/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file svc_system.c
 * @brief System service
 */

/* Includes *******************************************************/

/* Declarations */
#include "esp_rmaker_standard_services.h"
#include "services/standard_creation.h"

/* Platform common includes */
#include "osal_log.h"

/* Variables *******************************************************/

/**
 * @brief Tag for the system service
 */
static const char *TAG = "rmng_svc_system";

/* Public functions *******************************************************/

esp_rmaker_error_t esp_rmaker_system_service_enable(const esp_rmaker_system_serv_config_t *config)
{
    if (!config) {
        OSAL_LOGE(TAG, "System service config is NULL");
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = esp_rmaker_system_service_add_to_node(config);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to create system service");
        return err;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_system_service_disable(void)
{
    esp_rmaker_error_t err = esp_rmaker_system_service_remove_from_node();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to remove system service from node");
        return err;
    }
    return ESP_RMAKER_OK;
}
