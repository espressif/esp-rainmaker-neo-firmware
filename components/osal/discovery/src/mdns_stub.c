/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mdns_stub.c
 * @brief Stub mDNS discovery implementation when no mDNS library is available.
 */

/* Includes *******************************************************/

/* Declaration includes. */
#include "osal_discovery.h"

/* Platform common includes. */
#include "osal_log.h"

/* Constants *******************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "osal_disc_stub";

/* Public function definitions *******************************************************/

osal_err_t osal_discovery_init(const osal_discovery_service_config_t *service_config)
{
    (void)service_config; // Not used

    OSAL_LOGW(TAG, "No mDNS library available - discovery will not work");
    return OSAL_ERR_FAIL;
}

osal_err_t osal_discovery_deinit(void)
{
    // Nothing to clean up
    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_add_service(const char *service_type, const char *service_protocol, const char *instance_name, const osal_discovery_txt_items_t *txt_items)
{
    (void)service_type; // Not used
    (void)service_protocol; // Not used
    (void)instance_name; // Not used
    (void)txt_items; // Not used
    OSAL_LOGW(TAG, "No mDNS library available - cannot add service");
    /* The service was not advertised, so this must not report success. NOT_SUPPORTED rather
     * than FAIL, because the two mean different things to a caller: FAIL is "advertising was
     * attempted and broke", which is worth failing over, while NOT_SUPPORTED is "this build has
     * no mDNS backend at all" -- a standing property of the platform, not a fault. A caller
     * whose service stays reachable by IP can reasonably carry on in the latter case. */
    return OSAL_ERR_NOT_SUPPORTED;
}

osal_err_t osal_discovery_remove_service(const char *service_type, const char *service_protocol)
{
    (void)service_type; // Not used
    (void)service_protocol; // Not used
    // Nothing to do
    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_on_start(const osal_discovery_service_config_t *service_config, const osal_discovery_transport_config_t *transport_config)
{
    (void)service_config; // Not used

    OSAL_LOGW(TAG, "No mDNS library available - cannot register service");
    return OSAL_ERR_FAIL;
}

osal_err_t osal_discovery_on_stop(void)
{
    // Nothing to do
    return OSAL_ERR_OK;
}
