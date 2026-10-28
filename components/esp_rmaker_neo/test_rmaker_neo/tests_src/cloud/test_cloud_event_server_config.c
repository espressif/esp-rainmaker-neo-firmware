/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_cloud_event_server_config.c
 * @brief Tests for the 'getServerConfig' cloud event and esp_rmaker_server_config_get().
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"
#include "sdkconfig.h"

/* The feature, and so its API, exists only with CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG. */
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osal_task.h"
#include "osal_ticks.h"

#include "network/cloud/events.h"
#include "network/cloud/manager_internal.h"
#include "network/cloud/server_config.h"
#include "network/mqtt_topics.h"
#include "network/common.h"
#include "constants/network.h"

/* The request for the keys this build selects, in tree order: {"rmng-base":{"StackRegion":null,...}}. */
static void __expected_request(char *buf, size_t size)
{
    static const char *const names[] = {
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_STACK_REGION
        "StackRegion",
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_CRED_ENDPOINT
        "CredentialProviderEndpoint",
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_VIDEO_ROLE_ALIAS
        "NodeVideoRoleAliases",
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILE_ROLE_ALIAS
        "NodeFileRoleAliases",
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILES_BUCKET_NAME
        "FilesBucketName",
#endif
    };
    int off = snprintf(buf, size, "{\"rmng-base\":{");
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        off += snprintf(buf + off, size - (size_t)off, "%s\"%s\":null", i ? "," : "", names[i]);
        TEST_ASSERT_LESS_THAN((int)size, off);
    }
    off += snprintf(buf + off, size - (size_t)off, "}}");
    TEST_ASSERT_LESS_THAN((int)size, off);
}

/* The cloud's response: the request tree, with each null replaced by the value. */
static const char *SERVER_CONFIG_PAYLOAD =
    "{\"event\":[\"getServerConfig\"],"
    "\"getServerConfig\":{"
    "\"rmng-base\":{"
    "\"StackRegion\":\"us-east-1\","
    "\"CredentialProviderEndpoint\":\"abc123.credentials.iot.us-east-1.amazonaws.com\","
    "\"NodeVideoRoleAliases\":\"rmng-node-video-role-v1\","
    "\"NodeFileRoleAliases\":\"rmng-node-file-role-v1\","
    "\"FilesBucketName\":\"esp-rm-files-123456789012-us-east-1\"}}}";

/* Start each test from an empty cache, also after an earlier test failed before its own deinit. */
static void __init_fresh(void)
{
    esp_rmaker_server_config_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_server_config_init());
}

static void __dispatch(const char *payload)
{
    cloud_manager_internal_dispatch_payload(&esp_rmaker_topic_ctx_self, payload, strlen(payload));
}

static void __assert_value(esp_rmaker_server_config_key_t key, const char *expected)
{
    char *value = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_server_config_get(key, &value, 0));
    TEST_ASSERT_EQUAL_STRING(expected, value);
    free(value);
}

/* Test tree: a.b.c.k2, a.b.k1, a.k3, d.k4 and k5, with value indices 0 to 4. */
static const esp_rmaker_server_config_node_t s_test_c[] = {{.name = "k2", .key = 0}};
static const esp_rmaker_server_config_node_t s_test_b[] = {
    {.name = "c", .children = s_test_c, .num_children = 1},
    {.name = "k1", .key = 1},
};
static const esp_rmaker_server_config_node_t s_test_a[] = {
    {.name = "b", .children = s_test_b, .num_children = 2},
    {.name = "k3", .key = 2},
};
static const esp_rmaker_server_config_node_t s_test_d[] = {{.name = "k4", .key = 3}};
static const esp_rmaker_server_config_node_t s_test_tree[] = {
    {.name = "a", .children = s_test_a, .num_children = 2},
    {.name = "d", .children = s_test_d, .num_children = 1},
    {.name = "k5", .key = 4},
};
#define TEST_TREE_SIZE (sizeof(s_test_tree) / sizeof(s_test_tree[0]))

void test_cloud_event_server_config_builder(void)
{
    /* The event carries the request tree of the selected keys, which the cloud answers key by key. */
    char expected[256];
    __expected_request(expected, sizeof(expected));
    TEST_ASSERT_EQUAL_STRING(expected, esp_rmaker_server_config_request_data());

    esp_rmaker_cloud_event_t event;
    memset(&event, 0xAA, sizeof(event));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_cloud_event_getServerConfig(&event));
    TEST_ASSERT_EQUAL_STRING("getServerConfig", event.name);
    TEST_ASSERT_EQUAL_STRING(expected, event.data);
    TEST_ASSERT_NULL(event.p_set_response_cb_context);

    /* Table order must match the flag enum; a resend of pending events builds the same event. */
    esp_rmaker_cloud_event_t registered = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, RMAKER_CLOUD_EVENT_BUILDERS[RMAKER_CLOUD_EVENT_FLAG_POS_getServerConfig](&registered));
    TEST_ASSERT_EQUAL_STRING("getServerConfig", registered.name);
    TEST_ASSERT_EQUAL_STRING(expected, registered.data);
}

static int __build(const esp_rmaker_server_config_node_t *nodes, size_t num_nodes, char *buf, size_t buf_size)
{
    if (buf) {
        memset(buf, 'x', buf_size);
    }
    return esp_rmaker_server_config_build_request(nodes, num_nodes, buf, buf_size);
}

/* The sizing pass and the build pass agree, and the build gives @p expected. */
static void __assert_build(const esp_rmaker_server_config_node_t *nodes, size_t num_nodes, const char *expected)
{
    int req_size = __build(nodes, num_nodes, NULL, 0);
    TEST_ASSERT_EQUAL((int)strlen(expected) + 1, req_size);
    char *buf = malloc((size_t)req_size);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_EQUAL(req_size, __build(nodes, num_nodes, buf, (size_t)req_size));
    TEST_ASSERT_EQUAL_STRING(expected, buf);
    free(buf);
}

void test_cloud_event_server_config_build_nested(void)
{
    /* Two levels closed between keys, a key next to a level, and a key at the root. */
    __assert_build(s_test_tree, TEST_TREE_SIZE,
                   "{\"a\":{\"b\":{\"c\":{\"k2\":null},\"k1\":null},\"k3\":null},"
                   "\"d\":{\"k4\":null},\"k5\":null}");

    static const esp_rmaker_server_config_node_t empty_level[] = {{.name = "e", .children = s_test_c}};
    __assert_build(empty_level, 1, "{\"e\":{}}");
    __assert_build(NULL, 0, "{}");
}

void test_cloud_event_server_config_build_buffer_size(void)
{
    const char *expected = "{\"a\":{\"b\":{\"c\":{\"k2\":null},\"k1\":null},\"k3\":null}}";
    int req_size = (int)strlen(expected) + 1;
    char buf[64];

    /* A larger buffer is fine. */
    TEST_ASSERT_EQUAL(req_size, __build(s_test_tree, 1, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    /* One byte short, and short inside a level name: the size, and no partial JSON. */
    TEST_ASSERT_EQUAL(req_size, __build(s_test_tree, 1, buf, (size_t)req_size - 1));
    TEST_ASSERT_EQUAL_STRING("", buf);
    TEST_ASSERT_EQUAL(req_size, __build(s_test_tree, 1, buf, 8));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_cloud_event_server_config_build_invalid_args(void)
{
    char buf[8];
    TEST_ASSERT_EQUAL(-1, esp_rmaker_server_config_build_request(NULL, 1, NULL, 0));
    TEST_ASSERT_EQUAL(-1, esp_rmaker_server_config_build_request(s_test_tree, TEST_TREE_SIZE, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL(-1, esp_rmaker_server_config_build_request(s_test_tree, TEST_TREE_SIZE, buf, 0));
}

#define PARSE_MAX_KEYS 8

typedef struct {
    char *values[PARSE_MAX_KEYS];
    int calls;
} parse_result_t;

#define TEST_VALUE_SIZE 16

/* Value buffer size of each test key. */
static size_t s_test_sizes[PARSE_MAX_KEYS] = {
    TEST_VALUE_SIZE, TEST_VALUE_SIZE, TEST_VALUE_SIZE, TEST_VALUE_SIZE,
    TEST_VALUE_SIZE, TEST_VALUE_SIZE, TEST_VALUE_SIZE, TEST_VALUE_SIZE,
};

/* Parse @p json against the test tree, then check that the context is back at the root object. */
static void __parse(const char *json, parse_result_t *res)
{
    static char bufs[PARSE_MAX_KEYS][TEST_VALUE_SIZE];
    esp_rmaker_server_config_value_t values[PARSE_MAX_KEYS];
    for (size_t i = 0; i < PARSE_MAX_KEYS; i++) {
        memset(bufs[i], 'x', sizeof(bufs[i]));
        values[i] = (esp_rmaker_server_config_value_t) {
            .buf = bufs[i], .size = s_test_sizes[i]
        };
    }

    jparse_ctx_t jctx;
    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, (char *)json, (int)strlen(json)));
    esp_rmaker_server_config_parse_tree(&jctx, s_test_tree, TEST_TREE_SIZE, values);
    char root[8] = {0};
    TEST_ASSERT_EQUAL_MESSAGE(0, json_obj_get_string(&jctx, "root", root, sizeof(root)), "context not at root");
    TEST_ASSERT_EQUAL_STRING("yes", root);
    json_parse_end(&jctx);

    memset(res, 0, sizeof(*res));
    for (size_t i = 0; i < PARSE_MAX_KEYS; i++) {
        if (values[i].stored) {
            res->values[i] = strdup(values[i].buf);
            res->calls++;
        }
    }
}

static void __free_result(parse_result_t *res)
{
    for (size_t i = 0; i < PARSE_MAX_KEYS; i++) {
        free(res->values[i]);
    }
}

void test_cloud_event_server_config_parse_nested(void)
{
    parse_result_t res;

    __parse("{\"a\":{\"b\":{\"c\":{\"k2\":\"v2\"},\"k1\":\"v1\"},\"k3\":\"v3\"},"
            "\"d\":{\"k4\":\"v4\"},\"k5\":\"v5\",\"root\":\"yes\"}", &res);
    TEST_ASSERT_EQUAL(5, res.calls);
    TEST_ASSERT_EQUAL_STRING("v2", res.values[0]);
    TEST_ASSERT_EQUAL_STRING("v1", res.values[1]);
    TEST_ASSERT_EQUAL_STRING("v3", res.values[2]);
    TEST_ASSERT_EQUAL_STRING("v4", res.values[3]);
    TEST_ASSERT_EQUAL_STRING("v5", res.values[4]);
    __free_result(&res);

    /* Member order in the response does not matter, and empty strings are values. */
    __parse("{\"root\":\"yes\",\"k5\":\"\",\"d\":{\"k4\":\"v4\"},"
            "\"a\":{\"k3\":\"v3\",\"b\":{\"k1\":\"v1\",\"c\":{\"k2\":\"v2\"}}}}", &res);
    TEST_ASSERT_EQUAL(5, res.calls);
    TEST_ASSERT_EQUAL_STRING("v2", res.values[0]);
    TEST_ASSERT_EQUAL_STRING("", res.values[4]);
    __free_result(&res);
}

void test_cloud_event_server_config_parse_partial(void)
{
    parse_result_t res;

    /* Deepest level missing: the walk leaves the open levels and the next keys still read. */
    __parse("{\"a\":{\"b\":{\"k1\":\"v1\"},\"k3\":\"v3\"},\"d\":{\"k4\":\"v4\"},"
            "\"root\":\"yes\"}", &res);
    TEST_ASSERT_EQUAL(3, res.calls);
    TEST_ASSERT_NULL(res.values[0]);
    TEST_ASSERT_EQUAL_STRING("v1", res.values[1]);
    TEST_ASSERT_EQUAL_STRING("v3", res.values[2]);
    TEST_ASSERT_EQUAL_STRING("v4", res.values[3]);
    TEST_ASSERT_NULL(res.values[4]);
    __free_result(&res);

    /* A level that is not an object, and keys that are null, numbers or objects. */
    __parse("{\"a\":{\"b\":\"str\",\"k3\":null},\"d\":{\"k4\":{\"x\":\"y\"}},\"k5\":5,"
            "\"root\":\"yes\"}", &res);
    TEST_ASSERT_EQUAL(0, res.calls);
    __free_result(&res);

    /* A level that is an array. */
    __parse("{\"a\":[\"b\"],\"d\":{\"k4\":\"v4\"},\"root\":\"yes\"}", &res);
    TEST_ASSERT_EQUAL(1, res.calls);
    TEST_ASSERT_EQUAL_STRING("v4", res.values[3]);
    __free_result(&res);

    /* A missing level: the parser does not read a key of the same name from the parent. */
    __parse("{\"a\":{\"b\":{\"k2\":\"wrong\"},\"k1\":\"wrong\"},\"k4\":\"wrong\",\"root\":\"yes\"}", &res);
    TEST_ASSERT_EQUAL(0, res.calls);
    __free_result(&res);

    /* Nothing requested is present. */
    __parse("{\"root\":\"yes\"}", &res);
    TEST_ASSERT_EQUAL(0, res.calls);
    __free_result(&res);
}

void test_cloud_event_server_config_parse_too_long(void)
{
    parse_result_t res;
    const char *json = "{\"a\":{\"b\":{\"c\":{\"k2\":\"v2\"},\"k1\":\"v1\"},\"k3\":\"v3\"},"
                       "\"d\":{\"k4\":\"v4\"},\"k5\":\"v5\",\"root\":\"yes\"}";

    /* "v1" fits a 3-byte buffer exactly. */
    s_test_sizes[1] = 3;
    __parse(json, &res);
    TEST_ASSERT_EQUAL_STRING("v1", res.values[1]);
    __free_result(&res);

    /* One byte short inside a nested level: dropped, and the other keys still read. */
    s_test_sizes[1] = 2;
    __parse(json, &res);
    s_test_sizes[1] = TEST_VALUE_SIZE;
    TEST_ASSERT_EQUAL(4, res.calls);
    TEST_ASSERT_NULL(res.values[1]);
    TEST_ASSERT_EQUAL_STRING("v2", res.values[0]);
    TEST_ASSERT_EQUAL_STRING("v5", res.values[4]);
    __free_result(&res);
}

void test_cloud_event_server_config_value_limits(void)
{
    char endpoint[200];
    char payload[400];
    memset(endpoint, 'e', sizeof(endpoint) - 1);
    endpoint[sizeof(endpoint) - 1] = '\0';
    snprintf(payload, sizeof(payload), "{\"event\":[\"getServerConfig\"],\"getServerConfig\":{\"rmng-base\":{"
             "\"StackRegion\":\"ap-southeast-5\",\"CredentialProviderEndpoint\":\"%s\"}}}", endpoint);

    __init_fresh();
    __dispatch(payload);

    /* A value too long for its buffer is not found, and the response still counts as received. */
    __assert_value(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, "ap-southeast-5");
    char *value = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND,
                      esp_rmaker_server_config_get(ESP_RMAKER_SERVER_CONFIG_CREDENTIAL_PROVIDER_ENDPOINT, &value, 0));
    TEST_ASSERT_NULL(value);

    esp_rmaker_server_config_deinit();
}

void test_cloud_event_server_config_ignores_unrequested(void)
{
    __init_fresh();

    __dispatch("{\"event\":[\"getServerConfig\"],\"getServerConfig\":{"
               "\"espuser-base\":{\"StackRegion\":\"eu-west-1\"},"
               "\"rmng-base\":{\"IdentityPoolId\":\"x\",\"StackRegion\":\"us-east-1\"}}}");
    __assert_value(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, "us-east-1");

    esp_rmaker_server_config_deinit();
}

void test_cloud_event_server_config_missing_key(void)
{
    __init_fresh();

    __dispatch("{\"event\":[\"getServerConfig\"],\"getServerConfig\":{\"rmng-base\":{"
               "\"StackRegion\":\"us-east-1\",\"NodeVideoRoleAliases\":\"rmng-node-video-role-v1\"}}}");
    __assert_value(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, "us-east-1");
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_VIDEO_ROLE_ALIAS
    __assert_value(ESP_RMAKER_SERVER_CONFIG_NODE_VIDEO_ROLE_ALIASES, "rmng-node-video-role-v1");
#endif

    char *value = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND,
                      esp_rmaker_server_config_get(ESP_RMAKER_SERVER_CONFIG_CREDENTIAL_PROVIDER_ENDPOINT, &value, 0));
    TEST_ASSERT_NULL(value);

    esp_rmaker_server_config_deinit();
}

void test_cloud_event_server_config_non_string_value(void)
{
    __init_fresh();

    __dispatch("{\"event\":[\"getServerConfig\"],\"getServerConfig\":{\"rmng-base\":{"
               "\"StackRegion\":42,\"NodeVideoRoleAliases\":{\"a\":\"b\"},"
               "\"CredentialProviderEndpoint\":\"abc123.credentials.iot.us-east-1.amazonaws.com\"}}}");
    __assert_value(ESP_RMAKER_SERVER_CONFIG_CREDENTIAL_PROVIDER_ENDPOINT, "abc123.credentials.iot.us-east-1.amazonaws.com");

    char *value = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_server_config_get(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, &value, 0));
    TEST_ASSERT_NULL(value);
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_VIDEO_ROLE_ALIAS
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND,
                      esp_rmaker_server_config_get(ESP_RMAKER_SERVER_CONFIG_NODE_VIDEO_ROLE_ALIASES, &value, 0));
    TEST_ASSERT_NULL(value);
#endif

    esp_rmaker_server_config_deinit();
}

void test_cloud_event_server_config_waits_for_response(void)
{
    __init_fresh();

    /* Nothing received yet: a bounded wait times out rather than returning a stale value. */
    char *value = (char *)0x1;
    TEST_ASSERT_EQUAL(ESP_RMAKER_TIMEOUT,
                      esp_rmaker_server_config_get(ESP_RMAKER_SERVER_CONFIG_CREDENTIAL_PROVIDER_ENDPOINT, &value, 10));
    TEST_ASSERT_NULL(value);

    esp_rmaker_server_config_deinit();
}

void test_cloud_event_server_config_error_reply_not_received(void)
{
    __init_fresh();

    /* An error reply, or one with none of the requested keys, does not count as received: the next connection asks
     * again, and readers keep waiting rather than getting NOT_FOUND for good. */
    __dispatch("{\"event\":[\"getServerConfig\"],\"getServerConfig\":{\"status\":\"failure\"}}");
    TEST_ASSERT_FALSE(esp_rmaker_server_config_is_received());
    __dispatch("{\"event\":[\"getServerConfig\"],\"getServerConfig\":{}}");
    TEST_ASSERT_FALSE(esp_rmaker_server_config_is_received());
    char *value = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_TIMEOUT, esp_rmaker_server_config_get(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, &value, 0));
    TEST_ASSERT_NULL(value);

    /* A later good reply is taken. */
    __dispatch(SERVER_CONFIG_PAYLOAD);
    TEST_ASSERT_TRUE(esp_rmaker_server_config_is_received());
    __assert_value(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, "us-east-1");

    esp_rmaker_server_config_deinit();
}

void test_cloud_event_server_config_response_is_cached(void)
{
    __init_fresh();

    esp_rmaker_cloud_events_tracker_t tracker = {0};
    esp_rmaker_cloud_event_response_getServerConfig(&tracker);
    TEST_ASSERT_EQUAL(1 << RMAKER_CLOUD_EVENT_FLAG_POS_getServerConfig,
                      tracker.events_processed & (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getServerConfig));
    esp_rmaker_server_config_deinit();

    /* End to end through the real payload dispatcher. */
    __init_fresh();
    __dispatch(SERVER_CONFIG_PAYLOAD);

    __assert_value(ESP_RMAKER_SERVER_CONFIG_CREDENTIAL_PROVIDER_ENDPOINT, "abc123.credentials.iot.us-east-1.amazonaws.com");
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_VIDEO_ROLE_ALIAS
    __assert_value(ESP_RMAKER_SERVER_CONFIG_NODE_VIDEO_ROLE_ALIASES, "rmng-node-video-role-v1");
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILE_ROLE_ALIAS
    __assert_value(ESP_RMAKER_SERVER_CONFIG_NODE_FILE_ROLE_ALIASES, "rmng-node-file-role-v1");
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILES_BUCKET_NAME
    __assert_value(ESP_RMAKER_SERVER_CONFIG_FILES_BUCKET_NAME, "esp-rm-files-123456789012-us-east-1");
#endif
    __assert_value(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, "us-east-1");

    /* Every key is in the request tree. */
    for (int key = 0; key < ESP_RMAKER_SERVER_CONFIG_KEY_MAX; key++) {
        char *value = NULL;
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, esp_rmaker_server_config_get(key, &value, 0), "key not in the tree");
        free(value);
    }

    esp_rmaker_server_config_deinit();
}

void test_cloud_event_server_config_missing_rmng_base(void)
{
    __init_fresh();

    /* A response without rmng-base has none of the requested keys, so it is not taken as the config. */
    __dispatch("{\"event\":[\"getServerConfig\"],\"getServerConfig\":{\"rmng-core\":{}}}");
    TEST_ASSERT_FALSE(esp_rmaker_server_config_is_received());
    char *value = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_TIMEOUT,
                      esp_rmaker_server_config_get(ESP_RMAKER_SERVER_CONFIG_CREDENTIAL_PROVIDER_ENDPOINT, &value, 0));
    TEST_ASSERT_NULL(value);

    esp_rmaker_server_config_deinit();
}

void test_cloud_event_server_config_invalid_args(void)
{
    /* *p_value is cleared on every error, an out-of-range key included. */
    char *value = (char *)0x1;
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_server_config_get(ESP_RMAKER_SERVER_CONFIG_KEY_MAX, &value, 0));
    TEST_ASSERT_NULL(value);
    value = (char *)0x1;
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_server_config_get((esp_rmaker_server_config_key_t) -1, &value, 0));
    TEST_ASSERT_NULL(value);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_server_config_get(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, NULL, 0));
}

void test_cloud_event_server_config_deinit_keeps_readers_safe(void)
{
    __init_fresh();
    __dispatch(SERVER_CONFIG_PAYLOAD);
    __assert_value(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, "us-east-1");

    /* deinit clears the cache but keeps the sync objects, so a reader that races it gets
     * a clean "not received" rather than touching deleted objects. */
    esp_rmaker_server_config_deinit();
    char *value = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_TIMEOUT, esp_rmaker_server_config_get(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, &value, 0));
    TEST_ASSERT_NULL(value);

    /* ... and a later init + response works again. */
    __init_fresh();
    __dispatch(SERVER_CONFIG_PAYLOAD);
    __assert_value(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, "us-east-1");
    esp_rmaker_server_config_deinit();
}
void test_cloud_event_server_config_stop_wakes_reader(void)
{
    /* Once the SDK stops, a reader with a long timeout returns at once. */
    __init_fresh();
    esp_rmaker_server_config_deinit();
    char *value = (char *)0x1;
    osal_tick_type_t start = osal_task_get_tick_count();
    TEST_ASSERT_EQUAL(ESP_RMAKER_TIMEOUT,
                      esp_rmaker_server_config_get(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, &value, 60000));
    TEST_ASSERT_LESS_THAN_UINT32(1000, osal_ms_from_ticks(osal_task_get_tick_count() - start));
    TEST_ASSERT_NULL(value);
}

#endif /* CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG */
