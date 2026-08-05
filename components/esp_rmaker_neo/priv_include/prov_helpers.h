/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file prov_helpers.h
 * @brief Provisioning registry interface
 */

#ifndef __PROV_H__
#define __PROV_H__

/* Includes *******************************************************/

/* Standard includes */
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* Event loop includes */
#include "osal_event_loop.h"

#include "osal_err.h"

/* Types *******************************************************/

/**
 * @brief Endpoint handler function type.
 *
 * @param[in] session_id The session ID.
 * @param[in] inbuf The input buffer.
 * @param[in] inlen The length of the input buffer.
 * @param[out] outbuf The output buffer.
 * @param[out] outlen The length of the output buffer.
 * @param[in] priv_data The private data.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
typedef osal_err_t (*prov_endpoint_handler_t)(
    uint32_t session_id,
    const uint8_t *inbuf, ssize_t inlen,
    uint8_t **outbuf, ssize_t *outlen,
    void *priv_data
);

/**
 * @brief Endpoint type.
 *
 * @param[in] name The name of the endpoint.
 * @param[in] handler The handler function.
 * @param[in] priv_data The private data to pass to the handler.
 */
typedef struct {
    const char *name;
    prov_endpoint_handler_t handler;
    void *priv_data;
    struct {
        const char *label;
        const char *version;
        const char **capabilities;
        size_t total_capabilities;
    } app_info;
} prov_endpoint_t;

/**
 * @brief Provisioning registry endpoint action function type.
 *
 * @param[in] endpoint The endpoint.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
typedef osal_err_t (*prov_endpoint_action_t)(const prov_endpoint_t *endpoint);

/**
 * @brief Provisioning registry registration information.
 *
 * Users of this registry
 * @param[in] event_base The event base.
 * @param[in] event_ids The event ids.
 * @param[in] endpoint_actions The endpoint actions.
 */
typedef struct {
    osal_event_base_t event_base;
    struct {
        int32_t prov_init;
        int32_t prov_start;
        /** Provisioning service stopped. Endpoints registered with this registry are gone
         * once it fires, so anything still waiting on a peer must give up here. */
        int32_t prov_end;
    } event_ids;
    struct {
        prov_endpoint_action_t endpoint_create;
        prov_endpoint_action_t endpoint_register;
    } actions;
} prov_registration_info_t;

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Latch onto the provisioning backend.
 *
 * This does the following:
 * - Registers an event handler for the provisioning initialized event and the provisioning started event.
 * - Calls the callbacks when the events are received.
 * The callbacks return an action that can be performed on the endpoint
 * @param[out] registration_info The registration information.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t prov_get_registration_info(prov_registration_info_t *registration_info);

#ifdef __cplusplus
}
#endif

#endif /* __PROV_H__ */
