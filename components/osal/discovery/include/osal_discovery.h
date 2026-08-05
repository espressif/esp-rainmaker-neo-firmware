/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_discovery.h
 * @brief Service discovery (mDNS on Wi-Fi / SRP on Thread; Avahi or mDNSResponder on POSIX).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "osal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A single key/value pair advertised in a service's TXT record.
 */
typedef struct {
    const char *var; /**< TXT record key. */
    const char *val; /**< TXT record value. */
} osal_discovery_txt_item_t;

/**
 * @brief The set of TXT records advertised alongside a service.
 */
typedef struct {
    osal_discovery_txt_item_t *list; /**< Array of @p count items. Not retained by the backend. */
    size_t count;                    /**< Number of items in @p list. */
} osal_discovery_txt_items_t;

/**
 * @brief The service to advertise.
 */
typedef struct {
    const char *name;     /**< Hostname / service instance name. */
    const char *type;     /**< Service type, e.g. "_esp_local_ctrl". */
    const char *protocol; /**< Transport protocol, e.g. "_tcp". */
    int port;             /**< TCP/UDP port the service listens on. Must be > 0. */
    osal_discovery_txt_items_t txt_items; /**< TXT records to advertise. */
} osal_discovery_service_config_t;

/**
 * @brief Transport the advertised service runs over.
 */
typedef enum {
    OSAL_DISCOVERY_TRANSPORT_HTTPD = 0, /**< HTTP server transport. */
} osal_discovery_transport_type_t;

/**
 * @brief HTTP server transport parameters.
 */
typedef struct {
    int port;          /**< HTTP port. */
    int ctrl_port;     /**< Control socket port. */
    size_t stack_size; /**< Server task stack size. */
} osal_discovery_httpd_transport_t;

/**
 * @brief Transport configuration, tagged by ::osal_discovery_transport_type_t.
 */
typedef struct {
    osal_discovery_transport_type_t type; /**< Selects the active union member. */
    union {
        osal_discovery_httpd_transport_t httpd; /**< Valid when @p type is OSAL_DISCOVERY_TRANSPORT_HTTPD. */
    };
} osal_discovery_transport_config_t;

/**
 * @brief Bring up the discovery backend and set the advertised hostname.
 *
 * Must be called before any other entry point here. Backends that need a running daemon
 * or stack start it here.
 *
 * @param[in] service_config Service description. Only @p name (used as the hostname) and
 *                           @p port are consumed at init; the rest is used by
 *                           ::osal_discovery_on_start. Not retained.
 *
 * @return OSAL_ERR_OK on success.
 * @return OSAL_ERR_INVALID_ARG if @p service_config, its @p name, or its @p port is missing
 *         or invalid.
 * @return OSAL_ERR_FAIL if the backend could not be started.
 */
osal_err_t osal_discovery_init(const osal_discovery_service_config_t *service_config);

/**
 * @brief Tear down the discovery backend and release its resources.
 *
 * Removes any still-advertised services along with the backend.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_discovery_deinit(void);

/**
 * @brief Advertise a service on the port given to ::osal_discovery_init.
 *
 * @param[in] service_type     Service type, e.g. "_esp_local_ctrl".
 * @param[in] service_protocol Transport protocol, e.g. "_tcp".
 * @param[in] instance_name    Service instance name shown to browsers.
 * @param[in] txt_items        TXT records to advertise. Copied by the backend; may be empty
 *                             but must not be NULL.
 *
 * @return OSAL_ERR_OK on success.
 * @return OSAL_ERR_INVALID_ARG if any argument is NULL.
 * @return OSAL_ERR_INVALID_STATE if ::osal_discovery_init has not run.
 * @return OSAL_ERR_NO_MEM or OSAL_ERR_FAIL if the backend rejected the service.
 */
osal_err_t osal_discovery_add_service(const char *service_type, const char *service_protocol,
                                      const char *instance_name, const osal_discovery_txt_items_t *txt_items);

/**
 * @brief Stop advertising a service previously added with ::osal_discovery_add_service.
 *
 * Removing a service that was never advertised is not an error.
 *
 * @param[in] service_type     Service type used when adding.
 * @param[in] service_protocol Transport protocol used when adding.
 *
 * @return OSAL_ERR_OK on success, or if the service was not advertised.
 * @return OSAL_ERR_INVALID_ARG if the backend rejected the arguments.
 * @return OSAL_ERR_NO_MEM if the backend could not allocate while removing.
 */
osal_err_t osal_discovery_remove_service(const char *service_type, const char *service_protocol);

/**
 * @brief Notify the backend that the advertised service has started.
 *
 * Publishes (or republishes) the service description and its TXT records. The Thread/SRP
 * backend additionally registers the local-control endpoints it derives from
 * @p transport_config.
 *
 * @param[in] service_config   Service to advertise. Must carry @p name, @p type and
 *                             @p protocol.
 * @param[in] transport_config Transport the service runs over. Required by the Thread/SRP
 *                             backend; ignored by the mDNS backends.
 *
 * @return OSAL_ERR_OK on success.
 * @return OSAL_ERR_INVALID_ARG if a required field is missing, or the transport type is
 *         not supported.
 * @return OSAL_ERR_INVALID_STATE if ::osal_discovery_init has not run.
 * @return OSAL_ERR_FAIL if the backend rejected the advertisement.
 */
osal_err_t osal_discovery_on_start(const osal_discovery_service_config_t *service_config,
                                   const osal_discovery_transport_config_t *transport_config);

/**
 * @brief Notify the backend that the advertised service has stopped.
 *
 * A lifecycle notification, not a teardown call: releasing backend resources is
 * ::osal_discovery_deinit's job, so a backend with nothing to unwind at stop legitimately
 * returns OSAL_ERR_OK without doing anything. Only the Thread/SRP backend acts on it today.
 * Callers must still call ::osal_discovery_deinit to actually release resources.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_discovery_on_stop(void);

#ifdef __cplusplus
}
#endif
