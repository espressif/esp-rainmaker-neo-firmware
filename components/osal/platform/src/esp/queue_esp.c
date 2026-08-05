/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_queue.h"
#include "osal_mem_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

osal_queue_handle_t osal_queue_create(uint32_t queue_length, uint32_t item_size)
{
    return xQueueCreate(queue_length, item_size);
}

osal_queue_handle_t osal_queue_create_ext(uint32_t queue_length, uint32_t item_size)
{
    /* Single combined block: [StaticQueue_t control block][item storage]. The block start equals the
     * returned handle, so osal_queue_delete() frees it with one free(). */
    size_t cb_size = sizeof(StaticQueue_t);
    size_t storage_size = (size_t) queue_length * item_size;
    uint8_t *block = OSAL_MALLOC_EXTRAM(cb_size + storage_size);
    if (block == NULL) {
        return NULL;
    }

    StaticQueue_t *queue_cb = (StaticQueue_t *) block;
    uint8_t *queue_storage = block + cb_size;

    QueueHandle_t queue_handle = xQueueCreateStatic(queue_length, item_size, queue_storage, queue_cb);
    if (queue_handle == NULL) {
        free(block);
        return NULL;
    }
    return queue_handle;
}

void osal_queue_delete(osal_queue_handle_t queue_handle)
{
    /* Detect queues created via osal_queue_create_ext(): their backing buffer is statically
     * supplied and must be freed by us (vQueueDelete frees nothing for static objects). */
    uint8_t *queue_storage = NULL;
    StaticQueue_t *queue_cb = NULL;
    if (xQueueGetStaticBuffers(queue_handle, &queue_storage, &queue_cb) == pdTRUE) {
        vQueueDelete(queue_handle);
        /* queue_cb is the block start (storage lives in the same allocation). */
        free(queue_cb);
    } else {
        vQueueDelete(queue_handle);
    }
}

osal_err_t osal_queue_send(
    osal_queue_handle_t queue_handle,
    const void *item_to_queue,
    osal_tick_type_t ticks_to_wait
)
{
    if (xQueueSend(queue_handle, item_to_queue, ticks_to_wait) == pdPASS) {
        return OSAL_ERR_OK;
    } else {
        return OSAL_ERR_TIMEOUT;
    }
}

osal_err_t osal_queue_receive(
    osal_queue_handle_t queue_handle,
    void *buffer,
    osal_tick_type_t ticks_to_wait
)
{
    if (xQueueReceive(queue_handle, buffer, ticks_to_wait) == pdPASS) {
        return OSAL_ERR_OK;
    } else {
        return OSAL_ERR_TIMEOUT;
    }
}

osal_err_t osal_queue_peek(
    osal_queue_handle_t queue_handle,
    void *buffer,
    osal_tick_type_t ticks_to_wait
)
{
    if (xQueuePeek(queue_handle, buffer, ticks_to_wait) == pdPASS) {
        return OSAL_ERR_OK;
    } else {
        return OSAL_ERR_TIMEOUT;
    }
}
