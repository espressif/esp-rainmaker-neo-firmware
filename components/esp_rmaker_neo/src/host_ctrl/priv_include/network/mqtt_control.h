/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mqtt_control.h
 * @brief MQTT control functions.
 */

#ifndef __NETWORK_MQTT_CONTROL_H__
#define __NETWORK_MQTT_CONTROL_H__

/* Includes *******************************************************/

/* Error types includes */
#include "esp_rmaker_error_types.h"

/* Public function declarations ****************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Force all network operations (connect, send, recv) to fail.
 * Done by caching the original network operations implementation and installing a stub network operations implementation that always returns failure.
 * @return ESP_RMAKER_OK on success, error on failure.
 */
esp_rmaker_error_t network_mqtt_control_force_network_failure(void);

/**
 * @brief Restore default network operations settings.
 * Done by restoring the original network operations implementation.
 *
 * @return ESP_RMAKER_OK on success, error on failure.
 */
esp_rmaker_error_t network_mqtt_control_restore_network_default(void);

/**
 * @brief Force all MQTT operations (publish, subscribe, unsubscribe) to fail.
 * Done by overriding the publish, subscribe, and unsubscribe functions to return failure.
 *
 * @return ESP_RMAKER_OK on success, error on failure.
 */
esp_rmaker_error_t network_mqtt_control_force_operations_failure(void);

/**
 * @brief Restore default MQTT operations (publish, subscribe, unsubscribe) settings.
 * Done by restoring the original publish, subscribe, and unsubscribe functions.
 *
 * @return ESP_RMAKER_OK on success, error on failure.
 */
esp_rmaker_error_t network_mqtt_control_restore_operations_default(void);

/**
 * @brief Trigger an explicit MQTT disconnect via ``esp_rmaker_mqtt_impl.disconnect``.
 *
 * Unlike ``force_network_failure`` (which only mocks the socket layer and
 * relies on a keepalive timeout), this issues a clean disconnect immediately.
 *
 * @return ESP_RMAKER_OK on success, error on failure.
 */
esp_rmaker_error_t network_mqtt_control_disconnect(void);

/**
 * @brief Trigger an explicit MQTT (re)connect via ``esp_rmaker_mqtt_impl.connect``.
 *
 * @return ESP_RMAKER_OK on success, error on failure.
 */
esp_rmaker_error_t network_mqtt_control_connect(void);

#ifdef __cplusplus
}
#endif

#endif /* __NETWORK_MQTT_CONTROL_H__ */
