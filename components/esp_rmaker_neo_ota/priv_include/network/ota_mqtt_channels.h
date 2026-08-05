/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_mqtt_channels.h
 * @brief MQTT channels for the OTA.
 */

#ifndef __OTA_MQTT_CHANNELS_H__
#define __OTA_MQTT_CHANNELS_H__

/* Main channels includes */
#include "esp_rmaker_mqtt_channels.h"

/* Sub channels **********************************************************************/

/**
 * @brief Sub channels for OTA.
 */
typedef enum {
    /* OTA Jobs */
    MQTT_CHANNEL_SUB_OTA_GET_PENDING = 0,
    MQTT_CHANNEL_SUB_OTA_DESCRIBE_JOB = 1,
    MQTT_CHANNEL_SUB_OTA_UPDATE_JOB = 2,

    /* MQTT File Downloader */
    MQTT_CHANNEL_SUB_OTA_STREAM_DATA_REQUEST = 3,
    MQTT_CHANNEL_SUB_OTA_STREAM_DATA_SUBSCRIBE = 4,
    MQTT_CHANNEL_SUB_OTA_STREAM_DATA_UNSUBSCRIBE = 5,

    /* OTA Jobs subscribe lifecycle (separate from GET_PENDING publish to avoid on-complete collision) */
    MQTT_CHANNEL_SUB_OTA_JOBS_SUBSCRIBE = 6,
    MQTT_CHANNEL_SUB_OTA_JOBS_UNSUBSCRIBE = 7,
} mqtt_channel_sub_ota_t;

#endif /* __OTA_MQTT_CHANNELS_H__ */
