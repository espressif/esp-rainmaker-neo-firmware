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
 * @brief App info labels the provisioning backend advertises.
 *
 * @note The label strings and their versions live in the registry. Capabilities pick a
 *       label; every capability declared under one label merges into that label's entry.
 */
typedef enum {
    PROV_APP_LABEL_INVALID = 0,
    PROV_APP_LABEL_RMAKER,
    PROV_APP_LABEL_RMAKER_EXTRA,
    PROV_APP_LABEL_MAX,
} prov_app_label_t;

/**
 * @brief A protocomm endpoint.
 */
typedef struct {
    /** Endpoint name. */
    const char *name;
    /** Handler for the endpoint. */
    prov_endpoint_handler_t handler;
    /** Private data to pass to the handler. */
    void *priv_data;
    /** Optional. Runs on the event loop task once the provisioning service has stopped. */
    void (*on_prov_end)(void *priv_data);
} prov_endpoint_t;

/**
 * @brief A capability the provisioning backend advertises in its app info.
 *
 * A feature declares its capability once, however many endpoints serve it.
 */
typedef struct {
    /** The label to advertise the capability under. */
    prov_app_label_t label;
    /** The capability to advertise under `label`. */
    const char *capability;
} prov_capability_t;

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add an endpoint to the provisioning registry.
 *
 * The registry owns the provisioning events: it creates the endpoint when provisioning
 * initializes, registers the handler when provisioning starts, and calls `on_prov_end` when
 * provisioning stops. The endpoint stays across provisioning sessions until ::prov_del_endpoint
 * removes it. Call this before provisioning starts.
 *
 * @note The endpoint must have static storage duration: the registry keeps the pointer.
 *       Adding the same pointer again overrides its entry rather than adding a second one.
 * @note Not thread safe against another registry call. Call them from the application task
 *       only, which is where bring-up and teardown already run.
 *
 * @param[in] endpoint The endpoint to add.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t prov_add_endpoint(const prov_endpoint_t *endpoint);

/**
 * @brief Remove an endpoint from the provisioning registry.
 *
 * Removing the last endpoint and capability also drops the registry's event registrations.
 * An endpoint that is not in the registry is a success: the end state is the same.
 *
 * @note Not thread safe against another registry call; see ::prov_add_endpoint. Removal does
 *       not wait for an event dispatch already in flight, so the endpoint's ::on_prov_end may
 *       still be running when this returns.
 *
 * @param[in] endpoint The endpoint to remove. Matched by pointer.
 *
 * @return OSAL_ERR_OK on success, or if the endpoint is not in the registry.
 * @return OSAL_ERR_INVALID_ARG if `endpoint` is NULL.
 */
osal_err_t prov_del_endpoint(const prov_endpoint_t *endpoint);

/**
 * @brief Add a capability to the provisioning registry.
 *
 * The registry sets the app info when provisioning initializes: one entry per label, carrying
 * every capability added under it, in add order. Call this before provisioning starts.
 *
 * @note The capability must have static storage duration: the registry keeps the pointer.
 *       Adding the same pointer again overrides its entry rather than adding a second one.
 * @note Not thread safe against another registry call; see ::prov_add_endpoint.
 *
 * @param[in] capability The capability to add.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t prov_add_capability(const prov_capability_t *capability);

/**
 * @brief Remove a capability from the provisioning registry.
 *
 * Removing the last endpoint and capability also drops the registry's event registrations.
 * A capability that is not in the registry is a success; see ::prov_del_endpoint.
 *
 * @note Not thread safe against another registry call; see ::prov_add_endpoint.
 *
 * @param[in] capability The capability to remove. Matched by pointer.
 *
 * @return OSAL_ERR_OK on success, or if the capability is not in the registry.
 * @return OSAL_ERR_INVALID_ARG if `capability` is NULL.
 */
osal_err_t prov_del_capability(const prov_capability_t *capability);

#ifdef __cplusplus
}
#endif

#endif /* __PROV_H__ */
