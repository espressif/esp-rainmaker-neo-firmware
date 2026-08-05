/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bridge_subscriber.c
 * @brief MQTT subscriptions on the bridge namespace
 *        (bridge_filter_cloud + bridge_filter_params).
 *
 * Current scope:
 *   - Subscribes to the two wildcard filters on bridge connect.
 *   - Parses the child thing name (and shadow name segment for
 *     bridge_filter_params).
 *   - Looks up the corresponding child handle.
 *   - Logs the dispatch.
 *
 * TODO: feed the payload into the data-model param-write dispatch scoped to the
 * child's virtual devices, and handle internal child events on
 * bridge_filter_cloud (e.g. ``getGroupInfo``) - the latter needs the per-ctx
 * state pipeline.
 */

#include "esp_rmaker_bridge.h"
#include "bridge/bridge_internal.h"

#include "constants/identity.h"
#include "data_model_internal.h"
#include "network/common.h"
#include "network/mqtt_topics.h"
#include "network/mqtt_channels.h"
#include "network/cloud/manager_internal.h"
#include "esp_rmaker_mqtt_channels.h"

#include "osal_log.h"

#include <stddef.h>
#include <string.h>

static const char *TAG = "rmng_br_sub";

/* Parse helpers (exposed via bridge_internal.h for unit testing) ************/

int bridge_internal_parse_child_from_from_cloud_topic(const char *topic, size_t topic_len, char *out, size_t out_size)
{
    if (topic == NULL || out == NULL || out_size == 0) {
        return -1;
    }
    /* Locate the 4th '/' (after "children") then read up to next '/'. */
    int slash_count = 0;
    size_t i = 0;
    for (; i < topic_len; i++) {
        if (topic[i] == '/') {
            slash_count++;
            if (slash_count == 4) {
                i++;
                break;
            }
        }
    }
    if (slash_count < 4 || i >= topic_len) {
        return -1;
    }
    size_t start = i;
    while (i < topic_len && topic[i] != '/') {
        i++;
    }
    size_t len = i - start;
    if (len == 0 || len >= out_size) {
        return -1;
    }
    memcpy(out, topic + start, len);
    out[len] = '\0';
    return (int)len;
}

int bridge_internal_parse_child_and_shadow_from_params_topic(const char *topic, size_t topic_len,
        char *child_out, size_t child_out_size,
        char *shadow_out, size_t shadow_out_size)
{
    if (topic == NULL || child_out == NULL || shadow_out == NULL ||
            child_out_size == 0 || shadow_out_size == 0) {
        return -1;
    }
    if (bridge_internal_parse_child_from_from_cloud_topic(topic, topic_len, child_out, child_out_size) < 0) {
        return -1;
    }
    /* After <child>: "/user/<shadow_name>/params" - locate 6th '/' then read up to next. */
    int slash_count = 0;
    size_t i = 0;
    for (; i < topic_len; i++) {
        if (topic[i] == '/') {
            slash_count++;
            if (slash_count == 6) {
                i++;
                break;
            }
        }
    }
    if (slash_count < 6 || i >= topic_len) {
        return -1;
    }
    size_t start = i;
    while (i < topic_len && topic[i] != '/') {
        i++;
    }
    size_t len = i - start;
    if (len == 0 || len >= shadow_out_size) {
        return -1;
    }
    /* Verify trailing "/params" segment to reject truncated topics. */
    static const char kParamsTail[] = "/params";
    const size_t kParamsTailLen = sizeof(kParamsTail) - 1;
    if (topic_len < i + kParamsTailLen ||
            memcmp(topic + i, kParamsTail, kParamsTailLen) != 0) {
        return -1;
    }
    memcpy(shadow_out, topic + start, len);
    shadow_out[len] = '\0';
    return (int)len;
}

/* MQTT subscribe callbacks **************************************************/

static void __cb_from_cloud(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data)
{
    char child_thing_name[RMAKER_THING_NAME_BUFFER_SIZE];
    if (bridge_internal_parse_child_from_from_cloud_topic(topic, topic_len, child_thing_name, sizeof(child_thing_name)) < 0) {
        OSAL_LOGW(TAG, "from_cloud: failed to parse child thing name from %.*s", (int)topic_len, topic);
        return;
    }

    esp_rmaker_bridge_child_handle_t child = bridge_internal_find_by_thing_name(child_thing_name);
    if (!child) {
        /* Cloud may keep delivering inflight messages for a child the
         * firmware just tore down (post-remove). Expected, harmless. */
        OSAL_LOGD(TAG, "from_cloud: no READY child for %s (post-remove or unknown)", child_thing_name);
        return;
    }

    /* Route the payload through the shared cloud-manager dispatch helper
     * with the child's topic ctx; per-event handlers branch on ctx for
     * per-child persistence + follow-up publishes. */
    const esp_rmaker_topic_ctx_t *child_ctx = esp_rmaker_node_topic_ctx(bridge_internal_child_node(child));
    if (!esp_rmaker_topic_ctx_is_valid(child_ctx)) {
        OSAL_LOGW(TAG, "from_cloud: child ctx invalid for %s", child_thing_name);
        return;
    }
    cloud_manager_internal_dispatch_payload(child_ctx, (const char *)payload, payload_len);
}

static void __cb_params(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data)
{
    char child_thing_name[RMAKER_THING_NAME_BUFFER_SIZE];
    char shadow_name[64];
    if (bridge_internal_parse_child_and_shadow_from_params_topic(topic, topic_len,
            child_thing_name, sizeof(child_thing_name),
            shadow_name, sizeof(shadow_name)) < 0) {
        OSAL_LOGW(TAG, "params: failed to parse child/shadow from %.*s", (int)topic_len, topic);
        return;
    }

    esp_rmaker_bridge_child_handle_t child = bridge_internal_find_by_thing_name(child_thing_name);
    if (!child) {
        OSAL_LOGD(TAG, "params: no READY child for %s (post-remove or unknown)", child_thing_name);
        return;
    }

    esp_rmaker_node_t *child_node = bridge_internal_child_node(child);
    if (!child_node) {
        OSAL_LOGW(TAG, "params: no node for child %s", child_thing_name);
        return;
    }
    OSAL_LOGD(TAG, "params for %s shadow=%s (%d bytes): %.*s",
              child_thing_name, shadow_name, (int)payload_len, (int)payload_len, (char *)payload);
    (void)shadow_name;
    esp_rmaker_error_t err = data_model_state_handle_update_payload_json(
                                 child_node, (const char *)payload, payload_len, ESP_RMAKER_REQ_SRC_CLOUD);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "params: dispatch failed for %s: %d", child_thing_name, err);
    }
}

/* Public hooks **************************************************************/

esp_rmaker_error_t bridge_subscriber_subscribe_all(uint8_t *out_issued)
{
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_BRIDGE,
        .sub = MQTT_CHANNEL_SUB_BRIDGE_FILTER_CLOUD_SUBSCRIBE,
    };
    uint8_t issued = 0;
    bool failed_any = false;

    /* Attempt both subscribes; do NOT early-return on the first failure, so
     * the gate's expected ack count (derived from *out_issued) matches the
     * number of subscribes that will actually ACK. */
    int len = esp_rmaker_mqtt_topic_bridges_children_from_cloud_filter(topic, sizeof(topic));
    if (len < 0) {
        OSAL_LOGE(TAG, "Failed to build children-from_cloud filter");
        failed_any = true;
    } else {
        osal_err_t err = esp_rmaker_mqtt_impl.subscribe(&channel, topic, (size_t)len, __cb_from_cloud, QoS1, NULL);
        if (err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Subscribe %s failed: %d", topic, err);
            failed_any = true;
        } else {
            issued++;
            OSAL_LOGI(TAG, "Subscribed: %s", topic);
        }
    }

    channel.sub = MQTT_CHANNEL_SUB_BRIDGE_FILTER_PARAMS_SUBSCRIBE;
    len = esp_rmaker_mqtt_topic_bridges_children_params_filter(topic, sizeof(topic));
    if (len < 0) {
        OSAL_LOGE(TAG, "Failed to build children-params filter");
        failed_any = true;
    } else {
        osal_err_t err = esp_rmaker_mqtt_impl.subscribe(&channel, topic, (size_t)len, __cb_params, QoS1, NULL);
        if (err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Subscribe %s failed: %d", topic, err);
            failed_any = true;
        } else {
            issued++;
            OSAL_LOGI(TAG, "Subscribed: %s", topic);
        }
    }

    if (out_issued) {
        *out_issued = issued;
    }
    return failed_any ? ESP_RMAKER_FAIL : ESP_RMAKER_OK;
}

esp_rmaker_error_t bridge_subscriber_unsubscribe_all(uint8_t *out_issued)
{
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_BRIDGE,
        .sub = MQTT_CHANNEL_SUB_BRIDGE_FILTER_CLOUD_UNSUBSCRIBE,
    };
    uint8_t issued = 0;
    bool failed_any = false;

    int len = esp_rmaker_mqtt_topic_bridges_children_from_cloud_filter(topic, sizeof(topic));
    if (len >= 0) {
        if (esp_rmaker_mqtt_impl.unsubscribe(&channel, topic, (size_t)len, QoS1) == OSAL_ERR_OK) {
            issued++;
        } else {
            failed_any = true;
        }
    } else {
        failed_any = true;
    }

    channel.sub = MQTT_CHANNEL_SUB_BRIDGE_FILTER_PARAMS_UNSUBSCRIBE;
    len = esp_rmaker_mqtt_topic_bridges_children_params_filter(topic, sizeof(topic));
    if (len >= 0) {
        if (esp_rmaker_mqtt_impl.unsubscribe(&channel, topic, (size_t)len, QoS1) == OSAL_ERR_OK) {
            issued++;
        } else {
            failed_any = true;
        }
    } else {
        failed_any = true;
    }

    if (out_issued) {
        *out_issued = issued;
    }
    return failed_any ? ESP_RMAKER_FAIL : ESP_RMAKER_OK;
}
