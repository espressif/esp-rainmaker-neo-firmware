/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_local_config_targets.h
 * @brief Used to specify the start and end addresses of the local configuration
 * @note These are from certificate files included in the main project using `target_add_binary_data`:
 * - `host.crt`: Host root certificate
 * - `client.crt`: Client certificate
 * - `client.key`: Client private key
 */

#ifndef __ESP_RMAKER_LOCAL_CONFIG_TARGETS_H__
#define __ESP_RMAKER_LOCAL_CONFIG_TARGETS_H__

#include <stdint.h>

/** Bounds of a binary blob embedded in the firmware image */
typedef struct {
    /** First byte of the blob */
    const uint8_t *start;
    /** One past the last byte of the blob */
    const uint8_t *end;
} esp_rmaker_local_config_binary_t;

#ifdef __cplusplus
extern "C" {
#endif

// MQTT broker info
extern const char *esp_rmaker_mqtt_broker_host;
extern const int esp_rmaker_mqtt_broker_port;
extern const char *esp_rmaker_mqtt_broker_username;
extern const char *esp_rmaker_mqtt_broker_password;

// Node thing name
extern const char *esp_rmaker_thing_name;

// Host certificate
extern esp_rmaker_local_config_binary_t esp_rmaker_host_cert;

// Client certificate
extern esp_rmaker_local_config_binary_t esp_rmaker_client_cert;

// Client key
extern esp_rmaker_local_config_binary_t esp_rmaker_client_key;

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_LOCAL_CONFIG_TARGETS_H__ */
