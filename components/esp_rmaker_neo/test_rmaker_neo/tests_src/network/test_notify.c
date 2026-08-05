/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_notify.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "network/notify.h"

#include "sdkconfig.h"

#include "esp_rmaker_flow.h"
#include "esp_rmaker_data_model.h"
#include "esp_rmaker_mqtt_impl.h"
#include "data_model_internal.h"
#include "node_internal.h"
#include "network/mqtt_topics.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "esp_rmaker_bridge.h"
#include "bridge/bridge_internal.h"
#endif

#include "osal_event_loop.h"

#include <string.h>

/* Publish spy: captures the topic and payload the notify path publishes, so that routing and
 * payload shape can be asserted without a broker. */
#define TEST_NOTIFY_SPY_PAYLOAD_SIZE 256
static char __spy_topic[MQTT_TOPIC_BUFFER_SIZE];
static char __spy_payload[TEST_NOTIFY_SPY_PAYLOAD_SIZE];
static int __spy_publish_count;

static osal_err_t __spy_publish(osal_mqtt_event_loop_channel_t *channel, const char *topic, size_t topic_len,
                                void *data, size_t data_len, osal_mqtt_QoS_t qos, bool retain)
{
    (void)channel;
    (void)qos;
    (void)retain;
    __spy_publish_count++;
    __spy_topic[0] = '\0';
    if (topic != NULL && topic_len < sizeof(__spy_topic)) {
        memcpy(__spy_topic, topic, topic_len);
        __spy_topic[topic_len] = '\0';
    }
    __spy_payload[0] = '\0';
    if (data != NULL && data_len < sizeof(__spy_payload)) {
        memcpy(__spy_payload, data, data_len);
        __spy_payload[data_len] = '\0';
    }
    return OSAL_ERR_OK;
}

/* Same capture, but reports the publish as failed, to exercise the best-effort notify contract. */
static osal_err_t __spy_publish_fail(osal_mqtt_event_loop_channel_t *channel, const char *topic, size_t topic_len,
                                     void *data, size_t data_len, osal_mqtt_QoS_t qos, bool retain)
{
    (void)__spy_publish(channel, topic, topic_len, data, data_len, qos, retain);
    return OSAL_ERR_FAIL;
}

static esp_rmaker_error_t __test_notify_payload_fail(json_gen_str_t *jptr, void *data, bool is_sizing)
{
    (void)jptr;
    (void)data;
    (void)is_sizing;
    return ESP_RMAKER_FAIL;
}

void test_notify_init_deinit(void)
{
    /* Init should succeed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_notify_init());

    /* Deinit should succeed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_notify_deinit());
}

void test_notify_send_invalid_args(void)
{
    /* NULL notification should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_notify_send(NULL));

    /* Notification with NULL payload function should fail */
    esp_rmaker_notification_t notification = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_notify_send(&notification));
}

void test_notify_send_payload_generator_fail(void)
{
    esp_rmaker_notification_t notification = {
        .report_payload_fn = __test_notify_payload_fail,
        .data = NULL,
    };

    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_notify_send(&notification));
}

void test_notify_send_push_no_node_rejected(void)
{
    /* With no node, the update ID resolves to nothing and there is no self node to fall
     * back to, so the push must be rejected instead of published on a bogus topic. */
    TEST_ASSERT_NULL(esp_rmaker_get_node());
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_notify_send_push(NULL));
}

static esp_rmaker_param_t *__add_param_to_node(esp_rmaker_node_t *node, const char *dev_name, const char *param_name)
{
    esp_rmaker_device_t *dev = esp_rmaker_device_create(dev_name, "t", NULL);
    TEST_ASSERT_NOT_NULL(dev);
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_name, "bool", esp_rmaker_bool(false), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(param);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, dev));
    return param;
}

/* Publishes a push for the given update ID and copies out the topic it went out on, along
 * with the send result. No assertions: this runs while the spy is installed, and a failing
 * assertion here would abort the test before the real MQTT impl is restored, poisoning every
 * later test in the suite. */
static void __push_and_capture(esp_rmaker_state_update_id_t update_id, esp_rmaker_error_t *out_err,
                               char *out_topic, size_t out_topic_size, char *out_payload, size_t out_payload_size)
{
    __spy_topic[0] = '\0';
    __spy_payload[0] = '\0';
    __spy_publish_count = 0;
    *out_err = esp_rmaker_notify_send_push(update_id);
    out_topic[0] = '\0';
    out_payload[0] = '\0';
    if (__spy_publish_count == 1) {
        if (strlen(__spy_topic) < out_topic_size) {
            strcpy(out_topic, __spy_topic);
        }
        if (strlen(__spy_payload) < out_payload_size) {
            strcpy(out_payload, __spy_payload);
        }
    }
}

/* Expected notify topic for a node, built independently of the notify path. */
static void __expected_notify_topic(const esp_rmaker_node_t *node, char *out, size_t out_size)
{
    out[0] = '\0';
    (void)esp_rmaker_mqtt_topic_notify_for_node(esp_rmaker_node_topic_ctx(node), out, out_size);
}

void test_notify_send_push_routes_by_update_id(void)
{
    osal_event_loop_create_default();
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    bridge_internal_deinit();
#endif
    esp_rmaker_config_t config = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "tnode", "ttype");
    TEST_ASSERT_NOT_NULL(node);

    esp_rmaker_param_t *self_param = __add_param_to_node(node, "notify_dev", "alarm");

    char expected_self[MQTT_TOPIC_BUFFER_SIZE];
    char actual_self[MQTT_TOPIC_BUFFER_SIZE];
    char actual_null[MQTT_TOPIC_BUFFER_SIZE];
    char payload_self[TEST_NOTIFY_SPY_PAYLOAD_SIZE];
    char payload_null[TEST_NOTIFY_SPY_PAYLOAD_SIZE];
    esp_rmaker_error_t err_self, err_null;
    __expected_notify_topic(node, expected_self, sizeof(expected_self));

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_test_seed_child("a", "lid-a", "parent--a");
    TEST_ASSERT_NOT_NULL(child);
    esp_rmaker_node_t *child_node = esp_rmaker_bridge_child_node(child);
    TEST_ASSERT_NOT_NULL(child_node);
    esp_rmaker_param_t *child_param = __add_param_to_node(child_node, "child_dev", "alarm");

    char expected_child[MQTT_TOPIC_BUFFER_SIZE];
    char actual_child[MQTT_TOPIC_BUFFER_SIZE];
    char payload_child[TEST_NOTIFY_SPY_PAYLOAD_SIZE];
    esp_rmaker_error_t err_child;
    __expected_notify_topic(child_node, expected_child, sizeof(expected_child));
#endif

    /* Capture phase: the publish spy stands in for the broker. Nothing may assert until the
     * real impl is restored below. */
    osal_mqtt_impl_t saved_impl = esp_rmaker_mqtt_impl;
    esp_rmaker_mqtt_impl.publish = __spy_publish;

    esp_rmaker_state_update_id_t self_id = esp_rmaker_state_update_id_create(self_param);
    __push_and_capture(self_id, &err_self, actual_self, sizeof(actual_self), payload_self, sizeof(payload_self));
    __push_and_capture(NULL, &err_null, actual_null, sizeof(actual_null), payload_null, sizeof(payload_null));
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_state_update_id_t child_id = esp_rmaker_state_update_id_create(child_param);
    __push_and_capture(child_id, &err_child, actual_child, sizeof(actual_child), payload_child, sizeof(payload_child));
#endif

    esp_rmaker_mqtt_impl = saved_impl;

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    data_model_state_update_id_release(child_id);
#endif
    data_model_state_update_id_release(self_id);

    /* Tear down before asserting: a failing assertion aborts the test, and leaving the node,
     * the seeded child or the event loop behind would cascade into every later test in the
     * suite and bury this failure. */
    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    bridge_internal_deinit();
#endif
    osal_event_loop_delete_default();

    /* Assert phase. */
    TEST_ASSERT_NOT_NULL(self_id);
    TEST_ASSERT_TRUE(strlen(expected_self) > 0);

    /* A self-owned param publishes on the self node's notify topic, with the push payload the
     * cloud's node_notify_rule expects. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err_self);
    TEST_ASSERT_EQUAL_STRING(expected_self, actual_self);
    TEST_ASSERT_EQUAL_STRING("{\"notify\":{\"push\":true}}", payload_self);

    /* A NULL update ID falls back to the self node rather than being dropped. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err_null);
    TEST_ASSERT_EQUAL_STRING(expected_self, actual_null);
    TEST_ASSERT_EQUAL_STRING("{\"notify\":{\"push\":true}}", payload_null);

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    /* A param owned by a bridge child publishes on that child's notify topic, NOT the
     * parent's. This is what breaks when the update ID is discarded. */
    TEST_ASSERT_NOT_NULL(child_id);
    TEST_ASSERT_TRUE(strlen(expected_child) > 0);
    TEST_ASSERT_FALSE(strcmp(expected_self, expected_child) == 0);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err_child);
    TEST_ASSERT_EQUAL_STRING(expected_child, actual_child);
    TEST_ASSERT_EQUAL_STRING("{\"notify\":{\"push\":true}}", payload_child);
#endif
}

/* esp_rmaker_param_update_and_notify() lives in the data model, but its whole point is the
 * notify side effect, so it is covered here where the publish spy is. */
void test_notify_param_update_and_notify_publishes_push(void)
{
    osal_event_loop_create_default();
    esp_rmaker_config_t config = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "tnode", "ttype");
    TEST_ASSERT_NOT_NULL(node);

    esp_rmaker_param_t *param = __add_param_to_node(node, "notify_dev", "alarm");

    char expected_topic[MQTT_TOPIC_BUFFER_SIZE];
    char actual_topic[MQTT_TOPIC_BUFFER_SIZE];
    char actual_payload[TEST_NOTIFY_SPY_PAYLOAD_SIZE];
    __expected_notify_topic(node, expected_topic, sizeof(expected_topic));

    osal_mqtt_impl_t saved_impl = esp_rmaker_mqtt_impl;

    /* Updating with notify publishes exactly one push for the updated param. */
    esp_rmaker_mqtt_impl.publish = __spy_publish;
    __spy_topic[0] = '\0';
    __spy_payload[0] = '\0';
    __spy_publish_count = 0;
    esp_rmaker_error_t err_ok = esp_rmaker_param_update_and_notify(param, esp_rmaker_bool(true));
    int count_ok = __spy_publish_count;
    actual_topic[0] = '\0';
    actual_payload[0] = '\0';
    if (strlen(__spy_topic) < sizeof(actual_topic)) {
        strcpy(actual_topic, __spy_topic);
    }
    if (strlen(__spy_payload) < sizeof(actual_payload)) {
        strcpy(actual_payload, __spy_payload);
    }
    esp_rmaker_param_val_t *val_ok = esp_rmaker_param_get_val(param);
    bool val_ok_set = (val_ok != NULL) && (val_ok->val.b == true);

    /* A failing publish must not change the result of an update that already happened:
     * the notification is best-effort. */
    esp_rmaker_mqtt_impl.publish = __spy_publish_fail;
    __spy_publish_count = 0;
    esp_rmaker_error_t err_notify_failed = esp_rmaker_param_update_and_notify(param, esp_rmaker_bool(false));
    int count_failed = __spy_publish_count;
    esp_rmaker_param_val_t *val_failed = esp_rmaker_param_get_val(param);
    bool val_failed_set = (val_failed != NULL) && (val_failed->val.b == false);

    esp_rmaker_mqtt_impl = saved_impl;

    /* Tear down before asserting - see test_notify_send_push_routes_by_update_id. */
    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
    osal_event_loop_delete_default();

    /* Assert phase. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err_ok);
    TEST_ASSERT_EQUAL(1, count_ok);
    TEST_ASSERT_TRUE(strlen(expected_topic) > 0);
    TEST_ASSERT_EQUAL_STRING(expected_topic, actual_topic);
    TEST_ASSERT_EQUAL_STRING("{\"notify\":{\"push\":true}}", actual_payload);
    TEST_ASSERT_TRUE(val_ok_set);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err_notify_failed);
    TEST_ASSERT_EQUAL(1, count_failed);
    TEST_ASSERT_TRUE(val_failed_set);
}
