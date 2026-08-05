/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_event_loop.c
 * @brief Implementation of the event loop for the ESP RainMaker Neo SDK.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "event_loop.h"

/* Standard includes */
#include <stddef.h>
#include <string.h>

/* Constants **********************************************************************/

/**
 * @brief Event loop base event definition.
 */
OSAL_EVENT_DEFINE_BASE(RMAKER_EVENT);

/**
 * @brief MQTT connection handler.
 */
static osal_event_handler_t g_mqtt_connection_handler = NULL;

/* Public function definitions ****************************************************/

esp_rmaker_error_t event_loop_init(osal_event_handler_t mqtt_connection_handler)
{
    if (!mqtt_connection_handler) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_err_t event_loop_err = osal_event_loop_create_default();
    if (event_loop_err != OSAL_ERR_OK && event_loop_err != OSAL_ERR_INVALID_STATE) {
        return ESP_RMAKER_FAIL;
    }
    event_loop_err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, mqtt_connection_handler, NULL);
    if (event_loop_err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    event_loop_err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_DISCONNECTED, mqtt_connection_handler, NULL);
    if (event_loop_err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    g_mqtt_connection_handler = mqtt_connection_handler;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t event_loop_deinit(void)
{
    /* Unregister the MQTT connection handler */
    if (g_mqtt_connection_handler) {
        osal_err_t event_loop_err = osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, g_mqtt_connection_handler);
        if (event_loop_err != OSAL_ERR_OK) {
            return ESP_RMAKER_FAIL;
        }
        event_loop_err = osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_DISCONNECTED, g_mqtt_connection_handler);
        if (event_loop_err != OSAL_ERR_OK) {
            return ESP_RMAKER_FAIL;
        }
        g_mqtt_connection_handler = NULL;
    }

    /* Default event loop deletion is avoided in case other processes rely on it. */
    return ESP_RMAKER_OK;
}


esp_rmaker_error_t event_loop_register_mqtt_on_complete_handler(osal_event_handler_t handler)
{
    osal_err_t err;
    err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_PUBLISHED, handler, NULL);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_SUBSCRIBED, handler, NULL);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_UNSUBSCRIBED, handler, NULL);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t event_loop_unregister_mqtt_on_complete_handler(osal_event_handler_t handler)
{
    osal_err_t err;
    err = osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_PUBLISHED, handler);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    err = osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_SUBSCRIBED, handler);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    err = osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_UNSUBSCRIBED, handler);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t event_loop_register_timezone_change_handler(osal_event_handler_t handler)
{
    osal_err_t err;
    err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_EVENT_TZ_POSIX_CHANGED, handler, NULL);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_EVENT_TZ_CHANGED, handler, NULL);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t event_loop_unregister_timezone_change_handler(osal_event_handler_t handler)
{
    osal_err_t err;
    err = osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_EVENT_TZ_POSIX_CHANGED, handler);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    err = osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_EVENT_TZ_CHANGED, handler);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}
