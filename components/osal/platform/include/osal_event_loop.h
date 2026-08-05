/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_event_loop.h
 * @brief Event loop primitives: event bases, handler registration and posting.
 */

#ifndef __OSAL_EVENT_LOOP_H__
#define __OSAL_EVENT_LOOP_H__

#include <stdint.h>
#include <stddef.h>

#include "osal_err.h"
#include "osal_ticks.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Declare a new event base */
#define OSAL_EVENT_DECLARE_BASE(id) extern osal_event_base_t const id
/* Define a new event base */
#define OSAL_EVENT_DEFINE_BASE(id) osal_event_base_t const id = #id

/* Define a wildcard event ID. Events posted with this ID will be delivered to all registered handlers. */
#define OSAL_EVENT_ID_ANY -1

/**
 * @brief Event base type.
 */
typedef const char *osal_event_base_t;

/**
 * @brief Event handler function type.
 *
 * @param[in] event_handler_arg The argument to pass to the event handler.
 * @param[in] event_base The event base to send the event to.
 * @param[in] event_id The event id to send the event to.
 * @param[in] event_data The data to send with the event.
 * @note You should not free the event data in the event handler.
 */
typedef void (*osal_event_handler_t)(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief Event loop handle type.
 */
typedef void *osal_event_loop_handle_t;

/**
 * @brief Create the default event loop.
 *
 * @return
 *  - OSAL_ERR_OK: The event loop was created successfully.
 *  - OSAL_ERR_NO_MEM: The event loop could not be created because there was insufficient heap memory.
 *  - OSAL_ERR_INVALID_STATE: The event loop could not be created because the event loop is already created.
 *  - Others: failed to create the event loop.
 */
osal_err_t osal_event_loop_create_default(void);

/**
 * @brief Delete the default event loop.
 *
 * @return OSAL_ERR_OK: The event loop was deleted successfully, otherwise an error code.
 */
osal_err_t osal_event_loop_delete_default(void);

/**
 * @brief Register an event handler to the default event loop.
 *
 * @param[in] event_base The event base to register the event handler to.
 * @param[in] event_id The event id to register the event handler to.
 * @param[in] event_handler The event handler to register.
 * @param[in] event_handler_arg The argument to pass to the event handler.
 *
 * @return OSAL_ERR_OK: The event handler was registered successfully, otherwise an error code.
 */
osal_err_t osal_event_handler_register(osal_event_base_t event_base, int32_t event_id, osal_event_handler_t event_handler, void *event_handler_arg);

/**
 * @brief Unregister an event handler from the default event loop.
 *
 * @param[in] event_base The event base to unregister the event handler from.
 * @param[in] event_id The event id to unregister the event handler from.
 * @param[in] event_handler The event handler to unregister.
 *
 * @return OSAL_ERR_OK: The event handler was unregistered successfully, otherwise an error code.
 */
osal_err_t osal_event_handler_unregister(osal_event_base_t event_base, int32_t event_id, osal_event_handler_t event_handler);

/**
 * @brief Send an event to the default event loop.
 *
 * @param[in] event_base The event base to send the event to.
 * @param[in] event_id The event id to send the event to.
 * @param[in] event_data The data to send with the event.
 * @param[in] event_data_size The size of the event data.
 * @param[in] ticks_to_wait The number of ticks to block on a full event queue.
 *
 * @note The event data is copied to the event loop. The caller is responsible for freeing the event data after the event has been posted.
 * @return
 *  - OSAL_ERR_OK: The event was posted successfully.
 *  - OSAL_ERR_TIMEOUT: The event could not be posted because the event queue is full and the timeout was reached.
 *  - OSAL_ERR_INVALID_ARG: The event base and/or event id is invalid.
 *  - Others: failed to post the event.
 */
osal_err_t osal_event_post(osal_event_base_t event_base, int32_t event_id, void *event_data, size_t event_data_size, osal_tick_type_t ticks_to_wait);

#ifdef __cplusplus
}
#endif

#endif
