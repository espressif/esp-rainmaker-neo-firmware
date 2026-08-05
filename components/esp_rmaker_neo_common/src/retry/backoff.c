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
 * @param[in] delay_ms The delay in milliseconds.
 * @param[in] task The task to schedule.
 * @param[in] arg The argument to pass to the task.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t esp_rmaker_backoff_schedule_task(esp_rmaker_backoff_retry_context_t *p_retry_context, uint64_t delay_ms, osal_scheduler_task_t task, void *arg);

/* Private function definitions ***************************************************/

static uint64_t backoff_get_next_delay_ms(esp_rmaker_backoff_delay_context_t *p_delay_ctx)
{
    /* Get the next delay with jitter */
    uint64_t next_delay_ms = p_delay_ctx->delay_ms.current + (osal_random_generate() % (p_delay_ctx->params.max_jitter_ms + 1));

    /* Increment the delay */
    p_delay_ctx->delay_ms.current *= p_delay_ctx->params.exp_factor;
    if (p_delay_ctx->delay_ms.current > p_delay_ctx->delay_ms.max) {
        p_delay_ctx->delay_ms.current = p_delay_ctx->delay_ms.max;
    }

    /* Return the next delay */
    return next_delay_ms;
}

static esp_rmaker_error_t esp_rmaker_backoff_schedule_task(esp_rmaker_backoff_retry_context_t *p_retry_context, uint64_t delay_ms, osal_scheduler_task_t task, void *arg)
{
    if (p_retry_context == NULL || task == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_err_t err;
    if (p_retry_context->handle != NULL) {
        err = osal_scheduler_reset_timer(p_retry_context->handle, delay_ms);
    } else {
        err = osal_scheduler_schedule_task(&p_retry_context->handle, delay_ms, task, arg);
    }
    return err == OSAL_ERR_OK ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

/* Public function definitions ***********************************************/

void esp_rmaker_backoff_reset(esp_rmaker_backoff_retry_context_t *p_retry_context, uint64_t delay_ms)
{
    if (p_retry_context == NULL) {
        return;
    }

    /* Cancel any existing retries */
    if (p_retry_context->handle != NULL) {
        (void) osal_scheduler_cancel_task(&p_retry_context->handle);
        /*
         * Always drop our handle after attempting cancel. If the scheduler was
         * already deinitialized, cancel returns without nulling the out pointer,
         * which would otherwise leave a dangling handle for a later execute/resume.
         */
        p_retry_context->handle = NULL;
    }

    /* Reset the delay */
    p_retry_context->delay_ctx.delay_ms.current = delay_ms;
}

esp_rmaker_error_t esp_rmaker_backoff_retry(esp_rmaker_backoff_retry_context_t *p_retry_context, osal_scheduler_task_t task, void *arg)
{
    if (p_retry_context == NULL || task == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    uint64_t next_delay_ms = backoff_get_next_delay_ms(&p_retry_context->delay_ctx);
    return esp_rmaker_backoff_schedule_task(p_retry_context, next_delay_ms, task, arg);
}

esp_rmaker_error_t esp_rmaker_backoff_fire(esp_rmaker_backoff_retry_context_t *p_retry_context, osal_scheduler_task_t task, void *arg)
{
    if (p_retry_context == NULL || task == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    uint64_t next_delay_ms = p_retry_context->delay_ctx.delay_ms.current;
    return esp_rmaker_backoff_schedule_task(p_retry_context, next_delay_ms, task, arg);
}
