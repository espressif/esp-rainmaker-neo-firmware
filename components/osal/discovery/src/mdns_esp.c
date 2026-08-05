/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mdns_esp.c
 * @brief mDNS discovery implementation for ESP-IDF.
 */

/* Includes *******************************************************/

/* Declaration includes. */
#include "osal_discovery.h"

/* ESP-IDF includes. */
#include "esp_err.h"
#include "mdns.h"
#include "esp_log.h"

/* Standard includes. */
#include <stddef.h>

/* Constants *******************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "osal_disc_mdns";

/* Variables *******************************************************/

/**
 * @brief The port for the mDNS service (-1 if not set)
 */
static int s_service_port = -1;

/* Private function declarations *******************************************************/

/**
 * @brief Initialize the mDNS service
 * @param[in] service_type The service type
 * @param[in] service_protocol The service protocol
 * @param[in] instance_name The mDNS instance name
 * @param[in] txt_items The text items
 * @return The error code
 */
static osal_err_t __init_service(const char *service_type, const char *service_protocol, const char *instance_name, const osal_discovery_txt_items_t *txt_items);

/* Private function definitions *******************************************************/

static osal_err_t __init_service(const char *service_type, const char *service_protocol, const char *instance_name, const osal_discovery_txt_items_t *txt_items)
{
    if (service_type == NULL || service_protocol == NULL || instance_name == NULL || txt_items == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    mdns_service_instance_name_set(service_type, service_protocol, instance_name);
    for (size_t i = 0; i < txt_items->count; i++) {
        osal_discovery_txt_item_t *txt_item = &txt_items->list[i];
        if (txt_item == NULL || txt_item->var == NULL || txt_item->val == NULL) {
            continue;
        }
        mdns_service_txt_item_set(service_type, service_protocol, txt_item->var, txt_item->val);
    }
    return OSAL_ERR_OK;
}

/* Public function definitions *******************************************************/

osal_err_t osal_discovery_init(const osal_discovery_service_config_t *service_config)
{
    if (!service_config || !service_config->name || service_config->port <= 0) {
        ESP_LOGE(TAG, "Invalid discovery init configuration");
        return OSAL_ERR_INVALID_ARG;
    }

    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(err));
        return OSAL_ERR_FAIL;
    }
    mdns_hostname_set(service_config->name);
    ESP_LOGI(TAG, "mDNS initialized with hostname: %s", service_config->name);
    s_service_port = service_config->port;
    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_deinit(void)
{
    mdns_free();
    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_add_service(const char *service_type, const char *service_protocol, const char *instance_name, const osal_discovery_txt_items_t *txt_items)
{
    if (service_type == NULL || service_protocol == NULL || instance_name == NULL || txt_items == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    if (s_service_port <= 0) {
        return OSAL_ERR_INVALID_STATE;
    }

    /* Add the service to mDNS */
    esp_err_t err = mdns_service_add(NULL, service_type, service_protocol, s_service_port, NULL, 0);
    if (err != ESP_OK) {
        return OSAL_ERR_FAIL;
    }

    /* Initialize the service */
    return __init_service(service_type, service_protocol, instance_name, txt_items);
}

osal_err_t osal_discovery_remove_service(const char *service_type, const char *service_protocol)
{
    esp_err_t err = mdns_service_remove(service_type, service_protocol);
    if (err == ESP_ERR_INVALID_ARG) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (err == ESP_ERR_NO_MEM) {
        return OSAL_ERR_NO_MEM;
    }

    /* All other errors are considered as success */
    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_on_start(const osal_discovery_service_config_t *service_config, const osal_discovery_transport_config_t *transport_config)
{
    (void) transport_config;
    /* Initialize the mDNS service for local control */
    return __init_service(service_config->type, service_config->protocol, service_config->name, &service_config->txt_items);
}

osal_err_t osal_discovery_on_stop(void)
{
    /* Nothing to do for mDNS */
    return OSAL_ERR_OK;
}
