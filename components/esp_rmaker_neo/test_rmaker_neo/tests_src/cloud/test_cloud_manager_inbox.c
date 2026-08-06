/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_cloud_manager_inbox.c
 * @brief Ownership tests for set-response callback contexts in the cloud manager inbox.
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "network/cloud/cloud_manager.c"

static int s_cb_calls;
static bool s_cb_success;
static bool s_respond_during_publish;
static osal_err_t s_publish_ret;

static void __test_set_response_cb(esp_rmaker_cloud_event_set_response_t *p_response, void *priv_data)
{
    s_cb_calls++;
    s_cb_success = p_response->success;
    free(priv_data);
}

static esp_rmaker_cloud_event_set_response_cb_context_t *__new_cb_ctx(void)
{
    esp_rmaker_cloud_event_set_response_cb_context_t *p_cb_ctx = calloc(1, sizeof(*p_cb_ctx));
    TEST_ASSERT_NOT_NULL(p_cb_ctx);
    p_cb_ctx->cb = __test_set_response_cb;
    p_cb_ctx->priv_data = malloc(16);
    TEST_ASSERT_NOT_NULL(p_cb_ctx->priv_data);
    return p_cb_ctx;
}

static void __deliver_response(const char *json)
{
    char event_name[] = "setNodeConfig";
    jparse_ctx_t jctx;
    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(0, json_obj_get_object(&jctx, event_name));
    __set_event_response_payload_handler(&esp_rmaker_topic_ctx_self, event_name, &jctx);
    json_parse_end(&jctx);
}

static osal_err_t __fake_publish(osal_mqtt_event_loop_channel_t *channel, const char *topic, size_t topic_len,
                                 void *data, size_t data_len, osal_mqtt_QoS_t qos, bool retain)
{
    (void)channel; (void)topic; (void)topic_len; (void)data; (void)data_len; (void)qos; (void)retain;
    if (s_respond_during_publish) {
        __deliver_response("{\"setNodeConfig\":{\"status\":\"success\"}}");
    }
    return s_publish_ret;
}

static esp_rmaker_error_t __send(esp_rmaker_cloud_event_set_response_cb_context_t *p_cb_ctx)
{
    static char node_config[] = "{}";
    esp_rmaker_cloud_event_t event;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_cloud_event_setNodeConfig(&event, node_config, p_cb_ctx));
    return esp_rmaker_cloud_manager_send(&esp_rmaker_topic_ctx_self, &event, 1, 0);
}

static osal_mqtt_impl_t s_saved_impl;

static void __setup(osal_err_t publish_ret, bool respond_during_publish)
{
    s_cb_calls = 0;
    s_cb_success = false;
    s_publish_ret = publish_ret;
    s_respond_during_publish = respond_during_publish;
    s_saved_impl = esp_rmaker_mqtt_impl;
    esp_rmaker_mqtt_impl.publish = __fake_publish;

    inbox.num_entries = 1;
    inbox.entries = calloc(inbox.num_entries, sizeof(esp_rmaker_cloud_inbox_entry_t));
    TEST_ASSERT_NOT_NULL(inbox.entries);
    inbox.mutex = osal_semaphore_create_mutex();
    TEST_ASSERT_NOT_NULL(inbox.mutex);
}

static void __teardown(void)
{
    for (size_t i = 0; i < inbox.num_entries; i++) {
        __inbox_drop_context(inbox.entries[i].p_set_response_cb_context);
    }
    free(inbox.entries);
    inbox.entries = NULL;
    inbox.num_entries = 0;
    osal_semaphore_delete(inbox.mutex);
    inbox.mutex = NULL;
    esp_rmaker_mqtt_impl = s_saved_impl;
}

void test_cloud_manager_failed_send_releases_context(void)
{
    __setup(OSAL_ERR_MQTT_NOT_CONNECTED, false);

    TEST_ASSERT_NOT_EQUAL(ESP_RMAKER_OK, __send(__new_cb_ctx()));
    TEST_ASSERT_NULL(inbox.entries[0].p_set_response_cb_context);

    /* A late response must not reach the freed context. */
    __deliver_response("{\"setNodeConfig\":{\"status\":\"success\"}}");
    TEST_ASSERT_EQUAL_INT(0, s_cb_calls);

    __teardown();
}

void test_cloud_manager_newer_send_supersedes_context(void)
{
    __setup(OSAL_ERR_OK, false);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __send(__new_cb_ctx()));
    esp_rmaker_cloud_event_set_response_cb_context_t *p_second = __new_cb_ctx();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __send(p_second));
    TEST_ASSERT_EQUAL_PTR(p_second, inbox.entries[0].p_set_response_cb_context);

    __deliver_response("{\"setNodeConfig\":{\"status\":\"success\"}}");
    TEST_ASSERT_EQUAL_INT(1, s_cb_calls);
    TEST_ASSERT_TRUE(s_cb_success);
    TEST_ASSERT_NULL(inbox.entries[0].p_set_response_cb_context);

    __teardown();
}

void test_cloud_manager_response_during_failed_send(void)
{
    /* The response takes the context before the failed publish returns; it must be freed once. */
    __setup(OSAL_ERR_MQTT_NOT_CONNECTED, true);

    TEST_ASSERT_NOT_EQUAL(ESP_RMAKER_OK, __send(__new_cb_ctx()));
    TEST_ASSERT_EQUAL_INT(1, s_cb_calls);
    TEST_ASSERT_NULL(inbox.entries[0].p_set_response_cb_context);

    __teardown();
}

void test_cloud_manager_response_without_status_reports_failure(void)
{
    __setup(OSAL_ERR_OK, false);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __send(__new_cb_ctx()));
    __deliver_response("{\"setNodeConfig\":{\"message\":\"no status\"}}");
    TEST_ASSERT_EQUAL_INT(1, s_cb_calls);
    TEST_ASSERT_FALSE(s_cb_success);

    __teardown();
}
