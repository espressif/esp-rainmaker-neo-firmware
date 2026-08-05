/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_scheduler.h
 * @brief Platform common scheduler.
 */

#ifndef __OSAL_SCHEDULER_H__
#define __OSAL_SCHEDULER_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdint.h>

/* Platform common error includes */
#include "osal_err.h"

/* Types ************************************************************************/

/**
 * @brief Task to be executed.
 */
typedef void (*osal_scheduler_task_t)(void *arg);

/**
 * @brief Scheduler handle.
 */
typedef void *osal_scheduler_task_handle_t;

/* Function declarations **********************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the scheduler.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t osal_scheduler_init(void);

/**
 * @brief Deinitialize the scheduler.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t osal_scheduler_deinit(void);

/**
 * @brief Schedule a task to be executed after a delay, one-shot.
 *
 * @param[out] handle Pointer to the scheduled task handle. Control of this task is done using this handle.
 * @param[in] delay_ms Delay in milliseconds.
 * @param[in] task Task to be executed.
 * @param[in] arg Argument to the task.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t osal_scheduler_schedule_task(osal_scheduler_task_handle_t *handle, uint64_t delay_ms, osal_scheduler_task_t task, void *arg);

/**
 * @brief Schedule a task to be executed every given delay, repeating.
 *
 * @param[out] handle Pointer to the scheduled task handle. Control of this task is done using this handle.
 * @param[in] delay_ms Delay in milliseconds between each execution.
 * @param[in] task Task to be executed.
 * @param[in] arg Argument to the task.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t osal_scheduler_schedule_task_periodic(osal_scheduler_task_handle_t *handle, uint64_t delay_ms, osal_scheduler_task_t task, void *arg);

/**
 * @brief Reset the timer for a scheduled task, with a new delay.
 *
 * @param[in] handle Scheduled task handle.
 * @param[in] delay_ms New delay in milliseconds.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t osal_scheduler_reset_timer(osal_scheduler_task_handle_t handle, uint64_t delay_ms);


/**
 * @brief Stop a scheduled task. Can be resumed using the same handle with reset_timer.
 * @param[in] handle Scheduled task handle.
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t osal_scheduler_stop_timer(osal_scheduler_task_handle_t handle);

/**
 * @brief Cancel a scheduled task, and sets the handle to NULL. The task will not be executed.
 *
 * @note The task handle is invalidated.
 *
 * @note Non-blocking, so it is safe from inside a task callback, including on
 *       that callback's own handle. It does not wait, though: a task already
 *       executing when the cancel lands runs to completion. Only a task not yet
 *       invoked is suppressed, so do not free heap state passed as the task
 *       argument right after cancelling - free it from the task itself, or tie
 *       it to the owning object's lifetime.
 *
 * @param[in,out] handle Pointer to the scheduled task handle.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t osal_scheduler_cancel_task(osal_scheduler_task_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_SCHEDULER_H__ */
