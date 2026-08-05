/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file manager.h
 * @brief Retry manager that uses the work queue to retry tasks with exponential backoff.
 */

#ifndef __RETRY_MANAGER_H__
#define __RETRY_MANAGER_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Retry includes */
#include "retry/esp_rmaker_backoff.h"

/* Types ************************************************************************/

/**
 * @brief Prototype for the retry work function. This function will be called by the retry manager to execute the retry.
 *
 * @param[in] priv_data The private data associated with the retry.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 * If an error is returned, the retry manager will call the on_failure_cb function and schedule a new retry.
 */
typedef esp_rmaker_error_t (*retry_work_fn_t)(void *priv_data);

/**
 * @brief Callback function for when a retry fails.
 */
typedef void (*retry_on_failure_cb_t)(void);

/**
 * @brief Context for the retry manager.
 */
typedef struct {
    /**
     * @brief Backoff context for the retry manager.
     */
    struct {
        uint64_t base_delay_ms; /**< The base delay in milliseconds. */
        bool reset_on_success; /**< Whether to reset the backoff delay on success. */
        esp_rmaker_backoff_retry_context_t ctx; /**< The backoff context for the retry algorithm. */
    } backoff;

    /**
     * @brief Task context for the retry manager.
     */
    struct {
        retry_work_fn_t func; /**< The function to execute for the retry. */
        void *priv_data; /**< The private data associated with the retry. */
    } task;

    /**
     * @brief Callback functions for the retry manager.
     */
    struct {
        retry_on_failure_cb_t on_failure; /**< The callback function to call when a retry fails. */
    } callbacks;
} retry_manager_context_t;

/* Public function declarations ***************************************************/

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Execute a retry context.
 * - Resets the backoff delay and starts the retry process.
 *
 * @param[in] p_context The context to execute. This should be a persistent context that can be reused.
 */
void retry_manager_execute_context(retry_manager_context_t *p_context);

/**
 * @brief Resume a retry context.
 * - Resumes the retry process with the current backoff delay.
 *
 * @param[in] p_context The context to resume. This should be a persistent context that can be reused.
 */
void retry_manager_resume_context(retry_manager_context_t *p_context);

/**
 * @brief Stop a retry context.
 * - Cancels any scheduled retries.
 * - Resets the backoff delay.
 *
 * @param[in] p_context The context to stop. This should be a persistent context that can be reused.
 */
void retry_manager_stop_context(retry_manager_context_t *p_context);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RETRY_MANAGER_H__ */
