/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file retry_manager.c
 * @brief Implementation of the retry manager.
 */

/* Includes ****************************************************************/

/* Declarations */
#include "retry/manager.h"

/* Standard includes */
#include <string.h>

/* Work queue includes */
#include "esp_rmaker_work_queue.h"

/* Private function declarations ***********************************************/

/**
 * @brief Callback function for when a retry fails.
 *
 * @param[in] p_context The context that failed to retry.
 */
static void __retry_manager_on_failure(retry_manager_context_t *p_context);

/**
 * @brief Work queue task for the retry manager.
 *
 * @param[in] context_arg The argument to the work queue task. This should be a pointer to the retry manager context.
 */
static void __retry_manager_work_queue_task(void *context_arg);

/**
 * @brief Scheduler task for the retry manager.
 *
 * @param[in] context_arg The argument to the scheduler task. This should be a pointer to the retry manager context.
 */
static void __retry_manager_scheduler_task(void *context_arg);

/* Private function definitions ***********************************************/

static void __retry_manager_on_failure(retry_manager_context_t *p_context)
{
    if (p_context == NULL) {
        return;
    }
    if (p_context->callbacks.on_failure != NULL) {
        p_context->callbacks.on_failure();
    }
    esp_rmaker_backoff_retry(&p_context->backoff.ctx, __retry_manager_scheduler_task, (void *)p_context);
}

static void __retry_manager_work_queue_task(void *context_arg)
{
    retry_manager_context_t *p_context = (retry_manager_context_t *) context_arg;
    if (p_context == NULL) {
        return;
    }

    esp_rmaker_error_t err = p_context->task.func(p_context->task.priv_data);
    if (err != ESP_RMAKER_OK) {
        /* Failed to execute the task, schedule a new retry */
        __retry_manager_on_failure(p_context);
    } else if (p_context->backoff.reset_on_success) {
        /* Successfully executed the task, reset the backoff delay */
        esp_rmaker_backoff_reset(&p_context->backoff.ctx, p_context->backoff.base_delay_ms);
    }
}

static void __retry_manager_scheduler_task(void *context_arg)
{
    retry_manager_context_t *p_context = (retry_manager_context_t *) context_arg;
    if (p_context == NULL) {
        return;
    }

    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(__retry_manager_work_queue_task, p_context);
    if (err != ESP_RMAKER_OK) {
        __retry_manager_on_failure(p_context);
    }
}
/* Public function definitions ***********************************************/

void retry_manager_execute_context(retry_manager_context_t *p_context)
{
    if (p_context == NULL) {
        return;
    }

    /* Cancel any existing retries */
    esp_rmaker_backoff_reset(&p_context->backoff.ctx, p_context->backoff.base_delay_ms);

    /* Start the retry process */
    __retry_manager_scheduler_task(p_context);
}

void retry_manager_resume_context(retry_manager_context_t *p_context)
{
    if (p_context == NULL) {
        return;
    }

    /* Resume the retry process */
    esp_rmaker_backoff_retry(&p_context->backoff.ctx, __retry_manager_scheduler_task, (void *)p_context);
}

void retry_manager_stop_context(retry_manager_context_t *p_context)
{
    if (p_context == NULL) {
        return;
    }

    /* Cancel any existing retries */
    esp_rmaker_backoff_reset(&p_context->backoff.ctx, p_context->backoff.base_delay_ms);
}
