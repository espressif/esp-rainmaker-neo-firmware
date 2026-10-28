/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_server_config.h
 * @brief The deployment's server config, fetched from the cloud.
 *
 * With CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG, the node requests the "getServerConfig" cloud
 * event after connecting. The request names only the keys that Kconfig selects, and the
 * cloud returns only their values. Firmware uses them to reach deployment resources beyond MQTT
 * (for example, a camera exchanging its certificate for KVS credentials through the AWS
 * IoT Credential Provider).
 */

#ifndef __ESP_RMAKER_SERVER_CONFIG_H__
#define __ESP_RMAKER_SERVER_CONFIG_H__

/* Includes **************************************************************/

/* Standard includes */
#include <stdint.h>

/* Error types */
#include "esp_rmaker_error_types.h"

/* Configuration includes */
#include "sdkconfig.h"

#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG

/* Keys **************************************************************/

/**
 * @brief The server config values that the node requests.
 * @note Only the keys that Kconfig selects exist, for example with CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_VIDEOSTREAM_SUPPORT
 *       or CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_FILE_SUPPORT.
 */
typedef enum {
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_STACK_REGION
    /** AWS region of the deployment, e.g. "us-east-1". */
    ESP_RMAKER_SERVER_CONFIG_STACK_REGION,
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_CRED_ENDPOINT
    /** AWS IoT Credential Provider endpoint, "<prefix>.credentials.iot.<region>.amazonaws.com". */
    ESP_RMAKER_SERVER_CONFIG_CREDENTIAL_PROVIDER_ENDPOINT,
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_VIDEO_ROLE_ALIAS
    /** IoT role alias(es) for node video (KVS) access; comma-separated if several. */
    ESP_RMAKER_SERVER_CONFIG_NODE_VIDEO_ROLE_ALIASES,
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILE_ROLE_ALIAS
    /** IoT role alias(es) for node file (S3) access; comma-separated if several. */
    ESP_RMAKER_SERVER_CONFIG_NODE_FILE_ROLE_ALIASES,
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILES_BUCKET_NAME
    /** S3 bucket for node file storage, in the deployment's region. */
    ESP_RMAKER_SERVER_CONFIG_FILES_BUCKET_NAME,
#endif
    /** Number of keys. Not a key. */
    ESP_RMAKER_SERVER_CONFIG_KEY_MAX,
} esp_rmaker_server_config_key_t;

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get a value from the deployment's server config.
 *
 * The node requests the server config with the other cloud-setup events, after each connection until it has been
 * received; a lost response waits for the next connection. This call waits up to @p timeout_ms for it. Once received,
 * values stay cached for the life of the SDK (across reconnects) and the call returns immediately.
 *
 * @note Use @p timeout_ms 0 on the SDK work queue (param write callbacks, RainMaker event handlers): the response is
 *       handled there, so a wait could never succeed. Such a call returns ESP_RMAKER_INVALID_STATE.
 *
 * @param[in] key The value to get.
 * @param[out] p_value Heap-allocated copy of the value; free() it. Set to NULL on any error.
 * @param[in] timeout_ms How long to wait for the response (0 to not wait).
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_TIMEOUT if the server config has not been received in time, or the SDK stopped while waiting.
 * @return ESP_RMAKER_NOT_FOUND if the response did not carry the value, or it did not fit.
 * @return ESP_RMAKER_INVALID_STATE before the SDK is initialised, or for a wait on the SDK work queue.
 * @return ESP_RMAKER_INVALID_ARG / ESP_RMAKER_NO_MEM on bad arguments / allocation failure.
 */
esp_rmaker_error_t esp_rmaker_server_config_get(esp_rmaker_server_config_key_t key, char **p_value,
        uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG */

#endif /* __ESP_RMAKER_SERVER_CONFIG_H__ */
