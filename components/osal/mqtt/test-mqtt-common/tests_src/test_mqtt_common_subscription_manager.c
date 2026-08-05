/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_mqtt_common_subscription_manager.c
 * @brief Unit tests for osal_mqtt_subscription_manager (init/add/remove/handle_publish).
 */

#include "unity.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "osal_mqtt_subscription_manager.h"
#include "osal_mqtt_prototypes.h"

static unsigned int s_callback_invoked_count;
static char *s_last_topic;
static void *s_last_payload;
static size_t s_last_payload_len;
static void *s_last_priv_data;

static void test_sub_callback(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data)
{
    (void)topic_len;
    s_callback_invoked_count++;
    if (s_last_topic) {
        free(s_last_topic);
    }
    s_last_topic = topic ? strndup(topic, topic_len) : NULL;
    s_last_payload = payload;
    s_last_payload_len = payload_len;
    s_last_priv_data = priv_data;
}

static void subscription_test_setup(void)
{
    if (s_last_topic) {
        free(s_last_topic);
        s_last_topic = NULL;
    }
    s_callback_invoked_count = 0;
    s_last_payload = NULL;
    s_last_payload_len = 0;
    s_last_priv_data = NULL;
    osal_mqtt_subscription_deinit();
    osal_mqtt_subscription_init();
}

void test_mqtt_subscription_init_get_list(void)
{
    subscription_test_setup();
    osal_mqtt_subscription_element_t *list = osal_mqtt_subscription_get_list();
    TEST_ASSERT_NOT_NULL(list);
    for (int i = 0; i < OSAL_MQTT_MAX_SUBSCRIPTIONS; i++) {
        TEST_ASSERT_EQUAL(0, list[i].usFilterStringLength);
        TEST_ASSERT_NULL(list[i].pcSubscriptionFilterString);
    }
}

void test_mqtt_subscription_add_and_remove(void)
{
    subscription_test_setup();
    const char *topic = "test/topic";
    uint16_t topic_len = (uint16_t)strlen(topic);

    bool added = osal_mqtt_subscription_add(topic, topic_len, NULL, test_sub_callback, QoS0, NULL);
    TEST_ASSERT_TRUE(added);

    osal_mqtt_subscription_element_t *list = osal_mqtt_subscription_get_list();
    TEST_ASSERT_NOT_NULL(list);
    bool found = false;
    for (int i = 0; i < OSAL_MQTT_MAX_SUBSCRIPTIONS; i++) {
        if (list[i].usFilterStringLength > 0) {
            TEST_ASSERT_EQUAL(topic_len, list[i].usFilterStringLength);
            TEST_ASSERT_EQUAL_STRING_LEN(topic, list[i].pcSubscriptionFilterString, topic_len);
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);

    osal_mqtt_subscription_remove(topic, topic_len);
    for (int i = 0; i < OSAL_MQTT_MAX_SUBSCRIPTIONS; i++) {
        TEST_ASSERT_EQUAL(0, list[i].usFilterStringLength);
        TEST_ASSERT_NULL(list[i].pcSubscriptionFilterString);
    }
}

void test_mqtt_subscription_add_invalid_params(void)
{
    subscription_test_setup();
    const char *topic = "test/topic";
    uint16_t topic_len = (uint16_t)strlen(topic);

    TEST_ASSERT_FALSE(osal_mqtt_subscription_add(NULL, topic_len, NULL, test_sub_callback, QoS0, NULL));
    TEST_ASSERT_FALSE(osal_mqtt_subscription_add(topic, 0, NULL, test_sub_callback, QoS0, NULL));
    TEST_ASSERT_FALSE(osal_mqtt_subscription_add(topic, topic_len, NULL, NULL, QoS0, NULL));
}

void test_mqtt_subscription_handle_publish_invokes_callback(void)
{
    subscription_test_setup();
    const char *topic = "a/b/c";
    uint16_t topic_len = (uint16_t)strlen(topic);
    const char *payload = "hello";
    size_t payload_len = strlen(payload);

    TEST_ASSERT_TRUE(osal_mqtt_subscription_add(topic, topic_len, NULL, test_sub_callback, QoS0, (void *)0x1234));
    bool handled = osal_mqtt_subscription_handle_publish(topic, topic_len, (void *)payload, payload_len);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL(1, s_callback_invoked_count);
    TEST_ASSERT_EQUAL_STRING_LEN(topic, s_last_topic, topic_len);
    TEST_ASSERT_EQUAL_PTR(payload, s_last_payload);
    TEST_ASSERT_EQUAL(payload_len, s_last_payload_len);
    TEST_ASSERT_EQUAL_PTR((void *)0x1234, s_last_priv_data);

    osal_mqtt_subscription_remove(topic, topic_len);
}

void test_mqtt_subscription_handle_publish_wildcard(void)
{
    subscription_test_setup();
    const char *filter = "sensor/+/temp";
    uint16_t filter_len = (uint16_t)strlen(filter);
    const char *topic = "sensor/1/temp";
    size_t topic_len = strlen(topic);
    const char *payload = "22";
    size_t payload_len = strlen(payload);

    TEST_ASSERT_TRUE(osal_mqtt_subscription_add(filter, filter_len, NULL, test_sub_callback, QoS0, NULL));
    bool handled = osal_mqtt_subscription_handle_publish(topic, topic_len, (void *)payload, payload_len);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL(1, s_callback_invoked_count);
    TEST_ASSERT_EQUAL_STRING_LEN(topic, s_last_topic, topic_len);

    osal_mqtt_subscription_remove(filter, filter_len);
}

void test_mqtt_subscription_handle_publish_invalid_params(void)
{
    subscription_test_setup();
    const char *topic = "a/b";
    size_t topic_len = strlen(topic);
    const char *payload = "x";
    size_t payload_len = 1;

    TEST_ASSERT_FALSE(osal_mqtt_subscription_handle_publish(NULL, topic_len, (void *)payload, payload_len));
    TEST_ASSERT_FALSE(osal_mqtt_subscription_handle_publish(topic, topic_len, NULL, payload_len));
    TEST_ASSERT_FALSE(osal_mqtt_subscription_handle_publish(topic, topic_len, (void *)payload, 0));
}

void test_mqtt_subscription_duplicate_same_callback_not_added_twice(void)
{
    subscription_test_setup();
    const char *topic = "dup/topic";
    uint16_t topic_len = (uint16_t)strlen(topic);
    void *priv = (void *)0xABCD;

    TEST_ASSERT_TRUE(osal_mqtt_subscription_add(topic, topic_len, NULL, test_sub_callback, QoS0, priv));
    /* Same topic + same callback + same priv_data: should report success but not add another slot */
    TEST_ASSERT_TRUE(osal_mqtt_subscription_add(topic, topic_len, NULL, test_sub_callback, QoS0, priv));

    unsigned int count_before = 0;
    osal_mqtt_subscription_element_t *list = osal_mqtt_subscription_get_list();
    for (int i = 0; i < OSAL_MQTT_MAX_SUBSCRIPTIONS; i++) {
        if (list[i].usFilterStringLength > 0) {
            count_before++;
        }
    }
    TEST_ASSERT_EQUAL(1, count_before);

    osal_mqtt_subscription_remove(topic, topic_len);
}

static unsigned int s_resubscribe_count;

static osal_err_t mock_subscribe_fn(osal_mqtt_event_loop_channel_t *ch,
                                    const char *t, size_t t_len,
                                    osal_mqtt_subscribe_cb_t cb,
                                    osal_mqtt_QoS_t qos,
                                    void *priv)
{
    (void)ch;
    (void)t;
    (void)t_len;
    (void)cb;
    (void)qos;
    (void)priv;
    s_resubscribe_count++;
    return OSAL_ERR_OK;
}

void test_mqtt_subscription_attempt_resubscribe_all_called(void)
{
    subscription_test_setup();
    const char *topic = "resub/topic";
    uint16_t topic_len = (uint16_t)strlen(topic);

    s_resubscribe_count = 0;
    TEST_ASSERT_TRUE(osal_mqtt_subscription_add(topic, topic_len, NULL, test_sub_callback, QoS1, NULL));

    osal_mqtt_subscription_attempt_resubscribe_all(mock_subscribe_fn);
    TEST_ASSERT_EQUAL(1, s_resubscribe_count);

    osal_mqtt_subscription_remove(topic, topic_len);
}

static unsigned int s_suback_count;

static void mock_suback_fn(osal_mqtt_event_loop_channel_t *ch)
{
    (void)ch;
    s_suback_count++;
}

void test_mqtt_subscription_simulate_subacks_skips_qos0(void)
{
    subscription_test_setup();
    const char *topic = "suback/topic";
    uint16_t topic_len = (uint16_t)strlen(topic);

    s_suback_count = 0;
    TEST_ASSERT_TRUE(osal_mqtt_subscription_add(topic, topic_len, NULL, test_sub_callback, QoS0, NULL));

    osal_mqtt_subscription_simulate_subacks(mock_suback_fn);
    /* QoS0 should not be simulated */
    TEST_ASSERT_EQUAL(0, s_suback_count);

    osal_mqtt_subscription_remove(topic, topic_len);

    /* Add QoS1 and simulate again */
    s_suback_count = 0;
    TEST_ASSERT_TRUE(osal_mqtt_subscription_add(topic, topic_len, NULL, test_sub_callback, QoS1, NULL));
    osal_mqtt_subscription_simulate_subacks(mock_suback_fn);
    TEST_ASSERT_EQUAL(1, s_suback_count);

    osal_mqtt_subscription_remove(topic, topic_len);
}
