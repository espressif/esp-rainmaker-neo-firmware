/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file prov_helpers_esp.c
 * @brief ESP implementation of the provisioning backend.
 */

/* Includes *******************************************************/

/* Public function declaration include. */
#include "prov_helpers_internal.h"

/* ESP-IDF includes. */
#include "esp_err.h"
#include "network_provisioning/manager.h"

/* Private function declarations *******************************************************/

static esp_err_t __network_prov_endpoint_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf, ssize_t *outlen, void *priv_data);

/* Private function definitions *******************************************************/

static esp_err_t __network_prov_endpoint_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    /* Get the actual endpoint */
    const prov_endpoint_t *endpoint = priv_data;
    if (endpoint == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    osal_err_t err = endpoint->handler(session_id, inbuf, inlen, outbuf, outlen, endpoint->priv_data);
    switch (err) {
    case OSAL_ERR_OK:
        return ESP_OK;
    case OSAL_ERR_INVALID_ARG:
        return ESP_ERR_INVALID_ARG;
    case OSAL_ERR_NO_MEM:
        return ESP_ERR_NO_MEM;
    default:
        return ESP_FAIL;
    }
}

/* Public function definitions *******************************************************/

osal_err_t __prov_event_loop_init(void)
{
    __event_loop_info.event_base = NETWORK_PROV_EVENT;
    __event_loop_info.event_ids.prov_init = NETWORK_PROV_INIT;
    __event_loop_info.event_ids.prov_start = NETWORK_PROV_START;
    __event_loop_info.event_ids.prov_end = NETWORK_PROV_END;
    return OSAL_ERR_OK;
}

osal_err_t __prov_backend_endpoint_create(const prov_endpoint_t *endpoint)
{
    network_prov_mgr_set_app_info(
        endpoint->app_info.label,
        endpoint->app_info.version,
        endpoint->app_info.capabilities,
        endpoint->app_info.total_capabilities
    );
    if (network_prov_mgr_endpoint_create(endpoint->name) != ESP_OK) {
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}

osal_err_t __prov_backend_endpoint_register(const prov_endpoint_t *endpoint)
{
    if (network_prov_mgr_endpoint_register(endpoint->name, __network_prov_endpoint_handler, (void *)endpoint) != ESP_OK) {
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}
