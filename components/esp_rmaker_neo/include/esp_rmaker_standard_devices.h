/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_standard_devices.h
 * @brief Standard devices for RainMaker Neo.
 */

#ifndef __ESP_RMAKER_STANDARD_DEVICES_H__
#define __ESP_RMAKER_STANDARD_DEVICES_H__

#include <stdbool.h>
#include "esp_rmaker_data_model.h"

/* Public function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a standard Switch device
 *
 * This creates a Switch device with the mandatory parameters and also assigns
 * the primary parameter. The default parameter names will be used.
 * Refer @ref esp_rmaker_standard_params.h for default names.
 *
 * @param[in] dev_id The unique device id
 * @param[in] priv_data (Optional) Private data associated with the device. This should stay
 * allocated throughout the lifetime of the device
 * @param[in] power Default value of the mandatory parameter "power"
 *
 * @return Device handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_device_t *esp_rmaker_switch_device_create(const char *dev_id,
        void *priv_data, bool power);

/**
 * @brief Create a standard Lightbulb device
 *
 * This creates a Lightbulb device with the mandatory parameters and also assigns
 * the primary parameter. The default parameter names will be used.
 * Refer @ref esp_rmaker_standard_params.h for default names.
 *
 * @param[in] dev_id The unique device id
 * @param[in] priv_data (Optional) Private data associated with the device. This should stay
 * allocated throughout the lifetime of the device
 * @param[in] power Default value of the mandatory parameter "power"
 *
 * @return Device handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_device_t *esp_rmaker_lightbulb_device_create(const char *dev_id,
        void *priv_data, bool power);

/**
 * @brief Create a standard Fan device
 *
 * This creates a Fan device with the mandatory parameters and also assigns
 * the primary parameter. The default parameter names will be used.
 * Refer @ref esp_rmaker_standard_params.h for default names.
 *
 * @param[in] dev_id The unique device id
 * @param[in] priv_data (Optional) Private data associated with the device. This should stay
 * allocated throughout the lifetime of the device
 * @param[in] power Default value of the mandatory parameter "power"
 *
 * @return Device handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_device_t *esp_rmaker_fan_device_create(const char *dev_id,
        void *priv_data, bool power);

/**
 * @brief Create a standard Temperature Sensor device
 *
 * This creates a Temperature Sensor device with the mandatory parameters and also assigns
 * the primary parameter. The default parameter names will be used.
 * Refer @ref esp_rmaker_standard_params.h for default names.
 *
 * @param[in] dev_id The unique device id
 * @param[in] priv_data (Optional) Private data associated with the device. This should stay
 * allocated throughout the lifetime of the device
 * @param[in] temperature Default value of the mandatory parameter "temperature"
 *
 * @return Device handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_device_t *esp_rmaker_temp_sensor_device_create(const char *dev_id,
        void *priv_data, float temperature);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_STANDARD_DEVICES_H__ */
