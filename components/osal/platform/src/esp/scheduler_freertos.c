/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file scheduler_freertos.c
 * @brief FreeRTOS scheduler implementation.
 * @note This implementation uses the FreeRTOS software timer API. The task handle is the timer handle of the timer used for scheduling the task.
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
 * @brief Flag to indicate if the scheduler is initialized. Although no init is required for FreeRTOS scheduler,
 *        this flag is used to enforce proper init/deinit calls.
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

/**
 * @brief Timer private data.
 *
 * @note The chain is described by where it *ends*, not by how much is left, so
 *       a callback derives the next segment from these fields plus the clock
 *       instead of writing a balance back.
 */
typedef struct {
    osal_scheduler_task_t task_cb;
    void *task_arg;
    bool is_periodic;
    uint64_t total_ticks;    /* full delay (one whole period for periodic timers) */
    uint64_t deadline_ticks; /* absolute fire time; for periodic timers the first one */
    uint64_t segment_start;  /* tick the currently armed segment was armed at */
    bool in_callback;        /* a callback is using this block right now */
    bool cancelled;          /* cancelled mid-callback: the task must not run */
    bool handover;           /* the callback, not the canceller, owns the teardown */
} __scheduler_timer_priv_data_t;

/* Guards the fields above; the callback runs in the daemon task while the API
 * runs in the caller's, on either core. Held for field accesses only - never
 * across a timer command or the user task callback. */
static portMUX_TYPE s_priv_data_lock = portMUX_INITIALIZER_UNLOCKED;

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
 * @brief Arm the timer for the next segment of a split delay, from the callback.
 *
 * Zero block time: this runs in the daemon task, which is what drains the timer
 * command queue. That queue is shared system-wide, so the command can be
 * refused, leaving the timer dormant with nothing to re-arm it.
 *
 * @return true when the command was accepted.
 */
static bool __arm_segment(TimerHandle_t timer_handle, uint64_t period_ticks)
{
    return xTimerChangePeriod(timer_handle, (TickType_t) period_ticks, 0) == pdPASS;
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
 * @brief Read the cancellation flag under the lock.
 */
static bool __is_cancelled(__scheduler_timer_priv_data_t *timer_priv_data)
{
    taskENTER_CRITICAL(&s_priv_data_lock);
    bool cancelled = timer_priv_data->cancelled;
    taskEXIT_CRITICAL(&s_priv_data_lock);
    return cancelled;
}

/**
 * @brief Common timer callback.
 *
 * @note The claim (``in_callback``) spans the user task, not just the chain
 *       bookkeeping, so a cancel landing anywhere in it is seen: the task is
 *       suppressed if not yet invoked, and the teardown is handed here rather
 *       than the block being freed underneath.
 *
 * @param[in] timer_handle Timer handle.
 */
static void __scheduler_timer_common_cb(TimerHandle_t timer_handle)
{
    /* Resolve and claim under the lock: cancel detaches under the same lock, so
     * the block is either unreachable here (NULL) or safe until we release. */
    taskENTER_CRITICAL(&s_priv_data_lock);
    __scheduler_timer_priv_data_t *timer_priv_data = __scheduler_timer_priv_data_get(timer_handle);
    if (timer_priv_data == NULL) {
        taskEXIT_CRITICAL(&s_priv_data_lock);
        return;
    }
    timer_priv_data->in_callback = true;
    bool cancelled = timer_priv_data->cancelled;
    uint64_t total_ticks = timer_priv_data->total_ticks;
    uint64_t deadline_ticks = timer_priv_data->deadline_ticks;
    uint64_t segment_start = timer_priv_data->segment_start;
    bool is_periodic = timer_priv_data->is_periodic;
    osal_scheduler_task_t task_cb = timer_priv_data->task_cb;
    void *task_arg = timer_priv_data->task_arg;
    taskEXIT_CRITICAL(&s_priv_data_lock);

    if (!cancelled) {
        /* Decided from the clock and the absolute deadline - see osal_timer_chain.h. */
        uint64_t now = __now_ticks();
        osal_timer_chain_step_t step = osal_timer_chain_next(now, deadline_ticks, total_ticks,
                                       segment_start, is_periodic,
                                       OSAL_SCHEDULER_TIMER_MAX_TICKS,
                                       OSAL_SCHEDULER_TICK_SLACK);
        if (step.rearm) {
            if (__arm_segment(timer_handle, step.arm_ticks)) {
                /* So the next expiry knows the boundary it aimed at, however
                 * late the daemon runs it. */
                taskENTER_CRITICAL(&s_priv_data_lock);
                timer_priv_data->segment_start = now;
                taskEXIT_CRITICAL(&s_priv_data_lock);
            } else {
                OSAL_LOGE(TAG, "Timer command queue full; %s abandoned (%llu ticks were to be armed).",
                          step.run_task ? "periodic task is" : "a split delay is",
                          (unsigned long long) step.arm_ticks);
            }
        }

        /* Re-read: a cancel may have landed while the segment was being armed. */
        if (step.run_task && task_cb != NULL && !__is_cancelled(timer_priv_data)) {
            task_cb(task_arg);
        }
    }

    /* Release the claim. */
    taskENTER_CRITICAL(&s_priv_data_lock);
    timer_priv_data->in_callback = false;
    bool handover = timer_priv_data->handover;
    taskEXIT_CRITICAL(&s_priv_data_lock);

    if (handover) {
        /* Cancelled mid-callback, so the teardown is ours. The delete cannot
         * block: this is the daemon task that drains the command queue. */
        if (xTimerDelete(timer_handle, 0) != pdPASS) {
            OSAL_LOGE(TAG, "Timer command queue full; cancelled timer could not be deleted.");
        }
        free(timer_priv_data);
    }
}

/* Public functions ***********************************************************/

osal_err_t osal_scheduler_init(void)
{
    // No init is required for FreeRTOS scheduler, but log the initialization.
    is_initialized = true;
    OSAL_LOGI(TAG, "Initialized FreeRTOS scheduler implementation.");
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_deinit(void)
{
    // No deinit is required for FreeRTOS scheduler.
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
    timer_priv_data->is_periodic = is_periodic;
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

    // Start the timer.
    if (xTimerStart(timer_handle, portMAX_DELAY) != pdPASS) {
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

    // Stop the timer first
    if (xTimerIsTimerActive(timer_handle) == pdTRUE && xTimerStop(timer_handle, portMAX_DELAY) != pdPASS) {
        return OSAL_ERR_INVALID_STATE;
    }

    /* Publish only once stopped, so a failure above leaves the old deadline
     * intact. A callback in flight can still arm one segment against the old
     * one; the next expiry reads the new one. */
    taskENTER_CRITICAL(&s_priv_data_lock);
    __scheduler_timer_priv_data_t *timer_priv_data = __scheduler_timer_priv_data_get(timer_handle);
    if (timer_priv_data != NULL) {
        timer_priv_data->total_ticks = total_ticks;
        timer_priv_data->segment_start = __now_ticks();
        timer_priv_data->deadline_ticks = timer_priv_data->segment_start + total_ticks;
    }
    taskEXIT_CRITICAL(&s_priv_data_lock);

    // Reset the timer with new period
    if (xTimerChangePeriod(timer_handle, delay_ticks, portMAX_DELAY) != pdPASS) {
        return OSAL_ERR_INVALID_STATE;
    }

    // Start the timer again
    if (xTimerStart(timer_handle, portMAX_DELAY) != pdPASS) {
        return OSAL_ERR_INVALID_STATE;
    }

    return OSAL_ERR_OK;
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

    if (xTimerStop(timer_handle, portMAX_DELAY) != pdPASS) {
        return OSAL_ERR_INVALID_STATE;
    }

    return OSAL_ERR_OK;
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

    /* Detach first: the callback resolves under the same lock, so afterwards it
     * either cannot reach the block or has already claimed it - freeing then
     * would corrupt the heap under the daemon task. Later expiries see a NULL
     * timer ID and bail. */
    taskENTER_CRITICAL(&s_priv_data_lock);
    __scheduler_timer_priv_data_t *priv_data = __scheduler_timer_priv_data_get(timer_handle);
    if (priv_data == NULL) {
        /* A concurrent cancel already detached it and owns the teardown. */
        taskEXIT_CRITICAL(&s_priv_data_lock);
        *handle = NULL;
        return OSAL_ERR_OK;
    }
    vTimerSetTimerID(timer_handle, NULL);
    /* The callback re-reads this before invoking the task. */
    priv_data->cancelled = true;
    bool handover = priv_data->in_callback;
    priv_data->handover = handover;
    taskEXIT_CRITICAL(&s_priv_data_lock);

    if (handover) {
        /* A callback holds the claim and tears down on its way out. Not waited
         * for: the caller may be the daemon task itself (a task cancelling its
         * own timer), where waiting deadlocks. */
        *handle = NULL;
        return OSAL_ERR_OK;
    }

    /* Nothing holds the block, so this cancel owns the teardown. */
    if (xTimerIsTimerActive(timer_handle) == pdTRUE) {
        xTimerStop(timer_handle, portMAX_DELAY);
    }

    // Delete the timer, then free the detached private data
    xTimerDelete(timer_handle, portMAX_DELAY);
    free(priv_data);

    *handle = NULL;
    return OSAL_ERR_OK;
}
