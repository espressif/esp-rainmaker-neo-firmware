/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_bridge.c
 * @brief Unit tests for the bridge subsystem.
 *
 * Covers pure helpers that do not depend on a live MQTT connection:
 *   - Bridge-specific MQTT topic builders.
 *   - Suffix validator.
 *   - Topic parsers for Rule-A and Rule-B rewritten topics.
 *   - Public API argument validation (rejects bad input without
 *     touching the cloud).
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "sdkconfig.h"

#include "esp_rmaker_bridge.h"
#include "bridge/bridge_internal.h"
#include "network/mqtt_topics.h"
#include "local_config.h"

#include "osal_event_loop.h"

#include <string.h>
#include <stdio.h>

/* Topic builders ************************************************************/

void test_bridge_topic_group_control_subgroup_wildcard(void)
{
    char buf[MQTT_TOPIC_BUFFER_SIZE];
    int len = esp_rmaker_mqtt_topic_group_control_subgroup_wildcard(buf, sizeof(buf), "grpX");
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("rainmaker/nodes/groups/grpX/subgroups/+/control", buf);

    /* NULL / empty primary rejected. */
    TEST_ASSERT_EQUAL(-1, esp_rmaker_mqtt_topic_group_control_subgroup_wildcard(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL(-1, esp_rmaker_mqtt_topic_group_control_subgroup_wildcard(buf, sizeof(buf), ""));
    TEST_ASSERT_EQUAL(-1, esp_rmaker_mqtt_topic_group_control_subgroup_wildcard(NULL, sizeof(buf), "grpX"));
    TEST_ASSERT_EQUAL(-1, esp_rmaker_mqtt_topic_group_control_subgroup_wildcard(buf, 0, "grpX"));
}

void test_bridge_topic_bridges_to_cloud(void)
{
    esp_rmaker_local_config_init();
    char *thing = NULL;
    if (esp_rmaker_credentials_get_thing_name(&thing) != ESP_RMAKER_OK || thing == NULL) {
        /* No factory NVS in this test environment - same skip path as
         * test_mqtt_topics_basic. The topic builders are exercised
         * indirectly by the cloud-connected integration tests. */
        TEST_IGNORE_MESSAGE("Thing name unavailable (no factory NVS); skipping");
        return;
    }

    char buf[MQTT_TOPIC_BUFFER_SIZE];
    char expected[MQTT_TOPIC_BUFFER_SIZE];

    int len = esp_rmaker_mqtt_topic_bridges_to_cloud(buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
#if CONFIG_ESP_RMAKER_MQTT_USE_BASIC_INGEST
    snprintf(expected, sizeof(expected), "$aws/rules/bridge_to_cloud_rule/rainmaker/bridges/%s/to_cloud", thing);
#else
    snprintf(expected, sizeof(expected), "rainmaker/bridges/%s/to_cloud", thing);
#endif
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    len = esp_rmaker_mqtt_topic_bridges_children_from_cloud_filter(buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    snprintf(expected, sizeof(expected), "rainmaker/bridges/%s/children/+/from_cloud", thing);
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    len = esp_rmaker_mqtt_topic_bridges_children_params_filter(buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    snprintf(expected, sizeof(expected), "rainmaker/bridges/%s/children/+/user/+/params", thing);
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    free(thing);
}

/* Inline ops impl for the test (writes a fixed thing name + group info). */
typedef struct {
    const char *thing_name;
    const char *group_info;
} __test_priv_t;

static int __test_write_thing_name(void *priv, char *buf, size_t buf_size)
{
    __test_priv_t *p = (__test_priv_t *)priv;
    int written = snprintf(buf, buf_size, "%s", p->thing_name);
    return (written > 0 && (size_t)written < buf_size) ? written : -1;
}

static int __test_write_group_info_str(void *priv, char *buf, size_t buf_size)
{
    __test_priv_t *p = (__test_priv_t *)priv;
    int written = snprintf(buf, buf_size, "%s", p->group_info ? p->group_info : "");
    return (written >= 0 && (size_t)written < buf_size) ? written : -1;
}

static const esp_rmaker_topic_ops_t __test_ops = {
    .write_thing_name = __test_write_thing_name,
    .write_group_info_str = __test_write_group_info_str,
};

void test_bridge_topic_child_shadow_updates(void)
{
    char buf[MQTT_TOPIC_BUFFER_SIZE];
    int len;
    __test_priv_t priv = { .thing_name = "br--A", .group_info = "grp-s1" };
    esp_rmaker_topic_ctx_t ctx = { .ops = &__test_ops, .priv = &priv };

    len = esp_rmaker_mqtt_topic_params_named_shadow_update(&ctx, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("$aws/things/br--A/shadow/name/params-grp-s1/update", buf);

    /* Empty group info permitted. */
    priv.group_info = "";
    len = esp_rmaker_mqtt_topic_params_named_shadow_update(&ctx, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("$aws/things/br--A/shadow/name/params-/update", buf);

    /* iparams topic is group-info-independent. */
    len = esp_rmaker_mqtt_topic_params_indexed_shadow_update(&ctx, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_STRING("$aws/things/br--A/shadow/name/iparams/update", buf);
}

/* Suffix validator **********************************************************/

void test_bridge_suffix_validator(void)
{
    /* Accepted forms. */
    TEST_ASSERT_TRUE(bridge_internal_valid_suffix("A"));
    TEST_ASSERT_TRUE(bridge_internal_valid_suffix("a"));
    TEST_ASSERT_TRUE(bridge_internal_valid_suffix("0"));
    TEST_ASSERT_TRUE(bridge_internal_valid_suffix("abc_123"));
    TEST_ASSERT_TRUE(bridge_internal_valid_suffix("UPPER_lower_0123456789"));
    /* 32 chars exactly. */
    TEST_ASSERT_TRUE(bridge_internal_valid_suffix("01234567890123456789012345678901"));

    /* Rejected. */
    TEST_ASSERT_FALSE(bridge_internal_valid_suffix(NULL));
    TEST_ASSERT_FALSE(bridge_internal_valid_suffix(""));
    TEST_ASSERT_FALSE(bridge_internal_valid_suffix("has-hyphen")); /* hyphen disallowed */
    TEST_ASSERT_FALSE(bridge_internal_valid_suffix("has.dot"));
    TEST_ASSERT_FALSE(bridge_internal_valid_suffix("has space"));
    TEST_ASSERT_FALSE(bridge_internal_valid_suffix("has/slash"));
    /* 33 chars too long. */
    TEST_ASSERT_FALSE(bridge_internal_valid_suffix("012345678901234567890123456789012"));
}

/* Topic parsers *************************************************************/

void test_bridge_parse_child_from_from_cloud_topic(void)
{
    char child[64];
    const char *topic = "rainmaker/bridges/brdg/children/brdg--A/from_cloud";
    int n = bridge_internal_parse_child_from_from_cloud_topic(topic, strlen(topic), child, sizeof(child));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("brdg--A", child);

    /* Multi-segment child name with hyphens / underscores. */
    topic = "rainmaker/bridges/brdg/children/brdg--abc_123/from_cloud";
    n = bridge_internal_parse_child_from_from_cloud_topic(topic, strlen(topic), child, sizeof(child));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("brdg--abc_123", child);

    /* Malformed: too few segments. */
    topic = "rainmaker/bridges/brdg";
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_from_from_cloud_topic(topic, strlen(topic), child, sizeof(child)));

    /* Buffer too small. */
    topic = "rainmaker/bridges/brdg/children/brdg--ALONGCHILDNAME/from_cloud";
    char small[4];
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_from_from_cloud_topic(topic, strlen(topic), small, sizeof(small)));

    /* Null pointer guards. */
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_from_from_cloud_topic(NULL, 0, child, sizeof(child)));
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_from_from_cloud_topic(topic, strlen(topic), NULL, sizeof(child)));
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_from_from_cloud_topic(topic, strlen(topic), child, 0));
}

void test_bridge_parse_child_and_shadow_from_params_topic(void)
{
    char child[64];
    char shadow[64];
    const char *topic = "rainmaker/bridges/brdg/children/brdg--A/user/params-grp/params";
    int n = bridge_internal_parse_child_and_shadow_from_params_topic(topic, strlen(topic),
            child, sizeof(child), shadow, sizeof(shadow));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("brdg--A", child);
    TEST_ASSERT_EQUAL_STRING("params-grp", shadow);

    /* With subgroup. */
    topic = "rainmaker/bridges/brdg/children/brdg--XYZ/user/params-grp-sg1/params";
    n = bridge_internal_parse_child_and_shadow_from_params_topic(topic, strlen(topic),
            child, sizeof(child), shadow, sizeof(shadow));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("brdg--XYZ", child);
    TEST_ASSERT_EQUAL_STRING("params-grp-sg1", shadow);

    /* Malformed: missing /params suffix. */
    topic = "rainmaker/bridges/brdg/children/brdg--A/user/params-grp";
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_and_shadow_from_params_topic(topic, strlen(topic),
                      child, sizeof(child), shadow, sizeof(shadow)));

    /* Malformed: too few segments. */
    topic = "rainmaker/bridges/brdg";
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_and_shadow_from_params_topic(topic, strlen(topic),
                      child, sizeof(child), shadow, sizeof(shadow)));
}

/* Public API argument validation ********************************************/

void test_bridge_public_api_arg_validation(void)
{
    osal_event_loop_create_default();

    /* Without init, public API must reject. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE,
                      esp_rmaker_bridge_add_child("A", "id-1"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE,
                      esp_rmaker_bridge_remove_child(NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE,
                      esp_rmaker_bridge_child_mark_online(NULL, true));

    /* init is idempotent. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_internal_init());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_internal_init());

    /* Bad suffix / local_id rejected without attempting publish. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_bridge_add_child(NULL, "id"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_bridge_add_child("", "id"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_bridge_add_child("bad-hyphen", "id"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_bridge_add_child("A", NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_bridge_add_child("A", ""));

    /* remove_child on NULL handle. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_bridge_remove_child(NULL));

    /* mark_online on NULL handle. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_bridge_child_mark_online(NULL, true));

    /* Introspection of NULL handle is NULL. */
    TEST_ASSERT_NULL(esp_rmaker_bridge_child_thing_name(NULL));
    TEST_ASSERT_NULL(esp_rmaker_bridge_child_bridge_local_id(NULL));

    osal_event_loop_delete_default();
}
