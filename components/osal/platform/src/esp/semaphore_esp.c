/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_semaphore.h"
#include "osal_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "osal_semaphore";

/* NULL handle -> INVALID_ARG (as on POSIX) instead of the configASSERT inside
 * xQueueSemaphoreTake/xQueueGenericSend. Not FAIL: that already means "operation
 * refused" here. Caller address logged so addr2line can find the bad call site. */
#define SEMAPHORE_NULL_GUARD()                                                             \
    do {                                                                                   \
        if (semaphore_handle == NULL) {                                                     \
            OSAL_LOGE(TAG, "%s on NULL semaphore (caller %p)", __func__,                    \
                      __builtin_return_address(0));                                         \
            return OSAL_ERR_INVALID_ARG;                                                    \
        }                                                                                   \
    } while (0)

osal_semaphore_handle_t osal_semaphore_create_mutex(void)
{
    return xSemaphoreCreateMutex();
}

osal_semaphore_handle_t osal_semaphore_create_recursive_mutex(void)
{
    return xSemaphoreCreateRecursiveMutex();
}

osal_semaphore_handle_t osal_semaphore_create_binary(void)
{
    return xSemaphoreCreateBinary();
}

osal_semaphore_handle_t osal_semaphore_create_counting(uint32_t max_count, uint32_t initial_count)
{
    return xSemaphoreCreateCounting(max_count, initial_count);
}

void osal_semaphore_delete(osal_semaphore_handle_t semaphore_handle)
{
    if (semaphore_handle == NULL) {
        return;
    }
    vSemaphoreDelete(semaphore_handle);
}

osal_err_t osal_semaphore_take(osal_semaphore_handle_t semaphore_handle, osal_tick_type_t ticks_to_wait)
{
    SEMAPHORE_NULL_GUARD();
    if (xSemaphoreTake(semaphore_handle, ticks_to_wait) == pdTRUE) {
        return OSAL_ERR_OK;
    } else {
        return OSAL_ERR_TIMEOUT;
    }
}

osal_err_t osal_semaphore_give(osal_semaphore_handle_t semaphore_handle)
{
    SEMAPHORE_NULL_GUARD();
    if (xSemaphoreGive(semaphore_handle) == pdTRUE) {
        return OSAL_ERR_OK;
    } else {
        return OSAL_ERR_FAIL;
    }
}

osal_err_t osal_semaphore_take_recursive(osal_semaphore_handle_t semaphore_handle, osal_tick_type_t ticks_to_wait)
{
    SEMAPHORE_NULL_GUARD();
    if (xSemaphoreTakeRecursive((SemaphoreHandle_t) semaphore_handle, (TickType_t) ticks_to_wait) != pdTRUE) {
        return OSAL_ERR_TIMEOUT;
    }
    return OSAL_ERR_OK;
}

osal_err_t osal_semaphore_give_recursive(osal_semaphore_handle_t semaphore_handle)
{
    SEMAPHORE_NULL_GUARD();
    if (xSemaphoreGiveRecursive((SemaphoreHandle_t) semaphore_handle) != pdTRUE) {
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}
