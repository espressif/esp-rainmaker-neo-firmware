/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mqtt_control.c
 * @brief MQTT control functions.
 */

/* Includes *******************************************************/

/* Declarations includes */
#include "network/mqtt_control.h"

/* Socket mocks includes */
#include "socket_mocks.h"

/* Standard includes */
#include <stdbool.h>

/* Platform common includes */
#include "osal_log.h"

/* Network includes */
#include "network/common.h"

/* Constants **************************************************************/

/**
 * @brief Tag for the MQTT control functions.
 */
static const char *TAG = "rmng_hc_mqtt_ctrl";

/* Private variables **************************************************************/

/**
 * @brief Private data for the MQTT control functions.
 */
static struct {
    osal_mqtt_impl_t original; /* Original MQTT implementation. */
    bool is_holding_original; /* Whether the original MQTT implementation is being held. */
} mqtt_control_priv_data = {
    .original = {0},
    .is_holding_original = false,
};

/* Private function declarations ****************************************************/

/** MQTT mock publish function
 *
 * @return OSAL_ERR_MQTT_SEND_FAILED always.
 */
static osal_err_t __osal_mqtt_mock_publish( osal_mqtt_event_loop_channel_t *channel,
        const char *topic,
        size_t topic_len,
        void *data,
        size_t data_len,
        osal_mqtt_QoS_t qos,
        bool retain );

/** MQTT mock subscribe function
 *
 * @return OSAL_ERR_MQTT_SEND_FAILED always.
 */
static osal_err_t __osal_mqtt_mock_subscribe( osal_mqtt_event_loop_channel_t *channel,
        const char *topic,
        size_t topic_len,
        osal_mqtt_subscribe_cb_t cb,
        osal_mqtt_QoS_t qos,
        void *priv_data );

/** MQTT mock unsubscribe function
 *
 * @return OSAL_ERR_MQTT_SEND_FAILED always.
 */
static osal_err_t __osal_mqtt_mock_unsubscribe( osal_mqtt_event_loop_channel_t *channel,
        const char *topic,
        size_t topic_len,
        osal_mqtt_QoS_t qos );

/* Private function definitions ****************************************************/

static osal_err_t __osal_mqtt_mock_publish( osal_mqtt_event_loop_channel_t *channel,
        const char *topic,
        size_t topic_len,
        void *data,
        size_t data_len,
        osal_mqtt_QoS_t qos,
        bool retain )
{
    return OSAL_ERR_MQTT_SEND_FAILED;
}

static osal_err_t __osal_mqtt_mock_subscribe( osal_mqtt_event_loop_channel_t *channel,
        const char *topic,
        size_t topic_len,
        osal_mqtt_subscribe_cb_t cb,
        osal_mqtt_QoS_t qos,
        void *priv_data )
{
    return OSAL_ERR_MQTT_SEND_FAILED;
}

static osal_err_t __osal_mqtt_mock_unsubscribe( osal_mqtt_event_loop_channel_t *channel,
        const char *topic,
        size_t topic_len,
        osal_mqtt_QoS_t qos )
{
    return OSAL_ERR_MQTT_SEND_FAILED;
}

/* Public function definitions ****************************************************/

esp_rmaker_error_t network_mqtt_control_force_network_failure(void)
{
    socket_mock_force_failure(true);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t network_mqtt_control_restore_network_default(void)
{
    socket_mock_force_failure(false);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t network_mqtt_control_force_operations_failure(void)
{
    if (mqtt_control_priv_data.is_holding_original) {
        OSAL_LOGE(TAG, "MQTT implementation already forced to failure");
        return ESP_RMAKER_INVALID_STATE;
    }
    mqtt_control_priv_data.original = esp_rmaker_mqtt_impl;
    mqtt_control_priv_data.is_holding_original = true;

    // override the publish, subscribe, and unsubscribe functions to return failure
    esp_rmaker_mqtt_impl.publish = __osal_mqtt_mock_publish;
    esp_rmaker_mqtt_impl.subscribe = __osal_mqtt_mock_subscribe;
    esp_rmaker_mqtt_impl.unsubscribe = __osal_mqtt_mock_unsubscribe;

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t network_mqtt_control_restore_operations_default(void)
{
    if (!mqtt_control_priv_data.is_holding_original) {
        OSAL_LOGE(TAG, "MQTT implementation not forced to failure");
        return ESP_RMAKER_INVALID_STATE;
    }

    esp_rmaker_mqtt_impl = mqtt_control_priv_data.original;
    mqtt_control_priv_data.is_holding_original = false;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t network_mqtt_control_disconnect(void)
{
    if (esp_rmaker_mqtt_impl.disconnect == NULL) {
        OSAL_LOGE(TAG, "MQTT disconnect not available");
        return ESP_RMAKER_INVALID_STATE;
    }
    osal_err_t err = esp_rmaker_mqtt_impl.disconnect();
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "MQTT disconnect failed: %d", err);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t network_mqtt_control_connect(void)
{
    if (esp_rmaker_mqtt_impl.connect == NULL) {
        OSAL_LOGE(TAG, "MQTT connect not available");
        return ESP_RMAKER_INVALID_STATE;
    }
    osal_err_t err = esp_rmaker_mqtt_impl.connect();
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "MQTT connect failed: %d", err);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}
