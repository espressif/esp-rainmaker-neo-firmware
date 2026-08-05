/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_svc_system.c
 * @brief System service creation implementation
 */

/* Includes *******************************************************/

/* Declarations */
#include "esp_rmaker_standard_services.h"
#include "services/standard_creation.h"

/* Standard includes */
#include <string.h>

/* Constants */
#include "esp_rmaker_standard_types.h"
#include "constants/services.h"

/* System control */
#include "esp_rmaker_system_ctrl.h"

/* Value helpers */
#include "esp_rmaker_val.h"

/* Platform common includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* RMNG includes */
#include "node_internal.h"

/* Constants *******************************************************/

/**
 * @brief Tag for the system service
 */
static const char *TAG = "rmng_dm_svc_system";

/* Private variables *******************************************************/

/**
 * @brief The system service
 */
static esp_rmaker_device_t *__system_service = NULL;

/**
 * @brief Heap copy of the system service configuration (owned here)
 */
static esp_rmaker_system_serv_config_t *__system_config = NULL;

/* Private function declarations *******************************************************/

/**
 * @brief System service callback
 * @param[in] device Device handle
 * @param[in] param Parameter handle
 * @param[in] val Parameter value
 * @param[in] priv_data Private data
 * @param[in] ctx Write context
 * @return ESP_RMAKER_OK on success
 * @return ESP_RMAKER_FAIL on failure
 */
static esp_rmaker_error_t __system_service_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx);

/**
 * @brief Create a system service
 * @param[in] config System service configuration
 * @return The system service handle
 */
static esp_rmaker_device_t *__system_service_create(const esp_rmaker_system_serv_config_t *config);

/* Private function definitions *******************************************************/

static esp_rmaker_error_t __system_service_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    esp_rmaker_system_serv_config_t *config = (esp_rmaker_system_serv_config_t *)priv_data;
    if (!config) {
        return ESP_RMAKER_FAIL;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    const char *type = esp_rmaker_param_get_type(param);
    if (strcmp(type, ESP_RMAKER_PARAM_REBOOT) == 0) {
        if (val.val.b) {
            err = esp_rmaker_system_ctrl_reboot(config->reboot_seconds);
        }
    } else if (strcmp(type, ESP_RMAKER_PARAM_NETWORK_RESET) == 0) {
        if (val.val.b) {
            err = esp_rmaker_system_ctrl_network_reset(config->reset_seconds, config->reset_reboot_seconds, config->network_reset_fn);
        }
    } else if (strcmp(type, ESP_RMAKER_PARAM_FACTORY_RESET) == 0) {
        if (val.val.b) {
            err = esp_rmaker_system_ctrl_factory_reset(config->reset_seconds, config->reset_reboot_seconds, config->network_reset_fn);
        }
    } else {
        return ESP_RMAKER_FAIL;
    }

    if (err == ESP_RMAKER_OK) {
        esp_rmaker_param_update_and_report(param, val);
    }
    return err;
}

static esp_rmaker_device_t *__system_service_create(const esp_rmaker_system_serv_config_t *config)
{
    esp_rmaker_device_t *service = esp_rmaker_service_create(RMAKER_SERVICES_SYSTEM_SERVICE_ID, ESP_RMAKER_SERVICE_SYSTEM, (void *)config);
    if (service) {
        uint8_t properties = PROP_FLAG_READ | PROP_FLAG_WRITE;
        if (config->flags & SYSTEM_SERV_FLAG_REBOOT) {
            esp_rmaker_device_add_param(service, esp_rmaker_param_create(RMAKER_SERVICES_SYSTEM_REBOOT_PARAM_ID, ESP_RMAKER_PARAM_REBOOT, esp_rmaker_bool(false), properties));
        }
        if (config->flags & SYSTEM_SERV_FLAG_NETWORK_RESET) {
            esp_rmaker_device_add_param(service, esp_rmaker_param_create(RMAKER_SERVICES_SYSTEM_NETWORK_RESET_PARAM_ID, ESP_RMAKER_PARAM_NETWORK_RESET, esp_rmaker_bool(false), properties));
        }
        if (config->flags & SYSTEM_SERV_FLAG_FACTORY_RESET) {
            esp_rmaker_device_add_param(service, esp_rmaker_param_create(RMAKER_SERVICES_SYSTEM_FACTORY_RESET_PARAM_ID, ESP_RMAKER_PARAM_FACTORY_RESET, esp_rmaker_bool(false), properties));
        }
    }
    return service;
}

/* Public function definitions *******************************************************/

esp_rmaker_error_t esp_rmaker_system_service_add_to_node(const esp_rmaker_system_serv_config_t *config)
{
    if (!config) {
        OSAL_LOGE(TAG, "System service config is NULL");
        return ESP_RMAKER_INVALID_ARG;
    }
    if ((config->flags & SYSTEM_SERV_FLAGS_ALL) == 0) {
        OSAL_LOGE(TAG, "At least one flag should be set for system service");
        return ESP_RMAKER_INVALID_ARG;
    }
    if ((config->flags & (SYSTEM_SERV_FLAG_NETWORK_RESET | SYSTEM_SERV_FLAG_FACTORY_RESET)) && !config->network_reset_fn) {
        OSAL_LOGE(TAG, "network_reset_fn must be set when network-reset or factory-reset flag is set");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Keep a private copy of the config; it backs the write callback for the
     * service lifetime and is owned (freed) by this module. */
    esp_rmaker_system_serv_config_t *priv_config = OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_system_serv_config_t));
    if (!priv_config) {
        OSAL_LOGE(TAG, "Failed to allocate system service config");
        return ESP_RMAKER_NO_MEM;
    }
    *priv_config = *config;

    esp_rmaker_device_t *service = __system_service_create(priv_config);
    if (!service) {
        OSAL_LOGE(TAG, "Failed to create system service");
        free(priv_config);
        return ESP_RMAKER_NO_MEM;
    }

    __system_service = service;
    __system_config = priv_config;
    esp_rmaker_device_add_cb(service, __system_service_cb, NULL);

    esp_rmaker_error_t err = esp_rmaker_node_add_device(esp_rmaker_get_node(), service);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add system service to node");
        esp_rmaker_device_delete(service);
        free(priv_config);
        __system_service = NULL;
        __system_config = NULL;
        return err;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_system_service_remove_from_node(void)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    if (__system_service) {
        err = esp_rmaker_node_remove_device(esp_rmaker_get_node(), __system_service);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to remove system service from node");
            return err;
        }
        esp_rmaker_device_delete(__system_service);
        __system_service = NULL;
    }
    if (__system_config) {
        free(__system_config);
        __system_config = NULL;
    }
    return ESP_RMAKER_OK;
}
