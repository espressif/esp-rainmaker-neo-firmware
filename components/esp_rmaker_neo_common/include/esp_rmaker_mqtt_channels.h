/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_mqtt_channels.h
 * @brief MQTT channels for all RainMaker Neo common dependents.
 */

#ifndef __ESP_RMAKER_MQTT_CHANNELS_H__
#define __ESP_RMAKER_MQTT_CHANNELS_H__

/**
 * @brief Main channels for all components.
 *
 * Put here so that all components that depend on this component can avoid redefining the same values.
 */
typedef enum {
    /* RMNG core components */
    MQTT_CHANNEL_MAIN_STATE_CHANGES = 0, /* State changes */
    MQTT_CHANNEL_MAIN_CLOUD_MANAGER = 1, /* Cloud manager */
    MQTT_CHANNEL_MAIN_SHADOWS = 2, /* Indexed and named shadows */
    MQTT_CHANNEL_MAIN_NOTIFY = 3, /* Notify */
    MQTT_CHANNEL_MAIN_BRIDGE = 4, /* Bridge control + subscriber */

    /* OTA component */
    MQTT_CHANNEL_MAIN_OTA = 10, /* OTA */
} mqtt_channel_main_t;

#endif /* __ESP_RMAKER_MQTT_CHANNELS_H__ */
