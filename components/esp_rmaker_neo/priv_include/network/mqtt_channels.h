/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mqtt_channels.h
 * @brief MQTT channels for the network.
 */

#ifndef __MQTT_CHANNELS_H__
#define __MQTT_CHANNELS_H__

/* MQTT includes */
#include "osal_mqtt_prototypes.h"

/* Main channels includes */
#include "esp_rmaker_mqtt_channels.h"

/* Sub channels **********************************************************************/

/**
 * @brief Sub channels for state changes.
 */
typedef enum {
    MQTT_CHANNEL_SUB_STATE_CHANGE_UPDATE_NAMED = 0,
    MQTT_CHANNEL_SUB_STATE_CHANGE_UPDATE_INDEXED = 1,
    MQTT_CHANNEL_SUB_STATE_CHANGE_UPDATE_TIMESERIES = 2,
    MQTT_CHANNEL_SUB_STATE_CHANGE_DELETE = 3,
    MQTT_CHANNEL_SUB_STATE_CHANGE_START_LISTENING = 4,
    MQTT_CHANNEL_SUB_STATE_CHANGE_STOP_LISTENING = 5,
    MQTT_CHANNEL_SUB_STATE_CHANGE_GROUP_CTRL_START_LISTENING = 6,
    MQTT_CHANNEL_SUB_STATE_CHANGE_GROUP_CTRL_STOP_LISTENING = 7,
} mqtt_channel_sub_state_changes_t;

/**
 * @brief Sub channels for cloud manager.
 */
typedef enum {
    MQTT_CHANNEL_SUB_CLOUD_MANAGER_SEND = 0,
    MQTT_CHANNEL_SUB_CLOUD_MANAGER_START_LISTENING = 1,
    MQTT_CHANNEL_SUB_CLOUD_MANAGER_STOP_LISTENING = 2,
    MQTT_CHANNEL_SUB_CLOUD_MANAGER_CLOUD_SETUP = 3,
    MQTT_CHANNEL_SUB_CLOUD_MANAGER_REPORT_NODE_CONFIG = 4,
    MQTT_CHANNEL_SUB_CLOUD_MANAGER_VERSION_SCHEDULE = 5,
    MQTT_CHANNEL_SUB_CLOUD_MANAGER_VERSION_TRIGGER = 6,
    MQTT_CHANNEL_SUB_CLOUD_MANAGER_BRIDGE_ADD_CHILD = 7,
    MQTT_CHANNEL_SUB_CLOUD_MANAGER_BRIDGE_REMOVE_CHILD = 8,
    /* Per-child cloud-info bundle.
     * Distinct from CLOUD_SETUP so a child publish failure doesn't trigger
     * the self-only cloud-setup retry + on_failure reset. Fire-and-forget;
     * recovery on next RMAKER_MQTT_EVENT_CONNECTED. */
    MQTT_CHANNEL_SUB_CLOUD_MANAGER_BRIDGE_CHILD_CLOUD_INFO = 9,
} mqtt_channel_sub_cloud_manager_t;

/**
 * @brief Sub channels for indexed and named shadows.
 */
typedef enum {
    MQTT_CHANNEL_SUB_INDEXED_SHADOW_SUBSCRIBE = 0,
    MQTT_CHANNEL_SUB_INDEXED_SHADOW_UNSUBSCRIBE = 1,
    MQTT_CHANNEL_SUB_INDEXED_SHADOW_GET = 2,
    MQTT_CHANNEL_SUB_NAMED_SHADOW_SUBSCRIBE = 3,
    MQTT_CHANNEL_SUB_NAMED_SHADOW_UNSUBSCRIBE = 4,
    MQTT_CHANNEL_SUB_NAMED_SHADOW_GET = 5,
} mqtt_channel_sub_shadows_t;

/**
 * @brief Sub channels for notify.
 */
typedef enum {
    MQTT_CHANNEL_SUB_NOTIFY_SEND = 0,
} mqtt_channel_sub_notify_t;

/**
 * @brief Sub channels for the bridge subsystem.
 */
typedef enum {
    MQTT_CHANNEL_SUB_BRIDGE_TO_CLOUD_SEND = 0,
    /* "bridge_filter_cloud" - wildcard MQTT filter for children's
     *   <root>/bridges/<self>/children/<child>/from_cloud
     * topics. Subscribe + unsubscribe completion is co-gated with the
     * self start_listening / stop_listening ack via the cloud manager's
     * atomic ack counters. */
    MQTT_CHANNEL_SUB_BRIDGE_FILTER_CLOUD_SUBSCRIBE = 1,
    /* "bridge_filter_params" - wildcard for children's user-shadow
     *   <root>/bridges/<self>/children/<child>/user/<shadow>/params
     * topics. Co-gated with the above. */
    MQTT_CHANNEL_SUB_BRIDGE_FILTER_PARAMS_SUBSCRIBE = 2,
    MQTT_CHANNEL_SUB_BRIDGE_FILTER_CLOUD_UNSUBSCRIBE = 3,
    MQTT_CHANNEL_SUB_BRIDGE_FILTER_PARAMS_UNSUBSCRIBE = 4,
    MQTT_CHANNEL_SUB_BRIDGE_CHILD_NAMED_SHADOW_UPDATE = 5,
    MQTT_CHANNEL_SUB_BRIDGE_CHILD_INDEXED_SHADOW_UPDATE = 6,
} mqtt_channel_sub_bridge_t;

#endif /* __MQTT_CHANNELS_H__ */
