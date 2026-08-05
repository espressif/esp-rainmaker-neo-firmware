/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_task.h
 * @brief Task primitives (create, delete, delay, notify).
 */

#ifndef __OSAL_TASK_H__
#define __OSAL_TASK_H__

#include <stdint.h>
#include <stddef.h>
#include "osal_err.h"
#include "osal_ticks.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *osal_task_handle_t;
typedef void (*osal_task_function_t)(void *pvParameters);

/**
 * @brief Create a new task.
 *
 * @param[in] task_function Pointer to the task entry function.
 * @param[in] name A descriptive name for the task.
 * @param[in] stack_depth The size of the task stack, in bytes.
 * @param[in] parameters Pointer that will be passed as the parameter to the task being created.
 * @param[in] priority The priority at which the task should run.
 * @param[out] task_handle Pointer to the handle of the created task. May be NULL if the
 *                         handle is not needed.
 *
 * @return
 *  - OSAL_ERR_OK: The task was created successfully.
 *  - OSAL_ERR_NO_MEM: The task could not be created because there was insufficient
 *    heap memory.
 *  - OSAL_ERR_INVALID_STATE: Task creation failed.
 */
osal_err_t osal_task_create(
    osal_task_function_t task_function,
    const char *name,
    uint32_t stack_depth,
    void *parameters,
    uint32_t priority,
    osal_task_handle_t *task_handle
);

/**
 * @brief Delete a task.
 *
 * @param[in] task_handle The handle of the task to be deleted. If NULL, the calling task will be deleted.
 */
void osal_task_delete(osal_task_handle_t task_handle);

/**
 * @brief Delay a task for a given number of ticks.
 *
 * @param[in] ticks_to_delay The number of ticks to delay.
 */
void osal_task_delay(osal_tick_type_t ticks_to_delay);

/**
 * @brief Get the current tick count.
 *
 * @return The current tick count.
 */
osal_tick_type_t osal_task_get_tick_count(void);

/**
 * @brief Get the name of a task.
 *
 * @param[in] task_handle The handle of the task to get the name of.
 *
 * @return The name of the task.
 */
const char *osal_task_get_name(osal_task_handle_t task_handle);

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_TASK_H__ */
