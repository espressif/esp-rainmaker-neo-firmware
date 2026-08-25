/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file work_queue.c
 * @brief Work Queue for ESP RainMaker Neo
 */

#include "esp_rmaker_work_queue.h"

/* Standard includes */
#include <stdatomic.h>
#include <stdlib.h>

/* Platform Common includes */
#include "osal_task.h"
#include "osal_ticks.h"
#include "osal_queue.h"
#include "osal_log.h"

/* Configuration includes */
#include "sdkconfig.h"

#define ESP_RMAKER_TASK_QUEUE_SIZE  CONFIG_ESP_RMAKER_NEO_WORK_QUEUE_TASK_QUEUE_SIZE
#define ESP_RMAKER_TASK_STACK       CONFIG_ESP_RMAKER_NEO_WORK_QUEUE_TASK_STACK
#define ESP_RMAKER_TASK_PRIORITY    CONFIG_ESP_RMAKER_NEO_WORK_QUEUE_TASK_PRIORITY

static const char *TAG = "rmng_work_queue";

typedef enum {
    WORK_QUEUE_STATE_DEINIT = 0,      /**< Never created; no handle. */
    WORK_QUEUE_STATE_INIT_DONE,       /**< Handle live, worker not running; accepts work. */
    WORK_QUEUE_STATE_RUNNING,         /**< Worker task draining; accepts work. */
    WORK_QUEUE_STATE_STOP_REQUESTED,  /**< Worker asked to exit; still accepts work (any entries already queued are executed before the worker exits). */
    WORK_QUEUE_STATE_PARKED,          /**< De-initialised: handle retained (immortal) but closed; rejects work. */
} esp_rmaker_work_queue_state_t;

typedef struct {
    esp_rmaker_work_fn_t work_fn;
    void *priv_data;
} esp_rmaker_work_queue_entry_t;

struct esp_rmaker_work_queue {
    const char *name;
    osal_queue_handle_t queue;
    _Atomic esp_rmaker_work_queue_state_t state;
    _Atomic(osal_task_handle_t) task;  /**< The running worker task, NULL when stopped. */
    size_t queue_size;
    size_t task_stack;
    unsigned task_priority;
};

/* The singleton every existing caller reaches through the un-suffixed API. */
static struct esp_rmaker_work_queue s_singleton = {
    .name = "rmaker_queue_task",
    .queue_size = ESP_RMAKER_TASK_QUEUE_SIZE,
    .task_stack = ESP_RMAKER_TASK_STACK,
    .task_priority = ESP_RMAKER_TASK_PRIORITY,
};

static void esp_rmaker_handle_work_queue(struct esp_rmaker_work_queue *wq)
{
    esp_rmaker_work_queue_entry_t work_queue_entry;
    /* 2 sec delay to prevent spinning */
    osal_err_t ret = osal_queue_receive(wq->queue, &work_queue_entry, osal_ticks_from_ms(2000));
    while (ret == OSAL_ERR_OK) {
        work_queue_entry.work_fn(work_queue_entry.priv_data);
        ret = osal_queue_receive(wq->queue, &work_queue_entry, 0);
    }
}

static void esp_rmaker_work_queue_task(void *param)
{
    struct esp_rmaker_work_queue *wq = (struct esp_rmaker_work_queue *)param;
    OSAL_LOGI(TAG, "%s: Work Queue task started.", wq->name);
    atomic_store(&wq->task, osal_task_get_current());
    while (atomic_load(&wq->state) != WORK_QUEUE_STATE_STOP_REQUESTED) {
        esp_rmaker_handle_work_queue(wq);
    }
    OSAL_LOGI(TAG, "%s: Stopping Work Queue task", wq->name);
    atomic_store(&wq->task, NULL);
    atomic_store(&wq->state, WORK_QUEUE_STATE_INIT_DONE);
    osal_task_delete(NULL);
}

static esp_rmaker_error_t __add_task(struct esp_rmaker_work_queue *wq, esp_rmaker_work_fn_t work_fn,
                                     void *priv_data, uint32_t timeout_ticks)
{
    if (!wq->queue) {
        OSAL_LOGE(TAG, "%s: Cannot enqueue function as Work Queue hasn't been created.", wq->name);
        return ESP_RMAKER_FAIL;
    }
    /* Reject work while parked (de-initialised). The handle is immortal so this
     * is a semantic gate, not a safety one - sending to the parked queue would
     * be harmless (drained on next init), but rejecting preserves the "no work
     * after deinit" contract. A stale/torn read of the state here can only
     * mis-accept onto a live handle, never crash. */
    if (atomic_load(&wq->state) == WORK_QUEUE_STATE_PARKED) {
        OSAL_LOGE(TAG, "%s: Cannot enqueue function as Work Queue is parked (de-initialised).", wq->name);
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_work_queue_entry_t work_queue_entry = {
        .work_fn = work_fn,
        .priv_data = priv_data,
    };
    if (osal_queue_send(wq->queue, &work_queue_entry, timeout_ticks) == OSAL_ERR_OK) {
        return ESP_RMAKER_OK;
    }
    return ESP_RMAKER_FAIL;
}

/* Drain and discard every pending entry. Callers MUST ensure the worker task
 * is not running (state INIT_DONE), otherwise this races the consumer.
 *
 * Entries carry an opaque void* priv_data with no generic free hook, so a
 * discarded entry's priv_data leaks - exactly as the previous queue_delete
 * teardown leaked in-flight entries. The point of flushing is correctness, not
 * accounting: it drops stale tasks (e.g. a state-report retry enqueued before a
 * reset) so they can never execute against freshly re-initialised state. */
static void __flush_work_queue(struct esp_rmaker_work_queue *wq)
{
    if (!wq->queue) {
        return;
    }
    esp_rmaker_work_queue_entry_t work_queue_entry;
    while (osal_queue_receive(wq->queue, &work_queue_entry, 0) == OSAL_ERR_OK) {
        /* discard */
    }
}

static esp_rmaker_error_t __init(struct esp_rmaker_work_queue *wq)
{
    esp_rmaker_work_queue_state_t state = atomic_load(&wq->state);
    if (state == WORK_QUEUE_STATE_DEINIT) {
        /* First init: create the queue once. It is then immortal - never
         * freed for the process lifetime - so a producer can never send to a
         * freed handle (see esp_rmaker_work_queue_deinit). */
        wq->queue = osal_queue_create_ext(wq->queue_size, sizeof(esp_rmaker_work_queue_entry_t));
        if (!wq->queue) {
            OSAL_LOGE(TAG, "%s: Failed to create Work Queue.", wq->name);
            return ESP_RMAKER_FAIL;
        }
        OSAL_LOGI(TAG, "%s: Work Queue created.", wq->name);
        atomic_store(&wq->state, WORK_QUEUE_STATE_INIT_DONE);
    } else if (state == WORK_QUEUE_STATE_PARKED) {
        /* Reopen the immortal queue for a new lifecycle (post-deinit reuse). */
        atomic_store(&wq->state, WORK_QUEUE_STATE_INIT_DONE);
    } else if (state != WORK_QUEUE_STATE_INIT_DONE) {
        /* Worker is (still) running - cannot flush without racing it. */
        OSAL_LOGW(TAG, "%s: Work Queue already running; skipping flush on init.", wq->name);
        return ESP_RMAKER_OK;
    }

    /* Flush regardless before returning, so no entry left over from a prior
     * lifecycle (the immortal queue is reused across deinit/reinit) survives
     * into the freshly initialised SDK. */
    __flush_work_queue(wq);
    return ESP_RMAKER_OK;
}

static void __empty_work_task(void *unused) {}

static esp_rmaker_error_t __stop(struct esp_rmaker_work_queue *wq)
{
    /* Flip RUNNING -> STOP_REQUESTED atomically; only the caller that wins the
     * transition enqueues the wake task, so concurrent stop() calls can't
     * double-post or race the state. */
    esp_rmaker_work_queue_state_t expected = WORK_QUEUE_STATE_RUNNING;
    if (atomic_compare_exchange_strong(&wq->state, &expected, WORK_QUEUE_STATE_STOP_REQUESTED)) {
        __add_task(wq, __empty_work_task, NULL, 0); // Avoid waiting on the full queue delay with a dummy task
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __deinit(struct esp_rmaker_work_queue *wq)
{
    if (atomic_load(&wq->state) != WORK_QUEUE_STATE_STOP_REQUESTED) {
        __stop(wq);
    }

    while (atomic_load(&wq->state) == WORK_QUEUE_STATE_STOP_REQUESTED) {
        OSAL_LOGI(TAG, "%s: Waiting for Work Queue to be stopped...", wq->name);
        osal_task_delay(osal_ticks_from_ms(2000));
    }

    esp_rmaker_work_queue_state_t state = atomic_load(&wq->state);
    if (state == WORK_QUEUE_STATE_DEINIT || state == WORK_QUEUE_STATE_PARKED) {
        /* Never created, or already parked - idempotent. */
        return ESP_RMAKER_OK;
    } else if (state != WORK_QUEUE_STATE_INIT_DONE) {
        OSAL_LOGE(TAG, "%s: Cannot de-initialize Work Queue as the task is still running.", wq->name);
        return ESP_RMAKER_FAIL;
    }

    /* Immortal queue: drain pending entries but NEVER free the handle.
     * Freeing it opened a use-after-free window - a producer (a retry timer,
     * schedule, etc.) firing add_task concurrently with queue_delete during a
     * reset would send to freed memory and crash. The handle lives for the
     * process lifetime; here it is drained and parked (closed to new work),
     * ready to be reopened by the next init. */
    __flush_work_queue(wq);
    atomic_store(&wq->state, WORK_QUEUE_STATE_PARKED);
    OSAL_LOGI(TAG, "%s: Work Queue drained and parked (handle retained)", wq->name);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __start(struct esp_rmaker_work_queue *wq)
{
    esp_rmaker_work_queue_state_t state = atomic_load(&wq->state);
    if (state == WORK_QUEUE_STATE_RUNNING) {
        OSAL_LOGW(TAG, "%s: Work Queue already started.", wq->name);
        return ESP_RMAKER_OK;
    }
    if (state != WORK_QUEUE_STATE_INIT_DONE) {
        OSAL_LOGE(TAG, "%s: Failed to start Work Queue as it wasn't initialized.", wq->name);
        return ESP_RMAKER_FAIL;
    }
    if (osal_task_create(esp_rmaker_work_queue_task, wq->name, wq->task_stack,
                         wq, wq->task_priority, NULL) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "%s: Couldn't create Work Queue task", wq->name);
        /* Immortal queue: leave the handle intact and parked at INIT_DONE so a
         * later start() can retry. Never free it (see the deinit rationale). */
        return ESP_RMAKER_FAIL;
    }
    atomic_store(&wq->state, WORK_QUEUE_STATE_RUNNING);
    return ESP_RMAKER_OK;
}

static bool __is_current_task(struct esp_rmaker_work_queue *wq)
{
    osal_task_handle_t task = atomic_load(&wq->task);
    return task && task == osal_task_get_current();
}

/* Singleton API **************************************************************/

esp_rmaker_error_t esp_rmaker_work_queue_add_task(esp_rmaker_work_fn_t work_fn, void *priv_data)
{
    return __add_task(&s_singleton, work_fn, priv_data, 0);
}

esp_rmaker_error_t esp_rmaker_work_queue_init(void)
{
    return __init(&s_singleton);
}

esp_rmaker_error_t esp_rmaker_work_queue_deinit(void)
{
    return __deinit(&s_singleton);
}

esp_rmaker_error_t esp_rmaker_work_queue_start(void)
{
    return __start(&s_singleton);
}

esp_rmaker_error_t esp_rmaker_work_queue_stop(void)
{
    return __stop(&s_singleton);
}

bool esp_rmaker_work_queue_is_current_task(void)
{
    return __is_current_task(&s_singleton);
}

/* Instance API ***************************************************************/

esp_rmaker_work_queue_handle_t esp_rmaker_work_queue_create_inst(const esp_rmaker_work_queue_config_t *config)
{
    if (!config || !config->name || config->queue_size == 0 || config->task_stack == 0) {
        return NULL;
    }
    struct esp_rmaker_work_queue *wq = calloc(1, sizeof(*wq));
    if (!wq) {
        OSAL_LOGE(TAG, "%s: Failed to allocate the Work Queue instance.", config->name);
        return NULL;
    }
    wq->name = config->name;
    wq->queue_size = config->queue_size;
    wq->task_stack = config->task_stack;
    wq->task_priority = config->task_priority;
    atomic_store(&wq->state, WORK_QUEUE_STATE_DEINIT);
    return wq;
}

esp_rmaker_error_t esp_rmaker_work_queue_init_inst(esp_rmaker_work_queue_handle_t wq)
{
    return wq ? __init(wq) : ESP_RMAKER_INVALID_ARG;
}

esp_rmaker_error_t esp_rmaker_work_queue_deinit_inst(esp_rmaker_work_queue_handle_t wq)
{
    return wq ? __deinit(wq) : ESP_RMAKER_INVALID_ARG;
}

esp_rmaker_error_t esp_rmaker_work_queue_start_inst(esp_rmaker_work_queue_handle_t wq)
{
    return wq ? __start(wq) : ESP_RMAKER_INVALID_ARG;
}

esp_rmaker_error_t esp_rmaker_work_queue_stop_inst(esp_rmaker_work_queue_handle_t wq)
{
    return wq ? __stop(wq) : ESP_RMAKER_INVALID_ARG;
}

esp_rmaker_error_t esp_rmaker_work_queue_add_task_inst(esp_rmaker_work_queue_handle_t wq,
        esp_rmaker_work_fn_t work_fn, void *priv_data, uint32_t timeout_ticks)
{
    if (!wq || !work_fn) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __add_task(wq, work_fn, priv_data, timeout_ticks);
}

bool esp_rmaker_work_queue_is_current_task_inst(esp_rmaker_work_queue_handle_t wq)
{
    return wq != NULL && __is_current_task(wq);
}
