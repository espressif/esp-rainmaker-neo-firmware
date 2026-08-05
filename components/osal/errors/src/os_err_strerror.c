/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "osal_err.h"
#include "osal_err_values.h"

#ifdef ESP_PLATFORM
#include "esp_err.h"
#endif

static const char *core_strerror(osal_err_t err)
{
    switch ((int)err) {
    case OSAL_ERR_VAL_OK:
        return "OK";
    case OSAL_ERR_VAL_FAIL:
        return "Fail";
    case OSAL_ERR_VAL_NO_MEM:
        return "Out of memory";
    case OSAL_ERR_INVALID_ARG:
        return "Invalid argument";
    case OSAL_ERR_VAL_INVALID_STATE:
        return "Invalid state";
    case OSAL_ERR_VAL_TIMEOUT:
        return "Timeout";
    case OSAL_ERR_VAL_NOT_FOUND:
        return "Not found";
    case OSAL_ERR_VAL_NOT_SUPPORTED:
        return "Not supported";
    case OSAL_ERR_VAL_INVALID_RESPONSE:
        return "Invalid response";
    default:
        return NULL;
    }
}

const char *osal_err_strerror(osal_err_t err)
{
    const char *s = core_strerror(err);
    if (s) {
        return s;
    }

    switch ((int)err) {
    case OSAL_ERR_VAL_HTTP_CONNECTION_FAILED:
        return "HTTP connection failed";

    case OSAL_ERR_VAL_MQTT_SEND_FAILED:
        return "MQTT send failed";
    case OSAL_ERR_VAL_MQTT_RECV_FAILED:
        return "MQTT receive failed";
    case OSAL_ERR_VAL_MQTT_BAD_RESPONSE:
        return "MQTT bad response";
    case OSAL_ERR_VAL_MQTT_SERVER_REFUSED:
        return "MQTT server refused";
    case OSAL_ERR_VAL_MQTT_NO_DATA_AVAILABLE:
        return "MQTT no data available";
    case OSAL_ERR_VAL_MQTT_STATE_COLLISION:
        return "MQTT state collision";
    case OSAL_ERR_VAL_MQTT_KEEP_ALIVE_TIMEOUT:
        return "MQTT keep-alive timeout";
    case OSAL_ERR_VAL_MQTT_NEED_MORE_BYTES:
        return "MQTT need more bytes";
    case OSAL_ERR_VAL_MQTT_NOT_CONNECTED:
        return "MQTT not connected";

    case OSAL_ERR_NVS_NOT_INITIALIZED:
        return "NVS not initialized";
    case OSAL_ERR_NVS_PARTITION_NOT_FOUND:
        return "NVS partition not found";
    case OSAL_ERR_VAL_NVS_NAMESPACE_NOT_FOUND:
        return "NVS namespace not found";
    case OSAL_ERR_VAL_NVS_KEY_NOT_FOUND:
        return "NVS key not found";

    case OSAL_ERR_VAL_OTA_INVALID_SIZE:
        return "OTA invalid size";
    case OSAL_ERR_VAL_OTA_PARTITION_CONFLICT:
        return "OTA partition conflict";
    case OSAL_ERR_VAL_OTA_SELECT_INFO_INVALID:
        return "OTA select info invalid";
    case OSAL_ERR_VAL_OTA_VALIDATE_FAILED:
        return "OTA validation failed";
    case OSAL_ERR_VAL_OTA_SMALL_SEC_VER:
        return "OTA security version too small";
    case OSAL_ERR_VAL_OTA_ROLLBACK_FAILED:
        return "OTA rollback failed";
    case OSAL_ERR_VAL_OTA_ROLLBACK_INVALID_STATE:
        return "OTA rollback invalid state";

    case OSAL_ERR_VAL_RMAKER_NOT_INITIALIZED:
        return "RainMaker not initialized";
    case OSAL_ERR_VAL_RMAKER_ALREADY_INITIALIZED:
        return "RainMaker already initialized";
    case OSAL_ERR_VAL_RMAKER_NOT_CONNECTED:
        return "RainMaker not connected";
    case OSAL_ERR_VAL_RMAKER_ALREADY_CONNECTED:
        return "RainMaker already connected";
    case OSAL_ERR_VAL_RMAKER_NOT_DISCONNECTED:
        return "RainMaker not disconnected";
    case OSAL_ERR_VAL_RMAKER_ALREADY_EXISTS:
        return "RainMaker already exists";

    default:
#ifdef ESP_PLATFORM
        return esp_err_to_name(err);
#else
        return "Unknown error";
#endif
    }
}
