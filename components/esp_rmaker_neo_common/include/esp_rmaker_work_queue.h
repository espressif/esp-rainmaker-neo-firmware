/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_work_queue.h
 * @brief Work Queue for ESP RainMaker Neo
 */

#ifndef __ESP_RMAKER_WORK_QUEUE_H__
#define __ESP_RMAKER_WORK_QUEUE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_rmaker_error_types.h"

/**
 * @brief Prototype for ESP RainMaker Neo Work Queue Function
 *
 * @param[in] priv_data The private data associated with the work function.
 */
typedef void (*esp_rmaker_work_fn_t)(void *priv_data);

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the Work Queue
 *
 * This initializes the work queue, which is basically a mechanism to run
 * tasks in the context of a dedicated thread. You can start queueing tasks
 * after this, but they will get executed only after calling
 * esp_rmaker_work_queue_start().
 *
 * @return ESP_RMAKER_OK on success.
 * @return error on failure.
 */
esp_rmaker_error_t esp_rmaker_work_queue_init(void);

/**
 * @brief De-initialize the Work Queue
 *
 * This de-initializes the work queue. Note that the work queue needs to
 * be stopped using esp_rmaker_work_queue_stop() before calling this.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error on failure.
 */
esp_rmaker_error_t esp_rmaker_work_queue_deinit(void);

/**
 * @brief Start the Work Queue
 *
 * This starts the Work Queue thread which then starts executing the tasks queued.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error on failure.
 */
esp_rmaker_error_t esp_rmaker_work_queue_start(void);

/**
 * @brief Stop the Work Queue
 *
 * This stops a running Work Queue.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error on failure.
 */
esp_rmaker_error_t esp_rmaker_work_queue_stop(void);

/**
 * @brief Queue execution of a function in the Work Queue's context
 *
 * This API queues a work function for execution in the Work Queue Task's context.
 *
 * @param[in] work_fn The Work function to be queued.
 * @param[in] priv_data Private data to be passed to the work function.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error on failure.
 */
esp_rmaker_error_t esp_rmaker_work_queue_add_task(esp_rmaker_work_fn_t work_fn, void *priv_data);

/**
 * @brief Whether the caller runs on the Work Queue task.
 *
 * A function on the Work Queue cannot wait for work that the queue itself has to run.
 *
 * @return true if called from the Work Queue task.
 */
bool esp_rmaker_work_queue_is_current_task(void);

/* Work Queue instances *******************************************************/

/**
 * @brief Handle to a Work Queue instance.
 */
typedef struct esp_rmaker_work_queue *esp_rmaker_work_queue_handle_t;

/**
 * @brief Configuration for a Work Queue instance.
 */
typedef struct {
    const char *name;        /**< Task name, and the log prefix that tells two queues apart. Kept by
                                  pointer, so it must outlive the instance (a string literal does). */
    size_t queue_size;       /**< Number of entries the queue holds. */
    size_t task_stack;       /**< Stack depth of the worker task. */
    unsigned task_priority;  /**< Priority of the worker task. */
} esp_rmaker_work_queue_config_t;

/**
 * @brief Create a Work Queue instance.
 *
 * @note An instance lives for the process lifetime. There is deliberately no delete call: freeing
 *       the queue opens a use-after-free window against a producer calling add_task concurrently.
 *       Create each instance once and reuse it across init/deinit cycles.
 *
 * @param[in] config Instance configuration. Copied.
 *
 * @return The instance handle, or NULL on failure.
 */
esp_rmaker_work_queue_handle_t esp_rmaker_work_queue_create_inst(const esp_rmaker_work_queue_config_t *config);

/**
 * @brief Initialize a Work Queue instance.
 *
 * Creates the queue on first call and reopens it after a deinit. Queued work runs only after
 * esp_rmaker_work_queue_start_inst().
 *
 * @param[in] wq Instance handle.
 *
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t esp_rmaker_work_queue_init_inst(esp_rmaker_work_queue_handle_t wq);

/**
 * @brief De-initialize a Work Queue instance.
 *
 * Stops the worker if it runs, drains pending entries and parks the instance. The queue handle is
 * retained for the next init.
 *
 * @param[in] wq Instance handle.
 *
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t esp_rmaker_work_queue_deinit_inst(esp_rmaker_work_queue_handle_t wq);

/**
 * @brief Start a Work Queue instance's worker task.
 *
 * @param[in] wq Instance handle.
 *
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t esp_rmaker_work_queue_start_inst(esp_rmaker_work_queue_handle_t wq);

/**
 * @brief Stop a running Work Queue instance's worker task.
 *
 * Entries already queued run before the worker exits.
 *
 * @param[in] wq Instance handle.
 *
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t esp_rmaker_work_queue_stop_inst(esp_rmaker_work_queue_handle_t wq);

/**
 * @brief Queue a function on a Work Queue instance.
 *
 * @param[in] wq Instance handle.
 * @param[in] work_fn The work function to queue.
 * @param[in] priv_data Private data passed to the work function.
 * @param[in] timeout_ticks How long to block while the queue is full. 0 drops immediately.
 *
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t esp_rmaker_work_queue_add_task_inst(esp_rmaker_work_queue_handle_t wq,
        esp_rmaker_work_fn_t work_fn, void *priv_data, uint32_t timeout_ticks);

/**
 * @brief Report whether the calling task is this instance's worker task.
 *
 * @note Use this to run work inline instead of queueing it when a work function calls back into
 *       the same queue, which would otherwise deadlock.
 *
 * @param[in] wq Instance handle.
 *
 * @return true when the caller is the instance's worker task.
 */
bool esp_rmaker_work_queue_is_current_task_inst(esp_rmaker_work_queue_handle_t wq);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_WORK_QUEUE_H__ */
