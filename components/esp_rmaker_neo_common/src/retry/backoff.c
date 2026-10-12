/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file backoff.c
 * @brief Backoff retry algorithm implementation.
 */

/* Includes **********************************************************************/

/* Declarations includes */
#include "retry/esp_rmaker_backoff.h"

/* Platform common includes */
#include "osal_random.h"

/* Private function declarations ***************************************************/

/**s
 * @brief Get the next delay with jitter.
 * The delay is incremented by the exponential factor and a random jitter is added.
 * @param[in] p_delay_ctx The delay context.
 * @return The next delay in milliseconds.
 */
static uint64_t backoff_get_next_delay_ms(esp_rmaker_backoff_delay_context_t *p_delay_ctx);

/**
 * @brief Schedule a task with the given delay.
 * @param[in] p_retry_context The context for the backoff function.
 * @param[in] reset_gen The reset count, loaded before the delay was read.
 * @param[in] delay_ms The delay in milliseconds.
 * @param[in] keep_active If true, an active timer is not re-armed.
 * @param[in] task The task to schedule.
 * @param[in] arg The argument to pass to the task.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t esp_rmaker_backoff_schedule_task(esp_rmaker_backoff_retry_context_t *p_retry_context, uint32_t reset_gen, uint64_t delay_ms, bool keep_active, osal_scheduler_task_t task, void *arg);

/* Private function definitions ***************************************************/

static uint64_t backoff_get_next_delay_ms(esp_rmaker_backoff_delay_context_t *p_delay_ctx)
{
    /* Increment the delay; the CAS loop keeps concurrent retries from losing a step */
    uint64_t current = __atomic_load_n(&p_delay_ctx->delay_ms.current, __ATOMIC_ACQUIRE);
    uint64_t next;
    do {
        next = current * p_delay_ctx->params.exp_factor;
        if (next > p_delay_ctx->delay_ms.max) {
            next = p_delay_ctx->delay_ms.max;
        }
    } while (!__atomic_compare_exchange_n(&p_delay_ctx->delay_ms.current, &current, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));

    /* Return the previous delay with jitter */
    return current + (osal_random_generate() % (p_delay_ctx->params.max_jitter_ms + 1));
}

static esp_rmaker_error_t esp_rmaker_backoff_schedule_task(esp_rmaker_backoff_retry_context_t *p_retry_context, uint32_t reset_gen, uint64_t delay_ms, bool keep_active, osal_scheduler_task_t task, void *arg)
{
    /* Count this call first, so that a keep can tell an empty slot from a handle that another call holds */
    uint32_t busy_before = __atomic_fetch_add(&p_retry_context->busy, 1, __ATOMIC_ACQ_REL);

    /* Take the handle out of the context, so that no other task can cancel or re-arm it meanwhile */
    osal_scheduler_task_handle_t handle = __atomic_exchange_n(&p_retry_context->handle, NULL, __ATOMIC_ACQ_REL);
    osal_err_t err = OSAL_ERR_OK;
    if (handle == NULL) {
        /* In keep mode, a call in progress holds the handle or arms a timer */
        if (!keep_active || busy_before == 0) {
            err = osal_scheduler_schedule_task(&handle, delay_ms, task, arg);
        }
    } else if (!keep_active || !osal_scheduler_is_active(handle)) {
        err = osal_scheduler_reset_timer(handle, delay_ms);
    }

    /* Put the handle back; if a concurrent call installed its own timer, cancel this one */
    osal_scheduler_task_handle_t expected = NULL;
    if (handle != NULL && !__atomic_compare_exchange_n(&p_retry_context->handle, &expected, handle, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        (void) osal_scheduler_cancel_task(&handle);
    } else if (handle != NULL && __atomic_load_n(&p_retry_context->reset_gen, __ATOMIC_ACQUIRE) != reset_gen) {
        /* A reset ran meanwhile and found no handle; remove this timer unless another call took it */
        expected = handle;
        if (__atomic_compare_exchange_n(&p_retry_context->handle, &expected, NULL, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            (void) osal_scheduler_cancel_task(&handle);
        }
    }
    __atomic_sub_fetch(&p_retry_context->busy, 1, __ATOMIC_ACQ_REL);
    return err == OSAL_ERR_OK ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

/* Public function definitions ***********************************************/

void esp_rmaker_backoff_reset(esp_rmaker_backoff_retry_context_t *p_retry_context, uint64_t delay_ms)
{
    if (p_retry_context == NULL) {
        return;
    }

    /* Cancel any existing retries; increment the generation first, so that a retry in progress sees it */
    __atomic_add_fetch(&p_retry_context->reset_gen, 1, __ATOMIC_ACQ_REL);
    osal_scheduler_task_handle_t handle = __atomic_exchange_n(&p_retry_context->handle, NULL, __ATOMIC_ACQ_REL);
    if (handle != NULL) {
        (void) osal_scheduler_cancel_task(&handle);
    }

    /* Reset the delay */
    __atomic_store_n(&p_retry_context->delay_ctx.delay_ms.current, delay_ms, __ATOMIC_RELEASE);
}

esp_rmaker_error_t esp_rmaker_backoff_retry(esp_rmaker_backoff_retry_context_t *p_retry_context, osal_scheduler_task_t task, void *arg)
{
    if (p_retry_context == NULL || task == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    uint32_t reset_gen = __atomic_load_n(&p_retry_context->reset_gen, __ATOMIC_ACQUIRE);
    uint64_t next_delay_ms = backoff_get_next_delay_ms(&p_retry_context->delay_ctx);
    return esp_rmaker_backoff_schedule_task(p_retry_context, reset_gen, next_delay_ms, false, task, arg);
}

esp_rmaker_error_t esp_rmaker_backoff_fire(esp_rmaker_backoff_retry_context_t *p_retry_context, osal_scheduler_task_t task, void *arg)
{
    return esp_rmaker_backoff_fire_keep_retry(p_retry_context, UINT64_MAX, task, arg);
}

esp_rmaker_error_t esp_rmaker_backoff_fire_keep_retry(esp_rmaker_backoff_retry_context_t *p_retry_context, uint64_t base_delay_ms, osal_scheduler_task_t task, void *arg)
{
    if (p_retry_context == NULL || task == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    uint32_t reset_gen = __atomic_load_n(&p_retry_context->reset_gen, __ATOMIC_ACQUIRE);
    uint64_t delay_ms = __atomic_load_n(&p_retry_context->delay_ctx.delay_ms.current, __ATOMIC_ACQUIRE);
    return esp_rmaker_backoff_schedule_task(p_retry_context, reset_gen, delay_ms, delay_ms > base_delay_ms, task, arg);
}
