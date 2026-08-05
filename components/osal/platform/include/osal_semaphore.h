/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_semaphore.h
 * @brief Semaphore and mutex primitives.
 */

#ifndef __OSAL_SEMAPHORE_H__
#define __OSAL_SEMAPHORE_H__

#include <stdint.h>
#include <stddef.h>
#include "osal_err.h"
#include "osal_ticks.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *osal_semaphore_handle_t;

/**
 * @brief Creates a new mutex semaphore.
 *
 * @return A handle to the created mutex semaphore, or NULL if the semaphore could not be created.
 */
osal_semaphore_handle_t osal_semaphore_create_mutex(void);

/**
 * @brief Creates a new recursive mutex.
 *
 * The owning task may take it more than once; it is released only after an
 * equal number of gives. Must be used with ::osal_semaphore_take_recursive and
 * ::osal_semaphore_give_recursive - the plain take/give are not valid on a
 * recursive mutex (FreeRTOS requires the recursive variants).
 *
 * @return A handle to the created mutex, or NULL if it could not be created.
 */
osal_semaphore_handle_t osal_semaphore_create_recursive_mutex(void);

/**
 * @brief Creates a new binary semaphore.
 *
 * @return A handle to the created binary semaphore, or NULL if the semaphore could not be created.
 */
osal_semaphore_handle_t osal_semaphore_create_binary(void);

/**
 * @brief Creates a new counting semaphore.
 *
 * @param[in] max_count The maximum count value that can be reached.
 * @param[in] initial_count The count value assigned to the semaphore when it is created.
 *
 * @return A handle to the created counting semaphore, or NULL if the semaphore could not be created.
 */
osal_semaphore_handle_t osal_semaphore_create_counting(uint32_t max_count, uint32_t initial_count);

/**
 * @brief Deletes a semaphore.
 *
 * @param[in] semaphore_handle A handle to the semaphore to be deleted.
 */
void osal_semaphore_delete(osal_semaphore_handle_t semaphore_handle);

/**
 * @brief Takes (acquires) a semaphore.
 *
 * @param[in] semaphore_handle A handle to the semaphore.
 * @param[in] ticks_to_wait The time in ticks to wait for the semaphore to become available.
 *
 * @return
 *  - OSAL_ERR_OK: The semaphore was taken.
 *  - OSAL_ERR_TIMEOUT: A timeout occurred before the semaphore could be taken.
 */
osal_err_t osal_semaphore_take(osal_semaphore_handle_t semaphore_handle, osal_tick_type_t ticks_to_wait);

/**
 * @brief Gives (releases) a semaphore.
 *
 * @param[in] semaphore_handle A handle to the semaphore.
 *
 * @return
 *  - OSAL_ERR_OK: The semaphore was given.
 *  - OSAL_ERR_FAIL: The semaphore could not be given.
 */
osal_err_t osal_semaphore_give(osal_semaphore_handle_t semaphore_handle);

/**
 * @brief Takes a recursive mutex, which the owning task may do repeatedly.
 *
 * @param[in] semaphore_handle A handle from ::osal_semaphore_create_recursive_mutex.
 * @param[in] ticks_to_wait The time in ticks to wait for the mutex.
 *
 * @return
 *  - OSAL_ERR_OK: The mutex was taken.
 *  - OSAL_ERR_TIMEOUT: A timeout occurred before the mutex could be taken.
 */
osal_err_t osal_semaphore_take_recursive(osal_semaphore_handle_t semaphore_handle, osal_tick_type_t ticks_to_wait);

/**
 * @brief Gives a recursive mutex. Releases it only once every nested take has
 *        been matched.
 *
 * @param[in] semaphore_handle A handle from ::osal_semaphore_create_recursive_mutex.
 *
 * @return
 *  - OSAL_ERR_OK: The give was accepted.
 *  - OSAL_ERR_FAIL: The mutex could not be given (e.g. not held by this task).
 */
osal_err_t osal_semaphore_give_recursive(osal_semaphore_handle_t semaphore_handle);


#ifdef __cplusplus
}
#endif


#endif /* __OSAL_SEMAPHORE_H__ */
