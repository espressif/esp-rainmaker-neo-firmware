/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

osal_err_t  osal_task_create(
    osal_task_function_t task_function,
    const char *name,
    uint32_t stack_depth,
    void *parameters,
    uint32_t priority,
    osal_task_handle_t *task_handle
)
{
    if (xTaskCreate(task_function, name, stack_depth, parameters, priority, (TaskHandle_t *) task_handle) == pdPASS) {
        return OSAL_ERR_OK;
    } else {
        return OSAL_ERR_NO_MEM;
    }
}

void osal_task_delete(osal_task_handle_t task_handle)
{
    vTaskDelete(task_handle);
}

void osal_task_delay(osal_tick_type_t ticks_to_delay)
{
    vTaskDelay(ticks_to_delay);
}

osal_tick_type_t osal_task_get_tick_count(void)
{
    return xTaskGetTickCount();
}

const char *osal_task_get_name(osal_task_handle_t task_handle)
{
    return pcTaskGetName((TaskHandle_t) task_handle);
}
