/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file scheduler_esp.c
 * @brief ESP-IDF scheduler implementation.
 * @note This implementation uses the ESP-IDF timer API. The task handle is the timer handle of the timer used for scheduling the task.
 */

/* Declarations includes */
#include "osal_scheduler.h"

/* ESP-IDF includes */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

/* Global variables **************************************************************/

/* Tag for logging */
static const char *TAG = "osal_sched_esp";

/**
 * @brief Flag to indicate if the scheduler is initialized. Although no init is required for ESP-IDF scheduler,
 *        this flag is used to enforce proper init/deinit calls.
 */
static bool is_initialized = false;

/* Function definitions **********************************************************/

osal_err_t osal_scheduler_init(void)
{
    // No init is required for ESP-IDF scheduler, but log the initialization.
    is_initialized = true;
    ESP_LOGI(TAG, "Initialized ESP-IDF scheduler implementation.");
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_deinit(void)
{
    // No deinit is required for ESP-IDF scheduler.
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
        ESP_LOGE(TAG, "Scheduler is not initialized.");
        return OSAL_ERR_INVALID_STATE;
    }

    if (!handle || !task) {
        ESP_LOGE(TAG, "Invalid arguments. Must provide non-NULL handle and task.");
        return OSAL_ERR_INVALID_ARG;
    }

    // Create a new task.
    esp_timer_create_args_t timer_args = {
        .callback = (esp_timer_cb_t) task,
        .arg = arg,
    };
    esp_timer_handle_t timer_handle;
    if (esp_timer_create(&timer_args, &timer_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer.");
        *handle = NULL;
        return OSAL_ERR_INVALID_STATE;
    }

    // Start the timer (periodic or one-shot).
    esp_err_t start_result;
    if (is_periodic) {
        start_result = esp_timer_start_periodic(timer_handle, delay_ms * 1000);
    } else {
        start_result = esp_timer_start_once(timer_handle, delay_ms * 1000);
    }

    if (start_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer.");
        esp_timer_delete(timer_handle);
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
        ESP_LOGE(TAG, "Scheduler is not initialized.");
        return OSAL_ERR_INVALID_STATE;
    }

    if (!handle) {
        ESP_LOGE(TAG, "Invalid arguments. Must provide non-NULL handle.");
        return OSAL_ERR_INVALID_ARG;
    }

    esp_timer_handle_t timer_handle = (esp_timer_handle_t) handle;
    esp_timer_stop(timer_handle);
    if (esp_timer_start_once(timer_handle, delay_ms * 1000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset timer.");
        return OSAL_ERR_INVALID_STATE;
    }

    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_stop_timer(osal_scheduler_task_handle_t handle)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Scheduler is not initialized.");
        return OSAL_ERR_INVALID_STATE;
    }

    if (!handle) {
        ESP_LOGE(TAG, "Invalid arguments. Must provide non-NULL handle.");
        return OSAL_ERR_INVALID_ARG;
    }

    esp_timer_handle_t timer_handle = (esp_timer_handle_t) handle;
    esp_timer_stop(timer_handle);
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_cancel_task(osal_scheduler_task_handle_t *handle)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Scheduler is not initialized.");
        if (handle != NULL) {
            *handle = NULL;
        }
        return OSAL_ERR_INVALID_STATE;
    }

    if (!handle || !*handle) {
        ESP_LOGE(TAG, "Invalid arguments. Must provide non-NULL handle.");
        return OSAL_ERR_INVALID_ARG;
    }

    // Cancel the timer.
    esp_timer_handle_t timer_handle = (esp_timer_handle_t) * handle;
    esp_timer_stop(timer_handle);
    esp_timer_delete(timer_handle);

    *handle = NULL;
    return OSAL_ERR_OK;
}
