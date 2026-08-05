/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_mqtt_basic.c
 * @brief Test basic MQTT functionality
 */

#include "unity.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "osal_event_group.h"
#include "osal_event_loop.h"
#include "osal_mem_alloc.h"
#include "osal_task.h"
#include "osal_ticks.h"

#include "osal_mqtt_impl.h"
#include "test_mqtt_common_config.h"

#define TIMEOUT_MS_CONNECT 3000
#define TIMEOUT_MS_ACTION 1000
#define TIMEOUT_MS_LWT_DELIVERY 10000

static const char *TEST_MQTT_TOPIC = "esp/rmng/test/sub_pub_unsub_pub";
static char *TEST_MQTT_PAYLOAD = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Mauris nisi felis, blandit vitae molestie at, tincidunt quis ex.";

static const char *TEST_MQTT_RETAIN_TOPIC = "esp/rmng/test/retain";
static const char *TEST_MQTT_RETAIN_PAYLOAD = "retained-payload-content";

#ifndef CONFIG_OSAL_MQTT_IMPL_ESP
static const char *TEST_MQTT_LWT_TOPIC = "esp/rmng/test/lwt";
static const char *TEST_MQTT_LWT_PAYLOAD = "goodbye-cruel-world";
/* The two LWT phases must look like two distinct clients to the broker. The
 * impl fills conn_params->client_id in on init when it is NULL, and the same
 * conn_params is reused for phase 2 - without these, phase 2 would reconnect
 * under phase 1's client ID and race the broker's teardown of that session.
 *
 * Both LWT tests run through __test_mqtt_lwt(), so the IDs also carry the auth
 * mode: otherwise the second test would reconnect as "victim" while the broker
 * may still be reaping the first test's session - the same shape as the bug
 * these IDs fix. File-scope storage, so the pointer handed to conn_params stays
 * valid past __teardown() (which frees the struct, never the ID). */
static char TEST_MQTT_LWT_CLIENT_ID_VICTIM[48];
static char TEST_MQTT_LWT_CLIENT_ID_OBSERVER[48];
/* Phase-2 subscribe retries: the broker may still be reaping the dropped
 * phase-1 socket, in which case subscribe() fails outright (it does not time
 * out), so a longer timeout would not help - only a retry does. */
#define LWT_SUBSCRIBE_RETRIES 5
#define LWT_SUBSCRIBE_RETRY_DELAY_MS 300
#endif

static OSAL_EVENT_DEFINE_BASE(TEST_MQTT_COMMON_EVENT_BASE);

typedef enum {
    TEST_MQTT_COMMON_EVENT_CONNECTED = 0,
    TEST_MQTT_COMMON_EVENT_DISCONNECTED = 1,
    TEST_MQTT_COMMON_EVENT_PUBLISHED = 2,
    TEST_MQTT_COMMON_EVENT_SUBSCRIBED = 3,
    TEST_MQTT_COMMON_EVENT_UNSUBSCRIBED = 4,
} __event_id_t;

static osal_mqtt_event_loop_registration_info_t __event_loop_info = {
    .event_base = TEST_MQTT_COMMON_EVENT_BASE,
    .event_ids = {
        .connected = TEST_MQTT_COMMON_EVENT_CONNECTED,
        .disconnected = TEST_MQTT_COMMON_EVENT_DISCONNECTED,
        .published = TEST_MQTT_COMMON_EVENT_PUBLISHED,
        .subscribed = TEST_MQTT_COMMON_EVENT_SUBSCRIBED,
        .unsubscribed = TEST_MQTT_COMMON_EVENT_UNSUBSCRIBED,
    }
};

/* Signalling event group and flags */
static osal_event_group_handle_t __event_group;
#define __MQTT_COMMON_SIGNAL_CONNECTED (1 << 0) /**< Connected to MQTT broker */
#define __MQTT_COMMON_SIGNAL_DISCONNECTED (1 << 1) /**< Disconnected from MQTT broker */
#define __MQTT_COMMON_SIGNAL_SUBSCRIBED (1 << 2) /**< Subscribed to topic */
#define __MQTT_COMMON_SIGNAL_UNSUBSCRIBED (1 << 3) /**< Unsubscribed from topic */
#define __MQTT_COMMON_SIGNAL_PUBLISHED (1 << 4) /**< Published message */
#define __MQTT_COMMON_SIGNAL_RECEIVED (1 << 5) /**< Received message */
#define __mqtt_common_wait_for_signal(signal, timeout_ms) (osal_event_group_wait_bits(__event_group, (signal), true, true, osal_ticks_from_ms(timeout_ms)) & (signal))

/* Certificates and keys */
extern const uint8_t __client_cert_start[] asm("_binary_test_mqtt_common_client_crt_start");
extern const uint8_t __client_cert_end[] asm("_binary_test_mqtt_common_client_crt_end");
extern const uint8_t __client_key_start[] asm("_binary_test_mqtt_common_client_key_start");
extern const uint8_t __client_key_end[] asm("_binary_test_mqtt_common_client_key_end");

/* Get connection parameters */
static osal_mqtt_conn_params_t *__get_mqtt_conn_params(bool include_client)
{
    osal_mqtt_conn_params_t *conn_params = (osal_mqtt_conn_params_t *)OSAL_CALLOC_EXTRAM(1, sizeof(osal_mqtt_conn_params_t));
    if (!conn_params) {
        return NULL;
    }

    conn_params->hostname = TEST_MQTT_COMMON_BROKER_URI;
    conn_params->port = !include_client ? TEST_MQTT_COMMON_PORT_TLS_SERVER_ONLY : TEST_MQTT_COMMON_PORT_TLS_MUTUAL_AUTH;

    if (include_client) {
        conn_params->client_cert = (char *)__client_cert_start;
        conn_params->client_cert_len = __client_cert_end - __client_cert_start;
        conn_params->client_key = (char *)__client_key_start;
        conn_params->client_key_len = __client_key_end - __client_key_start;
    }

    return conn_params;
}

/* Free connection parameters */
static void __free_mqtt_conn_params(osal_mqtt_conn_params_t *conn_params)
{
    free(conn_params);
}

/* Event handler */
static void __mqtt_common_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)event_data;
    (void)event_handler_arg;
    (void)event_base;
    switch ((__event_id_t) event_id) {
    case TEST_MQTT_COMMON_EVENT_CONNECTED:
        osal_event_group_set_bits(__event_group, __MQTT_COMMON_SIGNAL_CONNECTED);
        break;
    case TEST_MQTT_COMMON_EVENT_DISCONNECTED:
        osal_event_group_set_bits(__event_group, __MQTT_COMMON_SIGNAL_DISCONNECTED);
        break;
    case TEST_MQTT_COMMON_EVENT_SUBSCRIBED:
        osal_event_group_set_bits(__event_group, __MQTT_COMMON_SIGNAL_SUBSCRIBED);
        break;
    case TEST_MQTT_COMMON_EVENT_UNSUBSCRIBED:
        osal_event_group_set_bits(__event_group, __MQTT_COMMON_SIGNAL_UNSUBSCRIBED);
        break;
    case TEST_MQTT_COMMON_EVENT_PUBLISHED:
        osal_event_group_set_bits(__event_group, __MQTT_COMMON_SIGNAL_PUBLISHED);
        break;
    default:
        break;
    }
}

/* Setup connection parameters */
osal_mqtt_conn_params_t *__setup(bool include_client)
{
    __event_group = osal_event_group_create();
    TEST_ASSERT_NOT_NULL_MESSAGE(__event_group, "Failed to create event group");
    osal_mqtt_conn_params_t *conn_params = __get_mqtt_conn_params(include_client);
    TEST_ASSERT_NOT_NULL_MESSAGE(conn_params, "Failed to get MQTT connection parameters");

    osal_event_loop_create_default();
    osal_event_handler_register(TEST_MQTT_COMMON_EVENT_BASE, OSAL_EVENT_ID_ANY, __mqtt_common_event_handler, NULL);
    return conn_params;
}

/* Teardown connection parameters */
void __teardown(osal_mqtt_conn_params_t *conn_params)
{
    osal_event_handler_unregister(TEST_MQTT_COMMON_EVENT_BASE, OSAL_EVENT_ID_ANY, __mqtt_common_event_handler);
    if (conn_params) {
        __free_mqtt_conn_params(conn_params);
    }
    osal_event_group_delete(__event_group);
}

static void __mqtt_common_on_message_cb(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data)
{
    char **payload_save_ptr = (char **)priv_data;
    *payload_save_ptr = (char *)OSAL_CALLOC_EXTRAM(payload_len + 1, sizeof(char));
    if (!*payload_save_ptr) {
        return;
    }
    memcpy(*payload_save_ptr, payload, payload_len);
    (*payload_save_ptr)[payload_len] = '\0';
    osal_event_group_set_bits(__event_group, __MQTT_COMMON_SIGNAL_RECEIVED);
}

static void __test_mqtt_sub_pub_unsub_pub(osal_mqtt_conn_params_t *conn_params)
{
    char *error_msg = NULL;
    osal_err_t status;
    osal_event_group_bits_t waited_bits;

    /* Setup MQTT implementation */
    osal_mqtt_impl_t mqtt_impl;
    status = osal_mqtt_impl_setup(&mqtt_impl);
    TEST_ASSERT_EQUAL_MESSAGE(OSAL_ERR_OK, status, "Failed to setup MQTT implementation");
    status = mqtt_impl.init(conn_params, &__event_loop_info);
    TEST_ASSERT_EQUAL_MESSAGE(OSAL_ERR_OK, status, "Failed to initialize MQTT");

    /* Connect to MQTT broker */
    status = mqtt_impl.connect();
    if (status != OSAL_ERR_OK) {
        error_msg = "Failed to connect to MQTT broker";
        goto sub_pub_unsub_pub_end;
    }

    /* Wait for connection to complete */
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_CONNECTED, TIMEOUT_MS_CONNECT);
    if (waited_bits != __MQTT_COMMON_SIGNAL_CONNECTED) {
        error_msg = "Connection failed or timed out";
        goto sub_pub_unsub_pub_end;
    }

    /* Subscribe to topic */
    char *payload_save_ptr = NULL;
    status = mqtt_impl.subscribe(NULL, TEST_MQTT_TOPIC, strlen(TEST_MQTT_TOPIC), __mqtt_common_on_message_cb, QoS1, (void *)&payload_save_ptr);
    if (status != OSAL_ERR_OK) {
        error_msg = "Failed to subscribe to topic";
        goto sub_pub_unsub_pub_end;
    }

    /* Wait for subscription to complete */
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_SUBSCRIBED, TIMEOUT_MS_ACTION);
    if (waited_bits != __MQTT_COMMON_SIGNAL_SUBSCRIBED) {
        error_msg = "Subscription failed or timed out";
        goto sub_pub_unsub_pub_end;
    }

    /* Publish message */
    status = mqtt_impl.publish(NULL, TEST_MQTT_TOPIC, strlen(TEST_MQTT_TOPIC), (void *)TEST_MQTT_PAYLOAD, strlen(TEST_MQTT_PAYLOAD), QoS1, false);
    if (status != OSAL_ERR_OK) {
        error_msg = "Failed to publish message";
        goto sub_pub_unsub_pub_end;
    }

    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_PUBLISHED | __MQTT_COMMON_SIGNAL_RECEIVED, TIMEOUT_MS_ACTION);
    if (waited_bits != (__MQTT_COMMON_SIGNAL_PUBLISHED | __MQTT_COMMON_SIGNAL_RECEIVED)) {
        error_msg = "Publish or message reception failed or timed out";
        goto sub_pub_unsub_pub_end;
    }

    /* Verify that the message was received */
    if (strcmp(TEST_MQTT_PAYLOAD, payload_save_ptr) != 0) {
        error_msg = "Received message does not match published message";
        goto sub_pub_unsub_pub_end;
    }
    free(payload_save_ptr);
    payload_save_ptr = NULL;

    /* Unsubscribe from topic */
    status = mqtt_impl.unsubscribe(NULL, TEST_MQTT_TOPIC, strlen(TEST_MQTT_TOPIC), QoS1);
    if (status != OSAL_ERR_OK) {
        error_msg = "Failed to unsubscribe from topic";
        goto sub_pub_unsub_pub_end;
    }

    /* Wait for unsubscription to complete */
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_UNSUBSCRIBED, TIMEOUT_MS_ACTION);
    if (waited_bits != __MQTT_COMMON_SIGNAL_UNSUBSCRIBED) {
        error_msg = "Unsubscription failed or timed out";
        goto sub_pub_unsub_pub_end;
    }

    /* Publish again to verify that the message is not received */
    status = mqtt_impl.publish(NULL, TEST_MQTT_TOPIC, strlen(TEST_MQTT_TOPIC), TEST_MQTT_PAYLOAD, strlen(TEST_MQTT_PAYLOAD), QoS1, false);
    if (status != OSAL_ERR_OK) {
        error_msg = "Failed to publish message";
        goto sub_pub_unsub_pub_end;
    }

    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_PUBLISHED, TIMEOUT_MS_ACTION);
    if (waited_bits != __MQTT_COMMON_SIGNAL_PUBLISHED) {
        error_msg = "Publish failed or timed out";
        goto sub_pub_unsub_pub_end;
    }

    /* Wait for message reception to complete */
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_RECEIVED, TIMEOUT_MS_ACTION);
    if (waited_bits != 0) {
        error_msg = "Expected no receive signal";
        goto sub_pub_unsub_pub_end;
    }

    /* Verify that the message was not received */
    if (payload_save_ptr != NULL) {
        error_msg = "Received message after unsubscription";
        goto sub_pub_unsub_pub_end;
    }

sub_pub_unsub_pub_end:
    /* Disconnect from MQTT broker */
    status = mqtt_impl.disconnect();
    if (status != OSAL_ERR_OK) {
        error_msg = "Failed to disconnect from MQTT broker";
        goto sub_pub_unsub_pub_eval;
    }

    /* Wait for disconnection to complete */
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_DISCONNECTED, TIMEOUT_MS_CONNECT);
    if (waited_bits != __MQTT_COMMON_SIGNAL_DISCONNECTED) {
        error_msg = "Disconnection failed or timed out";
        goto sub_pub_unsub_pub_eval;
    }

    /* Deinitialize MQTT */
    status = mqtt_impl.deinit();
    if (status != OSAL_ERR_OK) {
        error_msg = "Failed to deinitialize MQTT";
    }

sub_pub_unsub_pub_eval:
    if (error_msg) {
        TEST_FAIL_MESSAGE(error_msg);
    } else {
        TEST_PASS();
    }
}

void test_mqtt_tls_server_only_sub_pub_unsub_pub(void)
{
    osal_mqtt_conn_params_t *conn_params = __setup(false);
    __test_mqtt_sub_pub_unsub_pub(conn_params);
    __teardown(conn_params);
}
void test_mqtt_tls_mutual_auth_sub_pub_unsub_pub(void)
{
    osal_mqtt_conn_params_t *conn_params = __setup(true);
    __test_mqtt_sub_pub_unsub_pub(conn_params);
    __teardown(conn_params);
}

/* ---------------- Retain test ---------------------------------------------
 * Single wrapper client. Publishes with retain=true, subscribes afterwards,
 * and verifies the broker delivers the retained message on SUBSCRIBE. Cleans
 * up the retained message at the end by publishing an empty retained payload.
 * ------------------------------------------------------------------------ */

static void __test_mqtt_retain(osal_mqtt_conn_params_t *conn_params)
{
    char *error_msg = NULL;
    osal_err_t status;
    osal_event_group_bits_t waited_bits;
    char *payload_save_ptr = NULL;
    bool inited = false, connected = false, subscribed = false;

    /* Clean session avoids carrying over subscription state between runs. */
    conn_params->clean_session = true;

    osal_mqtt_impl_t mqtt_impl;
    status = osal_mqtt_impl_setup(&mqtt_impl);
    TEST_ASSERT_EQUAL_MESSAGE(OSAL_ERR_OK, status, "Failed to setup MQTT implementation");
    status = mqtt_impl.init(conn_params, &__event_loop_info);
    TEST_ASSERT_EQUAL_MESSAGE(OSAL_ERR_OK, status, "Failed to initialize MQTT");
    inited = true;

    status = mqtt_impl.connect();
    if (status != OSAL_ERR_OK) {
        error_msg = "Failed to connect to broker";
        goto retain_end;
    }
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_CONNECTED, TIMEOUT_MS_CONNECT);
    if (waited_bits != __MQTT_COMMON_SIGNAL_CONNECTED) {
        error_msg = "Connect timed out";
        goto retain_end;
    }
    connected = true;

    /* Defensive: clear any pre-existing retained message on the topic. */
    status = mqtt_impl.publish(NULL, TEST_MQTT_RETAIN_TOPIC, strlen(TEST_MQTT_RETAIN_TOPIC),
                               NULL, 0, QoS1, true);
    if (status != OSAL_ERR_OK) {
        error_msg = "Clear-retained publish failed";
        goto retain_end;
    }
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_PUBLISHED, TIMEOUT_MS_ACTION);
    if (waited_bits != __MQTT_COMMON_SIGNAL_PUBLISHED) {
        error_msg = "Clear-retained publish timed out";
        goto retain_end;
    }

    /* Publish the retained message that we expect to receive on subscribe. */
    status = mqtt_impl.publish(NULL, TEST_MQTT_RETAIN_TOPIC, strlen(TEST_MQTT_RETAIN_TOPIC),
                               (void *)TEST_MQTT_RETAIN_PAYLOAD, strlen(TEST_MQTT_RETAIN_PAYLOAD),
                               QoS1, true);
    if (status != OSAL_ERR_OK) {
        error_msg = "Retained publish failed";
        goto retain_end;
    }
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_PUBLISHED, TIMEOUT_MS_ACTION);
    if (waited_bits != __MQTT_COMMON_SIGNAL_PUBLISHED) {
        error_msg = "Retained publish timed out";
        goto retain_end;
    }

    /* Subscribe - broker must push the retained message back to us. */
    status = mqtt_impl.subscribe(NULL, TEST_MQTT_RETAIN_TOPIC, strlen(TEST_MQTT_RETAIN_TOPIC),
                                 __mqtt_common_on_message_cb, QoS1, (void *)&payload_save_ptr);
    if (status != OSAL_ERR_OK) {
        error_msg = "Subscribe failed";
        goto retain_end;
    }
    subscribed = true;
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_SUBSCRIBED | __MQTT_COMMON_SIGNAL_RECEIVED,
                  TIMEOUT_MS_ACTION);
    if (waited_bits != (__MQTT_COMMON_SIGNAL_SUBSCRIBED | __MQTT_COMMON_SIGNAL_RECEIVED)) {
        error_msg = "Subscribe or retained delivery timed out";
        goto retain_end;
    }

    if (payload_save_ptr == NULL ||
            strcmp(TEST_MQTT_RETAIN_PAYLOAD, payload_save_ptr) != 0) {
        error_msg = "Retained payload mismatch";
        goto retain_end;
    }

retain_end:
    if (payload_save_ptr) {
        free(payload_save_ptr);
        payload_save_ptr = NULL;
    }

    if (connected) {
        /* Clear retained message so re-runs start clean. */
        (void) mqtt_impl.publish(NULL, TEST_MQTT_RETAIN_TOPIC, strlen(TEST_MQTT_RETAIN_TOPIC),
                                 NULL, 0, QoS1, true);
        (void) __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_PUBLISHED, TIMEOUT_MS_ACTION);

        if (subscribed) {
            (void) mqtt_impl.unsubscribe(NULL, TEST_MQTT_RETAIN_TOPIC, strlen(TEST_MQTT_RETAIN_TOPIC), QoS1);
            (void) __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_UNSUBSCRIBED, TIMEOUT_MS_ACTION);
        }

        (void) mqtt_impl.disconnect();
        (void) __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_DISCONNECTED, TIMEOUT_MS_CONNECT);
    }
    if (inited) {
        (void) mqtt_impl.deinit();
    }

    if (error_msg) {
        TEST_FAIL_MESSAGE(error_msg);
    } else {
        TEST_PASS();
    }
}

void test_mqtt_tls_server_only_retain(void)
{
    osal_mqtt_conn_params_t *conn_params = __setup(false);
    __test_mqtt_retain(conn_params);
    __teardown(conn_params);
}

void test_mqtt_tls_mutual_auth_retain(void)
{
    osal_mqtt_conn_params_t *conn_params = __setup(true);
    __test_mqtt_retain(conn_params);
    __teardown(conn_params);
}

/* ---------------- LWT test (two sequential wrapper clients) ---------------
 * The mqtt-common wrapper keeps file-static client state per impl, so two
 * wrapper instances cannot coexist in one process. The test runs two wrapper
 * clients *sequentially* and relays the LWT payload between them via the
 * broker's retained-message mechanism:
 *
 *   Client A (LWT owner): connects with LWT configured (retain=true), then
 *     drop()s the connection. The broker sees the ungraceful disconnect and
 *     publishes the LWT to the LWT topic; because retain=true, the broker
 *     keeps it.
 *
 *   Client B (observer): connects on a fresh session, subscribes to the LWT
 *     topic, and must receive the retained LWT payload as SUBACK delivery.
 *
 * drop() is the key primitive that lets us force the ungraceful close
 * without leaving the wrapper API.
 *
 * Only the CoreMQTT impl supports a true ungraceful drop (it tears the TLS
 * transport directly). The esp-mqtt public API has no way to close the TCP
 * connection without first sending an MQTT DISCONNECT packet, so drop() on
 * esp-mqtt falls back to a graceful disconnect and the broker does NOT
 * publish the LWT - this test is therefore TEST_IGNORE'd on esp-mqtt.
 * ------------------------------------------------------------------------ */

static void __test_mqtt_lwt(osal_mqtt_conn_params_t *conn_params, const char *id_suffix)
{
#ifdef CONFIG_OSAL_MQTT_IMPL_ESP
    (void) conn_params;
    (void) id_suffix;
    TEST_IGNORE_MESSAGE(
        "esp-mqtt public API cannot perform an ungraceful close - LWT cannot be tested");
#else
    char *error_msg = NULL;
    osal_err_t status;
    osal_event_group_bits_t waited_bits;
    char *payload_save_ptr = NULL;
    bool inited = false, connected = false, subscribed = false;
    bool phase1_inited = false, phase1_connected = false;

    osal_mqtt_impl_t mqtt_impl;

    snprintf(TEST_MQTT_LWT_CLIENT_ID_VICTIM, sizeof(TEST_MQTT_LWT_CLIENT_ID_VICTIM),
             "rmng-test-lwt-victim-%s", id_suffix);
    snprintf(TEST_MQTT_LWT_CLIENT_ID_OBSERVER, sizeof(TEST_MQTT_LWT_CLIENT_ID_OBSERVER),
             "rmng-test-lwt-observer-%s", id_suffix);

    /* ---------- Phase 1 - Client A: register LWT, then drop ---------- */
    conn_params->client_id       = TEST_MQTT_LWT_CLIENT_ID_VICTIM;
    conn_params->clean_session = true;
    conn_params->lwt.enabled     = true;
    conn_params->lwt.topic       = TEST_MQTT_LWT_TOPIC;
    conn_params->lwt.topic_len   = strlen(TEST_MQTT_LWT_TOPIC);
    conn_params->lwt.payload     = TEST_MQTT_LWT_PAYLOAD;
    conn_params->lwt.payload_len = strlen(TEST_MQTT_LWT_PAYLOAD);
    conn_params->lwt.qos         = QoS1;
    conn_params->lwt.retain      = true;   /* broker retains, so Client B can see */

    status = osal_mqtt_impl_setup(&mqtt_impl);
    TEST_ASSERT_EQUAL_MESSAGE(OSAL_ERR_OK, status, "Failed to setup MQTT implementation (phase 1)");
    status = mqtt_impl.init(conn_params, &__event_loop_info);
    TEST_ASSERT_EQUAL_MESSAGE(OSAL_ERR_OK, status, "Failed to initialize phase-1 client");
    phase1_inited = true;

    status = mqtt_impl.connect();
    if (status != OSAL_ERR_OK) {
        error_msg = "Phase-1 connect failed";
        goto lwt_end;
    }
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_CONNECTED, TIMEOUT_MS_CONNECT);
    if (waited_bits != __MQTT_COMMON_SIGNAL_CONNECTED) {
        error_msg = "Phase-1 connect timed out";
        goto lwt_end;
    }
    phase1_connected = true;

    /* Drop: tear TCP without MQTT DISCONNECT, so the broker fires the LWT. */
    TEST_ASSERT_NOT_NULL_MESSAGE(mqtt_impl.drop, "drop() not wired on impl");
    status = mqtt_impl.drop();
    if (status != OSAL_ERR_OK) {
        error_msg = "drop() returned error";
        goto lwt_end;
    }
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_DISCONNECTED, TIMEOUT_MS_CONNECT);
    if (waited_bits != __MQTT_COMMON_SIGNAL_DISCONNECTED) {
        error_msg = "Phase-1 drop did not signal disconnected";
        goto lwt_end;
    }

    status = mqtt_impl.deinit();
    if (status != OSAL_ERR_OK) {
        error_msg = "Phase-1 deinit failed";
        goto lwt_end;
    }
    phase1_inited = false;

    /* ---------- Phase 2 - Client B: subscribe and receive retained LWT ---------- */
    conn_params->client_id   = TEST_MQTT_LWT_CLIENT_ID_OBSERVER;
    conn_params->lwt.enabled = false;   /* observer carries no LWT */
    conn_params->lwt.topic       = NULL;
    conn_params->lwt.topic_len   = 0;
    conn_params->lwt.payload     = NULL;
    conn_params->lwt.payload_len = 0;

    status = osal_mqtt_impl_setup(&mqtt_impl);
    TEST_ASSERT_EQUAL_MESSAGE(OSAL_ERR_OK, status, "Failed to setup MQTT implementation (phase 2)");
    status = mqtt_impl.init(conn_params, &__event_loop_info);
    TEST_ASSERT_EQUAL_MESSAGE(OSAL_ERR_OK, status, "Failed to initialize phase-2 client");
    inited = true;

    status = mqtt_impl.connect();
    if (status != OSAL_ERR_OK) {
        error_msg = "Phase-2 connect failed";
        goto lwt_end;
    }
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_CONNECTED, TIMEOUT_MS_CONNECT);
    if (waited_bits != __MQTT_COMMON_SIGNAL_CONNECTED) {
        error_msg = "Phase-2 connect timed out";
        goto lwt_end;
    }
    connected = true;

    for (int attempt = 0; attempt < LWT_SUBSCRIBE_RETRIES; attempt++) {
        status = mqtt_impl.subscribe(NULL, TEST_MQTT_LWT_TOPIC, strlen(TEST_MQTT_LWT_TOPIC),
                                     __mqtt_common_on_message_cb, QoS1, (void *)&payload_save_ptr);
        if (status == OSAL_ERR_OK) {
            break;
        }
        osal_task_delay(osal_ticks_from_ms(LWT_SUBSCRIBE_RETRY_DELAY_MS));
    }
    if (status != OSAL_ERR_OK) {
        error_msg = "Phase-2 subscribe failed";
        goto lwt_end;
    }
    subscribed = true;

    /* Broker should push the retained LWT payload on SUBACK. */
    waited_bits = __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_SUBSCRIBED | __MQTT_COMMON_SIGNAL_RECEIVED,
                  TIMEOUT_MS_LWT_DELIVERY);
    if (waited_bits != (__MQTT_COMMON_SIGNAL_SUBSCRIBED | __MQTT_COMMON_SIGNAL_RECEIVED)) {
        error_msg = "Did not receive retained LWT";
        goto lwt_end;
    }

    if (payload_save_ptr == NULL ||
            strcmp(TEST_MQTT_LWT_PAYLOAD, payload_save_ptr) != 0) {
        error_msg = "LWT payload mismatch";
        goto lwt_end;
    }

lwt_end:
    if (payload_save_ptr) {
        free(payload_save_ptr);
        payload_save_ptr = NULL;
    }

    /* Clear the retained LWT so re-runs start clean. Use whichever phase's
     * client is still alive - if neither is, skip (non-critical). */
    if (connected) {
        (void) mqtt_impl.publish(NULL, TEST_MQTT_LWT_TOPIC, strlen(TEST_MQTT_LWT_TOPIC),
                                 "", 0, QoS1, true);
        (void) __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_PUBLISHED, TIMEOUT_MS_ACTION);

        if (subscribed) {
            (void) mqtt_impl.unsubscribe(NULL, TEST_MQTT_LWT_TOPIC, strlen(TEST_MQTT_LWT_TOPIC), QoS1);
            (void) __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_UNSUBSCRIBED, TIMEOUT_MS_ACTION);
        }
        (void) mqtt_impl.disconnect();
        (void) __mqtt_common_wait_for_signal(__MQTT_COMMON_SIGNAL_DISCONNECTED, TIMEOUT_MS_CONNECT);
    }
    if (inited) {
        (void) mqtt_impl.deinit();
    }

    /* Phase-1 leftover cleanup if an error interrupted phase 1 before deinit. */
    if (phase1_inited) {
        (void) mqtt_impl.deinit();
    }
    (void) phase1_connected;

    if (error_msg) {
        TEST_FAIL_MESSAGE(error_msg);
    } else {
        TEST_PASS();
    }
#endif /* CONFIG_OSAL_MQTT_IMPL_ESP */
}

void test_mqtt_tls_server_only_lwt(void)
{
    osal_mqtt_conn_params_t *conn_params = __setup(false);
    __test_mqtt_lwt(conn_params, "server-only");
    __teardown(conn_params);
}

void test_mqtt_tls_mutual_auth_lwt(void)
{
    osal_mqtt_conn_params_t *conn_params = __setup(true);
    __test_mqtt_lwt(conn_params, "mutual-auth");
    __teardown(conn_params);
}
