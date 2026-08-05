/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_standard_devices.c
 * @brief Standard devices for RainMaker Neo.
 */

/* Includes *******************************************************/

/* Standard includes */
#include <stddef.h>

/* Platform includes */
#include "osal_log.h"

/* RMNG includes */
#include "esp_rmaker_error_types.h"
#include "node_internal.h"
#include "esp_rmaker_standard_types.h"
#include "esp_rmaker_standard_params.h"

/* Constants ******************************************************/

static const char *TAG = "rmng_dm_std_devices";

/* Private function declarations ******************************************************/

/**
 * @brief Make a standard device
 *
 * @param[in] dev_id The unique device id
 * @param[in] type The type of the device
 * @param[in] priv_data (Optional) Private data associated with the device. This should stay
 * allocated throughout the lifetime of the device
 */
static esp_rmaker_device_t *__esp_rmaker_standard_device_create(const char *dev_id, const char *type, void *priv_data);

/* Private function definitions ******************************************************/

/**
 * @brief Make a standard device
 *
 * @param[in] dev_id The unique device id
 * @param[in] type The type of the device
 * @param[in] priv_data (Optional) Private data associated with the device. This should stay
 * allocated throughout the lifetime of the device
 */
static esp_rmaker_device_t *__esp_rmaker_standard_device_create(const char *dev_id, const char *type, void *priv_data)
{
    esp_rmaker_device_t *device = NULL;
    esp_rmaker_param_t *name_param = NULL;
    device = esp_rmaker_device_create(dev_id, type, priv_data);
    if (!device) {
        OSAL_LOGE(TAG, "Failed to create device: %s - %s", dev_id, type);
        goto __esp_rmaker_standard_device_create_fail;
    }
    name_param = esp_rmaker_name_param_create(ESP_RMAKER_DEF_NAME_PARAM_ID, dev_id);
    if (!name_param) {
        OSAL_LOGE(TAG, "Failed to create name parameter for device: %s - %s", dev_id, type);
        goto __esp_rmaker_standard_device_create_fail;
    }
    if (esp_rmaker_device_add_param(device, name_param) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add name parameter to device: %s - %s", dev_id, type);
        esp_rmaker_param_delete(name_param);
        goto __esp_rmaker_standard_device_create_fail;
    }
    return device;

__esp_rmaker_standard_device_create_fail:
    if (device) {
        esp_rmaker_device_delete(device);
    }
    return NULL;
}

/* Public function declarations *******************************************************/

esp_rmaker_device_t *esp_rmaker_switch_device_create(const char *dev_id,
        void *priv_data, bool power)
{
    esp_rmaker_device_t *device = __esp_rmaker_standard_device_create(dev_id, ESP_RMAKER_DEVICE_SWITCH, priv_data);
    if (!device) {
        OSAL_LOGE(TAG, "Failed to create switch device");
        return NULL;
    }

    esp_rmaker_param_t *primary = esp_rmaker_power_param_create(ESP_RMAKER_DEF_POWER_ID, power);
    if (!primary) {
        OSAL_LOGE(TAG, "Failed to create power parameter for switch device: %s", dev_id);
        goto esp_rmaker_switch_device_create_fail;
    }
    if (esp_rmaker_device_add_param(device, primary) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add power parameter to switch device: %s", dev_id);
        esp_rmaker_param_delete(primary);
        goto esp_rmaker_switch_device_create_fail;
    }
    esp_rmaker_device_assign_primary_param(device, primary);
    return device;

esp_rmaker_switch_device_create_fail:
    if (device) {
        esp_rmaker_device_delete(device);
    }
    return NULL;
}

esp_rmaker_device_t *esp_rmaker_lightbulb_device_create(const char *dev_id,
        void *priv_data, bool power)
{
    esp_rmaker_device_t *device = __esp_rmaker_standard_device_create(dev_id, ESP_RMAKER_DEVICE_LIGHTBULB, priv_data);
    if (!device) {
        OSAL_LOGE(TAG, "Failed to create lightbulb device");
        return NULL;
    }

    esp_rmaker_param_t *primary = esp_rmaker_power_param_create(ESP_RMAKER_DEF_POWER_ID, power);
    if (!primary) {
        OSAL_LOGE(TAG, "Failed to create power parameter for lightbulb device: %s", dev_id);
        goto esp_rmaker_lightbulb_device_create_fail;
    }
    if (esp_rmaker_device_add_param(device, primary) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add power parameter to lightbulb device: %s", dev_id);
        esp_rmaker_param_delete(primary);
        goto esp_rmaker_lightbulb_device_create_fail;
    }
    esp_rmaker_device_assign_primary_param(device, primary);
    return device;

esp_rmaker_lightbulb_device_create_fail:
    if (device) {
        esp_rmaker_device_delete(device);
    }
    return NULL;
}

esp_rmaker_device_t *esp_rmaker_fan_device_create(const char *dev_id,
        void *priv_data, bool power)
{
    esp_rmaker_device_t *device = __esp_rmaker_standard_device_create(dev_id, ESP_RMAKER_DEVICE_FAN, priv_data);
    if (device) {
        esp_rmaker_param_t *primary = esp_rmaker_power_param_create(ESP_RMAKER_DEF_POWER_ID, power);
        if (!primary) {
            OSAL_LOGE(TAG, "Failed to create power parameter for fan device: %s", dev_id);
            goto esp_rmaker_fan_device_create_fail;
        }
        if (esp_rmaker_device_add_param(device, primary) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to add power parameter to fan device: %s", dev_id);
            esp_rmaker_param_delete(primary);
            goto esp_rmaker_fan_device_create_fail;
        }
        esp_rmaker_device_assign_primary_param(device, primary);
    }
    return device;

esp_rmaker_fan_device_create_fail:
    if (device) {
        esp_rmaker_device_delete(device);
    }
    return NULL;
}

esp_rmaker_device_t *esp_rmaker_temp_sensor_device_create(const char *dev_id,
        void *priv_data, float temperature)
{
    esp_rmaker_device_t *device = __esp_rmaker_standard_device_create(dev_id, ESP_RMAKER_DEVICE_TEMP_SENSOR, priv_data);
    if (!device) {
        OSAL_LOGE(TAG, "Failed to create temperature sensor device");
        return NULL;
    }

    esp_rmaker_param_t *primary = esp_rmaker_temperature_param_create(ESP_RMAKER_DEF_TEMPERATURE_ID, temperature);
    if (!primary) {
        OSAL_LOGE(TAG, "Failed to create temperature parameter for temperature sensor device: %s", dev_id);
        goto esp_rmaker_temp_sensor_device_create_fail;
    }
    if (esp_rmaker_device_add_param(device, primary) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add temperature parameter to temperature sensor device: %s", dev_id);
        esp_rmaker_param_delete(primary);
        goto esp_rmaker_temp_sensor_device_create_fail;
    }
    esp_rmaker_device_assign_primary_param(device, primary);
    return device;

esp_rmaker_temp_sensor_device_create_fail:
    if (device) {
        esp_rmaker_device_delete(device);
    }
    return NULL;
}
