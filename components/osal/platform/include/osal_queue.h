/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_queue.h
 * @brief Queue primitives (create, send, receive).
 */

#ifndef __OSAL_QUEUE_H__
#define __OSAL_QUEUE_H__

#include <stdint.h>
#include <stddef.h>

#include "osal_err.h"
#include "osal_task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *osal_queue_handle_t;

/**
 * @brief Creates a new queue.
 *
 * @param[in] queue_length The maximum number of items that the queue can hold.
 * @param[in] item_size The size, in bytes, of each item in the queue.
 *
 * @return A handle to the created queue, or NULL if the queue could not be created.
 */
osal_queue_handle_t osal_queue_create(uint32_t queue_length, uint32_t item_size);

/**
 * @brief Creates a new queue with its control block and item storage placed in external RAM (PSRAM) when available.
 *
 * @note Falls back to internal RAM if external RAM is unavailable. The returned handle is deleted with
 *       osal_queue_delete(), same as a normally-created queue.
 *
 * @warning The queue storage may reside in PSRAM. It is therefore UNSAFE to use such a queue from an ISR
 *          (PSRAM is inaccessible while the cache is disabled during flash operations). This queue API
 *          exposes no ISR entrypoints, so this is safe as long as the raw handle is not
 *          accessed directly from an ISR.
 *
 * @param[in] queue_length The maximum number of items that the queue can hold.
 * @param[in] item_size The size, in bytes, of each item in the queue.
 *
 * @return A handle to the created queue, or NULL if the queue could not be created.
 */
osal_queue_handle_t osal_queue_create_ext(uint32_t queue_length, uint32_t item_size);

/**
 * @brief Deletes a queue.
 *
 * @param[in] queue_handle A handle to the queue to be deleted.
 */
void osal_queue_delete(osal_queue_handle_t queue_handle);

/**
 * @brief Sends an item to the back of a queue.
 *
 * @param[in] queue_handle A handle to the queue.
 * @param[in] item_to_queue A pointer to the item to be placed on the queue.
 * @param[in] ticks_to_wait The time in ticks to wait for space to become available on the queue.
 *
 * @return
 *  - OSAL_ERR_OK: The item was successfully sent to the queue.
 *  - OSAL_ERR_TIMEOUT: A timeout occurred before space became available on the queue.
 */
osal_err_t osal_queue_send(
    osal_queue_handle_t queue_handle,
    const void *item_to_queue,
    osal_tick_type_t ticks_to_wait
);

/**
 * @brief Receives an item from a queue.
 *
 * @param[in] queue_handle A handle to the queue.
 * @param[out] buffer A pointer to the buffer into which the received item will be copied.
 * @param[in] ticks_to_wait The time in ticks to wait for an item to become available on the queue.
 *
 * @return
 *  - OSAL_ERR_OK: An item was successfully received from the queue.
 *  - OSAL_ERR_TIMEOUT: A timeout occurred before an item became available on the queue.
 */
osal_err_t osal_queue_receive(
    osal_queue_handle_t queue_handle,
    void *buffer,
    osal_tick_type_t ticks_to_wait
);

/**
 * @brief Peeks at an item from the front of a queue without removing it.
 *
 * @param[in] queue_handle A handle to the queue.
 * @param[out] buffer A pointer to the buffer into which the peeked item will be copied.
 * @param[in] ticks_to_wait The time in ticks to wait for an item to become available on the queue.
 *
 * @return
 *  - OSAL_ERR_OK: An item was successfully peeked from the queue.
 *  - OSAL_ERR_TIMEOUT: A timeout occurred before an item became available on the queue.
 */
osal_err_t osal_queue_peek(
    osal_queue_handle_t queue_handle,
    void *buffer,
    osal_tick_type_t ticks_to_wait
);

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_QUEUE_H__ */
