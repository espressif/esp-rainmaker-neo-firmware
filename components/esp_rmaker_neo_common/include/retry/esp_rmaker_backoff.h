/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_backoff.h
 * @brief Backoff algorithm for retrying tasks with exponential backoff.
 */

#ifndef __ESP_RMAKER_RETRY_BACKOFF_H__
#define __ESP_RMAKER_RETRY_BACKOFF_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Platform common includes */
#include "osal_scheduler.h"

/* Types ************************************************************************/

/**
 * @brief Context for the backoff delay calculation.
 */
typedef struct {
    /**
     * @brief Delay in milliseconds.
     */
    struct {
        uint64_t current; /**< The current delay in milliseconds. Should be set to the initial base delay. */
        uint64_t max; /**< The maximum delay in milliseconds. */
    } delay_ms; /**< The delay in milliseconds. */

    /**
     * @brief Parameters for the backoff.
     */
    struct {
        uint8_t exp_factor; /**< The exponential factor to use for the backoff. */
        uint16_t max_jitter_ms; /**< The maximum jitter in milliseconds. */
    } params; /**< The parameters for the backoff. */
} esp_rmaker_backoff_delay_context_t;

/** Context of a scheduled backoff retry */
typedef struct {
    osal_scheduler_task_handle_t handle; /**< The handle for the scheduled task */
    esp_rmaker_backoff_delay_context_t delay_ctx; /**< The backoff delay context for the retry algorithm */
} esp_rmaker_backoff_retry_context_t;

/* Constants **********************************************************************/

/**
 * @brief Default-initialised ::esp_rmaker_backoff_retry_context_t compound literal.
 *
 * No scheduled task, a 1 s base delay capped at 5 minutes, doubling each retry, with up to
 * 1 s of jitter.
 */
#define ESP_RMAKER_BACKOFF_DEFAULT_RETRY_CONTEXT() (esp_rmaker_backoff_retry_context_t) { \
    .handle = NULL, \
    .delay_ctx = { \
        .delay_ms = { \
            .current = 1000, /* 1 second */ \
            .max = 5 * 60 * 1000, /* 5 minutes */ \
        }, \
        .params = { \
            .exp_factor = 2, /* 2x the delay */ \
            .max_jitter_ms = 1000, /* 1 second */ \
        }, \
    }, \
}

/* Public function declarations ***************************************************/

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Reset the backoff retry.
 *
 * - Cancels any scheduled retries.
 * - Resets the delay to the initial base delay.
 * @param[in, out] p_retry_context The context for the backoff function.
 * @param[in] delay_ms The delay in milliseconds.
 */
void esp_rmaker_backoff_reset(esp_rmaker_backoff_retry_context_t *p_retry_context, uint64_t delay_ms);

/**
 * @brief Retry a task with backoff.
 *
 * The task is scheduled with the next delay with jitter.
 * @param[in,out] p_retry_context The context for the backoff function. The scheduled task
 *                                handle and the current delay are updated in place.
 * @param[in] task The task to schedule.
 * @param[in] arg The argument to pass to the task.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_backoff_retry(esp_rmaker_backoff_retry_context_t *p_retry_context, osal_scheduler_task_t task, void *arg);

/**
 * @brief Schedule a task immediately with the current next delay (no jitter).
 *
 * The delay is not incremented.
 *
 * @param[in,out] p_retry_context The context for the backoff function. The scheduled task
 *                                handle is updated in place.
 * @param[in] task The task to schedule.
 * @param[in] arg The argument to pass to the task.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_backoff_fire(esp_rmaker_backoff_retry_context_t *p_retry_context, osal_scheduler_task_t task, void *arg);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __ESP_RMAKER_RETRY_BACKOFF_H__ */
