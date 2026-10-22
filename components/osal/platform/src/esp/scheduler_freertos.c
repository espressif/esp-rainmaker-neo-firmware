/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file scheduler_freertos.c
 * @brief FreeRTOS scheduler implementation.
 * @note This implementation uses the FreeRTOS software timer API. The task handle is the timer handle of the timer used for scheduling the task.
 * @note Scheduled tasks run on a dispatch task, not on the FreeRTOS timer task.
 * @note esp_timer is used only as a monotonic 64-bit clock (xTaskGetTickCount() is 32-bit and wraps),
 *       so this file is built for ESP-IDF targets only - see components/osal/CMakeLists.txt.
 */

/* Declarations includes */
#include "osal_scheduler.h"

/* Platform common includes */
#include "osal_mem_alloc.h"
#include "osal_log.h"
#include "osal_tick_math.h"
#include "osal_timer_chain.h"

/* FreeRTOS includes */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

/* ESP-IDF includes */
#include "esp_timer.h"

/* Standard includes */
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

/* Global variables **************************************************************/

/* Tag for logging */
static const char *TAG = "osal_sched_freertos";

/**
 * @brief Flag to indicate if the scheduler is initialized, to enforce proper init/deinit calls.
 */
static bool is_initialized = false;

/* One arm cannot exceed a 32-bit TickType_t, so longer delays are split across
 * several periods and re-armed without invoking the task. portMAX_DELAY/2 keeps
 * headroom and avoids the "block forever" sentinel: 24.8 days at 1 kHz, 248 at
 * 100 Hz. A year-out schedule needs the split. */
#define OSAL_SCHEDULER_TIMER_MAX_TICKS ((uint64_t)(portMAX_DELAY / 2))

/* The clock is derived from microseconds, so it can read one tick short of what
 * the timer counted down. Treat a target this close as reached. */
#define OSAL_SCHEDULER_TICK_SLACK 1ULL

/* Timer private data flags */
#define TIMER_FLAG_PERIODIC    (1U << 0) /* restarts after each period */
#define TIMER_FLAG_QUEUED      (1U << 1) /* in the pending list, waiting for the dispatch task */
#define TIMER_FLAG_STOPPED     (1U << 2) /* stopped by the API: expiries are ignored until the next reset */
#define TIMER_FLAG_IN_CALLBACK (1U << 3) /* the dispatch task is using this block right now */
#define TIMER_FLAG_CANCELLED   (1U << 4) /* cancelled: never runs again; the dispatch task tears it down if it holds the block */

/* Flags that stop queued expiries from running */
#define TIMER_FLAGS_NO_RUN     (TIMER_FLAG_CANCELLED | TIMER_FLAG_STOPPED)

/**
 * @brief Timer private data.
 *
 * @note The chain is described by where it *ends*, not by how much is left, so
 *       a callback derives the next segment from these fields plus the clock
 *       instead of writing a balance back.
 */
typedef struct __scheduler_timer_priv_data {
    TimerHandle_t timer;     /* timer that owns this block */
    osal_scheduler_task_t task_cb;
    void *task_arg;
    uint64_t total_ticks;    /* full delay (one whole period for periodic timers) */
    uint64_t deadline_ticks; /* absolute fire time; for periodic timers the first one */
    uint64_t segment_start;  /* tick the currently armed segment was armed at */
    uint32_t flags;          /* TIMER_FLAG_* */
    uint32_t pending_fires;  /* expiries merged into the queued entry; a stop clears them */
    uint64_t reset_ticks;    /* delay that the latest reset publishes in the daemon task */
    uint32_t reset_seq;      /* incremented by each reset and stop; only the latest reset publishes */
    struct __scheduler_timer_priv_data *next; /* pending list link */
} __scheduler_timer_priv_data_t;

/* Guards the fields above and the pending list. Held for field accesses only - never
 * across a timer command or the user task callback. */
static portMUX_TYPE s_priv_data_lock = portMUX_INITIALIZER_UNLOCKED;

/* Expired timers, in expiry order, that the dispatch task has not run yet */
static __scheduler_timer_priv_data_t *s_pending_head = NULL;
static __scheduler_timer_priv_data_t *s_pending_tail = NULL;

/* Wakes the dispatch task */
static SemaphoreHandle_t s_dispatch_sem = NULL;
static StaticSemaphore_t s_dispatch_sem_buf;

/* Orders the stop, reset and arm commands; the daemon task never waits for it */
static SemaphoreHandle_t s_cmd_mutex = NULL;
static StaticSemaphore_t s_cmd_mutex_buf;

/* Creation state of the dispatch objects, so concurrent init calls create them once */
#define DISPATCH_STATE_NONE     0U
#define DISPATCH_STATE_CREATING 1U
#define DISPATCH_STATE_READY    2U
static uint32_t s_dispatch_state = DISPATCH_STATE_NONE;

/* Private functions **********************************************************/

/**
 * @brief Convert a delay in milliseconds to a full tick count.
 *
 * Not pdMS_TO_TICKS(): it truncates to TickType_t before multiplying by
 * configTICK_RATE_HZ, arming the timer short. See osal_tick_math.h.
 */
static uint64_t __ticks_from_ms(uint64_t delay_ms)
{
    return osal_ticks_from_ms_64(delay_ms, configTICK_RATE_HZ);
}

/**
 * @brief Clamp a full tick count to one legal timer period (never 0).
 */
static TickType_t __period_ticks(uint64_t total_ticks)
{
    return (TickType_t) osal_tick_period_clamp(total_ticks, OSAL_SCHEDULER_TIMER_MAX_TICKS);
}

/**
 * @brief Monotonic tick count that does not wrap.
 *
 * xTaskGetTickCount() wraps every 2^32 ticks (497 days at 100 Hz), inside the
 * range this file expresses, so deadlines are kept against esp_timer's 64-bit
 * microsecond clock, converted to ticks to keep one unit throughout.
 */
static uint64_t __now_ticks(void)
{
    return ((uint64_t) esp_timer_get_time() * configTICK_RATE_HZ) / 1000000ULL;
}

/**
 * @brief Convert a caller's delay into a full tick count and the first period.
 *
 * A sub-tick delay is rounded up rather than rejected: the tick rate belongs to
 * this backend, not to the API, and the other backends accept such delays.
 *
 * @param[in]  delay_ms     Delay in milliseconds.
 * @param[out] total_ticks  Full delay in ticks, at least 1.
 * @param[out] period_ticks Period the first arm may use; anything beyond it is
 *                          waited out by re-arming, see osal_timer_chain.h.
 */
static void __split_delay(uint64_t delay_ms, uint64_t *total_ticks, TickType_t *period_ticks)
{
    *total_ticks = __ticks_from_ms(delay_ms);
    if (*total_ticks == 0) {
        OSAL_LOGD(TAG, "Delay of %llu ms is shorter than one tick at %u Hz; using one tick.",
                  (unsigned long long) delay_ms, (unsigned) configTICK_RATE_HZ);
        *total_ticks = 1;
    }
    *period_ticks = __period_ticks(*total_ticks);
}

/**
 * @brief Create a new timer private data.
 *
 * @param[in] task_cb Task callback function.
 * @param[in] task_arg Task argument.
 *
 * @return Pointer to the timer private data.
 */
static __scheduler_timer_priv_data_t *__scheduler_timer_priv_data_create(osal_scheduler_task_t task_cb, void *task_arg)
{
    __scheduler_timer_priv_data_t *data = OSAL_CALLOC_EXTRAM(1, sizeof(__scheduler_timer_priv_data_t));
    if (data == NULL) {
        return NULL;
    }
    data->task_cb = task_cb;
    data->task_arg = task_arg;
    return data;
}

/**
 * @brief Get the timer private data from the timer handle.
 *
 * @param[in] timer_handle Timer handle.
 *
 * @return Pointer to the timer private data.
 */
static __scheduler_timer_priv_data_t *__scheduler_timer_priv_data_get(TimerHandle_t timer_handle)
{
    return (__scheduler_timer_priv_data_t *) pvTimerGetTimerID(timer_handle);
}

/**
 * @brief Next step for an expiry of this timer; call it under the lock.
 */
static osal_timer_chain_step_t __chain_step(const __scheduler_timer_priv_data_t *timer_priv_data, uint64_t now)
{
    return osal_timer_chain_next(now, timer_priv_data->deadline_ticks, timer_priv_data->total_ticks,
                                 timer_priv_data->segment_start, (timer_priv_data->flags & TIMER_FLAG_PERIODIC) != 0,
                                 OSAL_SCHEDULER_TIMER_MAX_TICKS, OSAL_SCHEDULER_TICK_SLACK);
}

/**
 * @brief Publish a reset in the daemon task after its stop, if no later reset or stop replaced it.
 */
static void __publish_reset(void *timer, uint32_t reset_seq)
{
    uint64_t now = __now_ticks();
    taskENTER_CRITICAL(&s_priv_data_lock);
    __scheduler_timer_priv_data_t *timer_priv_data = __scheduler_timer_priv_data_get((TimerHandle_t) timer);
    if (timer_priv_data != NULL && timer_priv_data->reset_seq == reset_seq) {
        timer_priv_data->total_ticks = timer_priv_data->reset_ticks;
        timer_priv_data->segment_start = now;
        timer_priv_data->deadline_ticks = now + timer_priv_data->reset_ticks;
        timer_priv_data->pending_fires = 0;
        timer_priv_data->flags &= ~TIMER_FLAG_STOPPED;
    }
    taskEXIT_CRITICAL(&s_priv_data_lock);
}

/**
 * @brief Mark the timer stopped under the lock: drop its queued expiries and any pending reset.
 *
 * @return The new reset sequence number.
 */
static uint32_t __mark_stopped_locked(__scheduler_timer_priv_data_t *timer_priv_data)
{
    timer_priv_data->flags |= TIMER_FLAG_STOPPED;
    timer_priv_data->pending_fires = 0;
    return ++timer_priv_data->reset_seq;
}

/**
 * @brief Check, under the lock, that the timer has expiries to run and is not cancelled or stopped.
 */
static bool __runnable_locked(const __scheduler_timer_priv_data_t *timer_priv_data)
{
    return timer_priv_data->pending_fires > 0 && !(timer_priv_data->flags & TIMER_FLAGS_NO_RUN);
}

/**
 * @brief Check under the lock that the timer is not cancelled, stopped or reset since the expiries were taken.
 */
static bool __may_run(__scheduler_timer_priv_data_t *timer_priv_data, uint32_t taken_seq)
{
    taskENTER_CRITICAL(&s_priv_data_lock);
    bool may_run = !(timer_priv_data->flags & TIMER_FLAGS_NO_RUN) && timer_priv_data->reset_seq == taken_seq;
    taskEXIT_CRITICAL(&s_priv_data_lock);
    return may_run;
}

/**
 * @brief Run one expired timer in the dispatch task.
 *
 * @param[in] timer_priv_data Private data of the expired timer, taken from the pending list.
 */
static void __dispatch_one(__scheduler_timer_priv_data_t *timer_priv_data)
{
    taskENTER_CRITICAL(&s_priv_data_lock);
    /* The claim lasts across the user task, so a cancel at any point hands the teardown here */
    timer_priv_data->flags |= TIMER_FLAG_IN_CALLBACK;
    TimerHandle_t timer_handle = timer_priv_data->timer;
    osal_scheduler_task_t task_cb = timer_priv_data->task_cb;
    void *task_arg = timer_priv_data->task_arg;
    taskEXIT_CRITICAL(&s_priv_data_lock);

    /* Compute an arming step again under the command mutex, so a stop or reset comes before or after the arm */
    bool hold = false;
    uint64_t now = __now_ticks();
    taskENTER_CRITICAL(&s_priv_data_lock);
    osal_timer_chain_step_t step = __chain_step(timer_priv_data, now);
    if (__runnable_locked(timer_priv_data) && step.rearm) {
        taskEXIT_CRITICAL(&s_priv_data_lock);
        xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);
        hold = true;
        now = __now_ticks();
        taskENTER_CRITICAL(&s_priv_data_lock);
        step = __chain_step(timer_priv_data, now);
    }
    bool may_run = __runnable_locked(timer_priv_data);
    uint32_t fires = timer_priv_data->pending_fires;
    timer_priv_data->pending_fires = 0;
    timer_priv_data->flags &= ~TIMER_FLAG_QUEUED;
    uint32_t taken_seq = timer_priv_data->reset_seq;
    bool is_periodic = (timer_priv_data->flags & TIMER_FLAG_PERIODIC) != 0;
    taskEXIT_CRITICAL(&s_priv_data_lock);

    if (!may_run) {
        step.run_task = false;
        step.rearm = false;
    }
    if (step.rearm) {
        if (xTimerChangePeriod(timer_handle, (TickType_t) step.arm_ticks, portMAX_DELAY) == pdPASS) {
            taskENTER_CRITICAL(&s_priv_data_lock);
            timer_priv_data->segment_start = now;
            taskEXIT_CRITICAL(&s_priv_data_lock);
        } else {
            OSAL_LOGE(TAG, "Failed to arm the timer (%llu ticks).", (unsigned long long) step.arm_ticks);
        }
    }
    if (hold) {
        xSemaphoreGive(s_cmd_mutex);
    }

    /* A periodic timer that fits one period runs once per merged expiry; any other timer runs at most once. */
    uint32_t runs = step.run_task ? ((is_periodic && !step.rearm) ? fires : 1) : 0;
    for (uint32_t i = 0; i < runs && task_cb != NULL && __may_run(timer_priv_data, taken_seq); i++) {
        task_cb(task_arg);
    }

    /* Release the claim; if the timer expired again meanwhile, its queued run does the teardown. */
    taskENTER_CRITICAL(&s_priv_data_lock);
    timer_priv_data->flags &= ~TIMER_FLAG_IN_CALLBACK;
    bool teardown = (timer_priv_data->flags & (TIMER_FLAG_CANCELLED | TIMER_FLAG_QUEUED)) == TIMER_FLAG_CANCELLED;
    taskEXIT_CRITICAL(&s_priv_data_lock);

    if (teardown) {
        if (xTimerDelete(timer_handle, portMAX_DELAY) != pdPASS) {
            OSAL_LOGE(TAG, "Failed to delete a cancelled timer.");
        }
        free(timer_priv_data);
    }
}

/**
 * @brief Dispatch task: run the expired timers that the daemon task queued.
 */
static void __dispatch_task(void *unused)
{
    for (;;) {
        xSemaphoreTake(s_dispatch_sem, portMAX_DELAY);

        taskENTER_CRITICAL(&s_priv_data_lock);
        __scheduler_timer_priv_data_t *timer_priv_data = s_pending_head;
        s_pending_head = NULL;
        s_pending_tail = NULL;
        taskEXIT_CRITICAL(&s_priv_data_lock);

        while (timer_priv_data != NULL) {
            /* Read first: until __dispatch_one clears QUEUED, the daemon cannot relink the block and a cancel cannot free it. */
            __scheduler_timer_priv_data_t *next = timer_priv_data->next;
            __dispatch_one(timer_priv_data);
            timer_priv_data = next;
        }
    }
}

/**
 * @brief Add the timer to the pending list under the lock, unless it is there already.
 *
 * @return true when the timer was added, so the dispatch task must be woken.
 */
static bool __enqueue_locked(__scheduler_timer_priv_data_t *timer_priv_data)
{
    if (timer_priv_data->flags & TIMER_FLAG_QUEUED) {
        return false;
    }
    timer_priv_data->flags |= TIMER_FLAG_QUEUED;
    timer_priv_data->next = NULL;
    if (s_pending_tail != NULL) {
        s_pending_tail->next = timer_priv_data;
    } else {
        s_pending_head = timer_priv_data;
    }
    s_pending_tail = timer_priv_data;
    return true;
}

/**
 * @brief Timer callback in the daemon task: queue the timer for the dispatch task, or count the expiry if queued.
 *
 * @param[in] timer_handle Timer handle.
 */
static void __scheduler_timer_common_cb(TimerHandle_t timer_handle)
{
    taskENTER_CRITICAL(&s_priv_data_lock);
    __scheduler_timer_priv_data_t *timer_priv_data = __scheduler_timer_priv_data_get(timer_handle);
    bool notify = false;
    if (timer_priv_data != NULL && !(timer_priv_data->flags & TIMER_FLAG_STOPPED)) {
        notify = __enqueue_locked(timer_priv_data);
        if (timer_priv_data->pending_fires < CONFIG_OSAL_TIMER_FREERTOS_CATCH_UP_MAX) {
            timer_priv_data->pending_fires++;
        }
    }
    taskEXIT_CRITICAL(&s_priv_data_lock);

    if (notify) {
        xSemaphoreGive(s_dispatch_sem);
    }
}

/* Public functions ***********************************************************/

/**
 * @brief Create the dispatch task and its semaphores once; a concurrent caller waits for the first one.
 *
 * @return true when the dispatch objects exist.
 */
static bool __dispatch_create_once(void)
{
    for (;;) {
        uint32_t state = DISPATCH_STATE_NONE;
        if (__atomic_compare_exchange_n(&s_dispatch_state, &state, DISPATCH_STATE_CREATING, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            break;
        }
        if (state == DISPATCH_STATE_READY) {
            return true;
        }
        vTaskDelay(1);
    }

    s_dispatch_sem = xSemaphoreCreateBinaryStatic(&s_dispatch_sem_buf);
    s_cmd_mutex = xSemaphoreCreateMutexStatic(&s_cmd_mutex_buf);
    if (xTaskCreate(__dispatch_task, "osal_sched", CONFIG_OSAL_TIMER_FREERTOS_DISPATCH_TASK_STACK_SIZE, NULL,
                    CONFIG_OSAL_TIMER_FREERTOS_DISPATCH_TASK_PRIORITY, NULL) != pdPASS) {
        __atomic_store_n(&s_dispatch_state, DISPATCH_STATE_NONE, __ATOMIC_RELEASE);
        return false;
    }
    __atomic_store_n(&s_dispatch_state, DISPATCH_STATE_READY, __ATOMIC_RELEASE);
    return true;
}

osal_err_t osal_scheduler_init(void)
{
    if (!__dispatch_create_once()) {
        OSAL_LOGE(TAG, "Failed to create the scheduler dispatch task.");
        return OSAL_ERR_NO_MEM;
    }
    is_initialized = true;
    OSAL_LOGI(TAG, "Initialized FreeRTOS scheduler implementation.");
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_deinit(void)
{
    // The dispatch task stays, to run timers that expire after deinit.
    is_initialized = false;
    return OSAL_ERR_OK;
}

/**
 * @brief Internal helper function to schedule a task with periodic option.
 *
 * @param[out] handle Pointer to the scheduled task handle.
 * @param[in] delay_ms Delay in milliseconds between executions.
 * @param[in] task Task to be executed.
 * @param[in] arg Argument to the task.
 * @param[in] is_periodic True for periodic tasks, false for one-shot.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
static osal_err_t osal_scheduler_schedule_task_internal(osal_scheduler_task_handle_t *handle, uint64_t delay_ms, osal_scheduler_task_t task, void *arg, bool is_periodic)
{
    if (!is_initialized) {
        return OSAL_ERR_INVALID_STATE;
    }

    if (!handle || !task) {
        return OSAL_ERR_INVALID_ARG;
    }

    // Convert delay to ticks in 64-bit, then split it across periods if needed
    uint64_t total_ticks = 0;
    TickType_t delay_ticks = 0;
    __split_delay(delay_ms, &total_ticks, &delay_ticks);

    // Create a new task.
    __scheduler_timer_priv_data_t *timer_priv_data = __scheduler_timer_priv_data_create(task, arg);
    if (timer_priv_data == NULL) {
        *handle = NULL;
        return OSAL_ERR_NO_MEM;
    }
    timer_priv_data->flags = is_periodic ? TIMER_FLAG_PERIODIC : 0;
    timer_priv_data->total_ticks = total_ticks;
    timer_priv_data->segment_start = __now_ticks();
    timer_priv_data->deadline_ticks = timer_priv_data->segment_start + total_ticks;

    TimerHandle_t timer_handle = xTimerCreate(is_periodic ? "scheduler_task_periodic" : "scheduler_task",
                                 delay_ticks,
                                 is_periodic ? pdTRUE : pdFALSE, // Periodic or one-shot timer
                                 timer_priv_data,
                                 __scheduler_timer_common_cb);
    if (timer_handle == NULL) {
        free(timer_priv_data);
        *handle = NULL;
        return OSAL_ERR_NO_MEM;
    }
    timer_priv_data->timer = timer_handle;

    // Start the timer.
    if (xTimerStart(timer_handle, portMAX_DELAY) != pdPASS) {
        vTimerSetTimerID(timer_handle, NULL);
        xTimerDelete(timer_handle, portMAX_DELAY);
        free(timer_priv_data);
        *handle = NULL;
        return OSAL_ERR_INVALID_STATE;
    }

    *handle = (osal_scheduler_task_handle_t) timer_handle;
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_schedule_task(osal_scheduler_task_handle_t *handle, uint64_t delay_ms, osal_scheduler_task_t task, void *arg)
{
    return osal_scheduler_schedule_task_internal(handle, delay_ms, task, arg, false);
}

osal_err_t osal_scheduler_schedule_task_periodic(osal_scheduler_task_handle_t *handle, uint64_t delay_ms, osal_scheduler_task_t task, void *arg)
{
    return osal_scheduler_schedule_task_internal(handle, delay_ms, task, arg, true);
}

osal_err_t osal_scheduler_reset_timer(osal_scheduler_task_handle_t handle, uint64_t delay_ms)
{
    if (!is_initialized) {
        return OSAL_ERR_INVALID_STATE;
    }

    if (!handle) {
        return OSAL_ERR_INVALID_ARG;
    }

    TimerHandle_t timer_handle = (TimerHandle_t) handle;

    // Convert delay to ticks in 64-bit, then split it across periods if needed
    uint64_t total_ticks = 0;
    TickType_t delay_ticks = 0;
    __split_delay(delay_ms, &total_ticks, &delay_ticks);

    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);

    /* Mark stopped, as a stop does; commands run in order, so no expiry of the old delay follows the publish. */
    uint32_t reset_seq = 0;
    taskENTER_CRITICAL(&s_priv_data_lock);
    __scheduler_timer_priv_data_t *timer_priv_data = __scheduler_timer_priv_data_get(timer_handle);
    if (timer_priv_data != NULL) {
        timer_priv_data->reset_ticks = total_ticks;
        reset_seq = __mark_stopped_locked(timer_priv_data);
    }
    taskEXIT_CRITICAL(&s_priv_data_lock);

    /* A cancelled task is not armed again */
    osal_err_t err = OSAL_ERR_INVALID_STATE;
    if (timer_priv_data != NULL &&
            xTimerStop(timer_handle, portMAX_DELAY) == pdPASS &&
            xTimerPendFunctionCall(__publish_reset, timer_handle, reset_seq, portMAX_DELAY) == pdPASS &&
            xTimerChangePeriod(timer_handle, delay_ticks, portMAX_DELAY) == pdPASS) {
        err = OSAL_ERR_OK;
    }

    xSemaphoreGive(s_cmd_mutex);
    return err;
}

osal_err_t osal_scheduler_stop_timer(osal_scheduler_task_handle_t handle)
{
    if (!is_initialized) {
        return OSAL_ERR_INVALID_STATE;
    }

    if (!handle) {
        return OSAL_ERR_INVALID_ARG;
    }

    TimerHandle_t timer_handle = (TimerHandle_t) handle;

    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);

    /* Mark first: an expiry that the daemon task handles before the stop command is then dropped too. */
    taskENTER_CRITICAL(&s_priv_data_lock);
    __scheduler_timer_priv_data_t *timer_priv_data = __scheduler_timer_priv_data_get(timer_handle);
    if (timer_priv_data != NULL) {
        (void) __mark_stopped_locked(timer_priv_data);
    }
    taskEXIT_CRITICAL(&s_priv_data_lock);

    /* A cancelled task is already stopped */
    osal_err_t err = OSAL_ERR_OK;
    if (timer_priv_data != NULL && xTimerStop(timer_handle, portMAX_DELAY) != pdPASS) {
        err = OSAL_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_cmd_mutex);
    return err;
}

osal_err_t osal_scheduler_cancel_task(osal_scheduler_task_handle_t *handle)
{
    if (!is_initialized) {
        if (handle != NULL) {
            *handle = NULL;
        }
        return OSAL_ERR_INVALID_STATE;
    }

    if (!handle || !*handle) {
        return OSAL_ERR_INVALID_ARG;
    }

    TimerHandle_t timer_handle = (TimerHandle_t) * handle;
    *handle = NULL;
    xSemaphoreTake(s_cmd_mutex, portMAX_DELAY);

    /* Detach first: later expiries then see a NULL timer ID and cannot queue the block again. */
    taskENTER_CRITICAL(&s_priv_data_lock);
    __scheduler_timer_priv_data_t *priv_data = __scheduler_timer_priv_data_get(timer_handle);
    if (priv_data == NULL) {
        /* A concurrent cancel already detached it and owns the teardown. */
        taskEXIT_CRITICAL(&s_priv_data_lock);
        xSemaphoreGive(s_cmd_mutex);
        return OSAL_ERR_OK;
    }
    vTimerSetTimerID(timer_handle, NULL);
    /* The dispatch task re-reads this before invoking the task. */
    priv_data->flags |= TIMER_FLAG_CANCELLED;
    bool handover = (priv_data->flags & (TIMER_FLAG_IN_CALLBACK | TIMER_FLAG_QUEUED)) != 0;
    taskEXIT_CRITICAL(&s_priv_data_lock);

    if (!handover) {
        /* Nothing holds the block, so this cancel owns the teardown; else the dispatch task does it. */
        if (xTimerDelete(timer_handle, portMAX_DELAY) != pdPASS) {
            OSAL_LOGE(TAG, "Failed to delete a cancelled timer.");
        }
        free(priv_data);
    }

    xSemaphoreGive(s_cmd_mutex);
    return OSAL_ERR_OK;
}
