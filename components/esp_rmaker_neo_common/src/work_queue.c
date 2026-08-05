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

/* Platform Common includes */
#include "osal_task.h"
#include "osal_ticks.h"
#include "osal_queue.h"
#include "osal_log.h"

/* Configuration includes */
#include "sdkconfig.h"

#define ESP_RMAKER_TASK_QUEUE_SIZE  CONFIG_RMAKER_WORK_QUEUE_TASK_QUEUE_SIZE
#define ESP_RMAKER_TASK_STACK       CONFIG_RMAKER_WORK_QUEUE_TASK_STACK_SIZE
#define ESP_RMAKER_TASK_PRIORITY    CONFIG_RMAKER_WORK_QUEUE_TASK_PRIORITY

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

static osal_queue_handle_t work_queue;
static _Atomic esp_rmaker_work_queue_state_t queue_state;

static void esp_rmaker_handle_work_queue(void)
{
    esp_rmaker_work_queue_entry_t work_queue_entry;
    /* 2 sec delay to prevent spinning */
    osal_err_t ret = osal_queue_receive(work_queue, &work_queue_entry, osal_ticks_from_ms(2000));
    while (ret == OSAL_ERR_OK) {
        work_queue_entry.work_fn(work_queue_entry.priv_data);
        ret = osal_queue_receive(work_queue, &work_queue_entry, 0);
    }
}

static void esp_rmaker_work_queue_task(void *param)
{
    OSAL_LOGI(TAG, "RainMaker Work Queue task started.");
    while (atomic_load(&queue_state) != WORK_QUEUE_STATE_STOP_REQUESTED) {
        esp_rmaker_handle_work_queue();
    }
    OSAL_LOGI(TAG, "Stopping Work Queue task");
    atomic_store(&queue_state, WORK_QUEUE_STATE_INIT_DONE);
    osal_task_delete(NULL);
}

esp_rmaker_error_t esp_rmaker_work_queue_add_task(esp_rmaker_work_fn_t work_fn, void *priv_data)
{
    if (!work_queue) {
        OSAL_LOGE(TAG, "Cannot enqueue function as Work Queue hasn't been created.");
        return ESP_RMAKER_FAIL;
    }
    /* Reject work while parked (de-initialised). The handle is immortal so this
     * is a semantic gate, not a safety one - sending to the parked queue would
     * be harmless (drained on next init), but rejecting preserves the "no work
     * after deinit" contract. A stale/torn read of queue_state here can only
     * mis-accept onto a live handle, never crash. */
    if (atomic_load(&queue_state) == WORK_QUEUE_STATE_PARKED) {
        OSAL_LOGE(TAG, "Cannot enqueue function as Work Queue is parked (de-initialised).");
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_work_queue_entry_t work_queue_entry = {
        .work_fn = work_fn,
        .priv_data = priv_data,
    };
    if (osal_queue_send(work_queue, &work_queue_entry, 0) == OSAL_ERR_OK) {
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
static void __flush_work_queue(void)
{
    if (!work_queue) {
        return;
    }
    esp_rmaker_work_queue_entry_t work_queue_entry;
    while (osal_queue_receive(work_queue, &work_queue_entry, 0) == OSAL_ERR_OK) {
        /* discard */
    }
}

esp_rmaker_error_t esp_rmaker_work_queue_init(void)
{
    esp_rmaker_work_queue_state_t state = atomic_load(&queue_state);
    if (state == WORK_QUEUE_STATE_DEINIT) {
        /* First init: create the queue once. It is then immortal - never
         * freed for the process lifetime - so a producer can never send to a
         * freed handle (see esp_rmaker_work_queue_deinit). */
        work_queue = osal_queue_create_ext(ESP_RMAKER_TASK_QUEUE_SIZE, sizeof(esp_rmaker_work_queue_entry_t));
        if (!work_queue) {
            OSAL_LOGE(TAG, "Failed to create Work Queue.");
            return ESP_RMAKER_FAIL;
        }
        OSAL_LOGI(TAG, "Work Queue created.");
        atomic_store(&queue_state, WORK_QUEUE_STATE_INIT_DONE);
    } else if (state == WORK_QUEUE_STATE_PARKED) {
        /* Reopen the immortal queue for a new lifecycle (post-deinit reuse). */
        atomic_store(&queue_state, WORK_QUEUE_STATE_INIT_DONE);
    } else if (state != WORK_QUEUE_STATE_INIT_DONE) {
        /* Worker is (still) running - cannot flush without racing it. */
        OSAL_LOGW(TAG, "Work Queue already running; skipping flush on init.");
        return ESP_RMAKER_OK;
    }

    /* Flush regardless before returning, so no entry left over from a prior
     * lifecycle (the immortal queue is reused across deinit/reinit) survives
     * into the freshly initialised SDK. */
    __flush_work_queue();
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_work_queue_deinit(void)
{
    if (atomic_load(&queue_state) != WORK_QUEUE_STATE_STOP_REQUESTED) {
        esp_rmaker_work_queue_stop();
    }

    while (atomic_load(&queue_state) == WORK_QUEUE_STATE_STOP_REQUESTED) {
        OSAL_LOGI(TAG, "Waiting for Work Queue to be stopped...");
        osal_task_delay(osal_ticks_from_ms(2000));
    }

    esp_rmaker_work_queue_state_t state = atomic_load(&queue_state);
    if (state == WORK_QUEUE_STATE_DEINIT || state == WORK_QUEUE_STATE_PARKED) {
        /* Never created, or already parked - idempotent. */
        return ESP_RMAKER_OK;
    } else if (state != WORK_QUEUE_STATE_INIT_DONE) {
        OSAL_LOGE(TAG, "Cannot de-initialize Work Queue as the task is still running.");
        return ESP_RMAKER_FAIL;
    }

    /* Immortal queue: drain pending entries but NEVER free the handle.
     * Freeing it opened a use-after-free window - a producer (a retry timer,
     * schedule, etc.) firing add_task concurrently with queue_delete during a
     * reset would send to freed memory and crash. The handle lives for the
     * process lifetime; here it is drained and parked (closed to new work),
     * ready to be reopened by the next init. */
    __flush_work_queue();
    atomic_store(&queue_state, WORK_QUEUE_STATE_PARKED);
    OSAL_LOGI(TAG, "Work Queue drained and parked (handle retained)");
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_work_queue_start(void)
{
    esp_rmaker_work_queue_state_t state = atomic_load(&queue_state);
    if (state == WORK_QUEUE_STATE_RUNNING) {
        OSAL_LOGW(TAG, "Work Queue already started.");
        return ESP_RMAKER_OK;
    }
    if (state != WORK_QUEUE_STATE_INIT_DONE) {
        OSAL_LOGE(TAG, "Failed to start Work Queue as it wasn't initialized.");
        return ESP_RMAKER_FAIL;
    }
    if (osal_task_create(esp_rmaker_work_queue_task, "rmaker_queue_task", ESP_RMAKER_TASK_STACK,
                         NULL, ESP_RMAKER_TASK_PRIORITY, NULL) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Couldn't create Work Queue task");
        /* Immortal queue: leave the handle intact and parked at INIT_DONE so a
         * later start() can retry. Never free it (see the deinit rationale). */
        return ESP_RMAKER_FAIL;
    }
    atomic_store(&queue_state, WORK_QUEUE_STATE_RUNNING);
    return ESP_RMAKER_OK;
}

static void __empty_work_task(void *unused) {}

esp_rmaker_error_t esp_rmaker_work_queue_stop(void)
{
    /* Flip RUNNING -> STOP_REQUESTED atomically; only the caller that wins the
     * transition enqueues the wake task, so concurrent stop() calls can't
     * double-post or race the state. */
    esp_rmaker_work_queue_state_t expected = WORK_QUEUE_STATE_RUNNING;
    if (atomic_compare_exchange_strong(&queue_state, &expected, WORK_QUEUE_STATE_STOP_REQUESTED)) {
        esp_rmaker_work_queue_add_task(__empty_work_task, NULL); // Avoid waiting on the full queue delay with a dummy task
    }
    return ESP_RMAKER_OK;
}
