/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_err.h
 * @brief Portable OS error type for ESP-IDF (typedef to esp_err_t).
 */

#pragma once

#include "esp_err.h"
#include "osal_err_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Portable error type. On ESP-IDF this is esp_err_t; on POSIX an int32_t. */
typedef esp_err_t osal_err_t;

#define OSAL_ERR_OK                    ESP_OK
#define OSAL_ERR_FAIL                  ESP_FAIL
#define OSAL_ERR_NO_MEM                ESP_ERR_NO_MEM
#define OSAL_ERR_INVALID_ARG           ESP_ERR_INVALID_ARG
#define OSAL_ERR_INVALID_STATE         ESP_ERR_INVALID_STATE
#define OSAL_ERR_TIMEOUT               ESP_ERR_TIMEOUT
#define OSAL_ERR_NOT_FOUND             ESP_ERR_NOT_FOUND
#define OSAL_ERR_NOT_SUPPORTED         ESP_ERR_NOT_SUPPORTED
#define OSAL_ERR_INVALID_RESPONSE      ESP_ERR_INVALID_RESPONSE

#define OSAL_ERR_HTTP_CONNECTION_FAILED    ((osal_err_t)OSAL_ERR_VAL_HTTP_CONNECTION_FAILED)

#define OSAL_ERR_MQTT_SEND_FAILED          ((osal_err_t)OSAL_ERR_VAL_MQTT_SEND_FAILED)
#define OSAL_ERR_MQTT_RECV_FAILED          ((osal_err_t)OSAL_ERR_VAL_MQTT_RECV_FAILED)
#define OSAL_ERR_MQTT_BAD_RESPONSE         ((osal_err_t)OSAL_ERR_VAL_MQTT_BAD_RESPONSE)
#define OSAL_ERR_MQTT_SERVER_REFUSED       ((osal_err_t)OSAL_ERR_VAL_MQTT_SERVER_REFUSED)
#define OSAL_ERR_MQTT_NO_DATA_AVAILABLE    ((osal_err_t)OSAL_ERR_VAL_MQTT_NO_DATA_AVAILABLE)
#define OSAL_ERR_MQTT_STATE_COLLISION      ((osal_err_t)OSAL_ERR_VAL_MQTT_STATE_COLLISION)
#define OSAL_ERR_MQTT_KEEP_ALIVE_TIMEOUT   ((osal_err_t)OSAL_ERR_VAL_MQTT_KEEP_ALIVE_TIMEOUT)
#define OSAL_ERR_MQTT_NEED_MORE_BYTES      ((osal_err_t)OSAL_ERR_VAL_MQTT_NEED_MORE_BYTES)
#define OSAL_ERR_MQTT_NOT_CONNECTED        ((osal_err_t)OSAL_ERR_VAL_MQTT_NOT_CONNECTED)

#define OSAL_ERR_NVS_NOT_INITIALIZED       ((osal_err_t)OSAL_ERR_VAL_NVS_NOT_INITIALIZED)
#define OSAL_ERR_NVS_PARTITION_NOT_FOUND   ((osal_err_t)OSAL_ERR_VAL_NVS_PARTITION_NOT_FOUND)
#define OSAL_ERR_NVS_NAMESPACE_NOT_FOUND   ((osal_err_t)OSAL_ERR_VAL_NVS_NAMESPACE_NOT_FOUND)
#define OSAL_ERR_NVS_KEY_NOT_FOUND         ((osal_err_t)OSAL_ERR_VAL_NVS_KEY_NOT_FOUND)

#define OSAL_ERR_OTA_INVALID_SIZE          ((osal_err_t)OSAL_ERR_VAL_OTA_INVALID_SIZE)
#define OSAL_ERR_OTA_PARTITION_CONFLICT    ((osal_err_t)OSAL_ERR_VAL_OTA_PARTITION_CONFLICT)
#define OSAL_ERR_OTA_SELECT_INFO_INVALID    ((osal_err_t)OSAL_ERR_VAL_OTA_SELECT_INFO_INVALID)
#define OSAL_ERR_OTA_VALIDATE_FAILED       ((osal_err_t)OSAL_ERR_VAL_OTA_VALIDATE_FAILED)
#define OSAL_ERR_OTA_SMALL_SEC_VER         ((osal_err_t)OSAL_ERR_VAL_OTA_SMALL_SEC_VER)
#define OSAL_ERR_OTA_ROLLBACK_FAILED       ((osal_err_t)OSAL_ERR_VAL_OTA_ROLLBACK_FAILED)
#define OSAL_ERR_OTA_ROLLBACK_INVALID_STATE ((osal_err_t)OSAL_ERR_VAL_OTA_ROLLBACK_INVALID_STATE)

#ifndef ESP_ERR_RMAKER_BASE
#define ESP_ERR_RMAKER_BASE              OSAL_ERR_VAL_RMAKER_BASE
#endif

#define OSAL_ERR_RMAKER_NOT_INITIALIZED        ((osal_err_t)OSAL_ERR_VAL_RMAKER_NOT_INITIALIZED)
#define OSAL_ERR_RMAKER_ALREADY_INITIALIZED    ((osal_err_t)OSAL_ERR_VAL_RMAKER_ALREADY_INITIALIZED)
#define OSAL_ERR_RMAKER_NOT_CONNECTED          ((osal_err_t)OSAL_ERR_VAL_RMAKER_NOT_CONNECTED)
#define OSAL_ERR_RMAKER_ALREADY_CONNECTED      ((osal_err_t)OSAL_ERR_VAL_RMAKER_ALREADY_CONNECTED)
#define OSAL_ERR_RMAKER_NOT_DISCONNECTED       ((osal_err_t)OSAL_ERR_VAL_RMAKER_NOT_DISCONNECTED)
#define OSAL_ERR_RMAKER_ALREADY_EXISTS         ((osal_err_t)OSAL_ERR_VAL_RMAKER_ALREADY_EXISTS)

/**
 * @brief Get a human-readable description of an error code.
 *
 * @param[in] err Error code to describe.
 *
 * @return Pointer to a statically allocated description string.
 */
const char *osal_err_strerror(osal_err_t err);

#ifdef __cplusplus
}
#endif
