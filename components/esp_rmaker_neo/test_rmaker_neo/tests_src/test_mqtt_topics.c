/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_mqtt_topics.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "local_config.h"
#include "network/mqtt_topics.h"
#include "constants/identity.h"
#include "sdkconfig.h"

static int __topic_fn_fail(char *b, size_t s)
{
    (void)b;
    (void)s;
    return -1;
}

static void __set_group(const char *group)
{
    /* Only update group info in local config as needed */
    if (group) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_group_info_str(group));
    }
}

void test_mqtt_topics_basic(void)
{
    esp_rmaker_local_config_init();
    char buf[MQTT_TOPIC_BUFFER_SIZE];
    char expected[MQTT_TOPIC_BUFFER_SIZE];
    int len;

    __set_group("grp-s1-s2");

    char *thing = NULL;
    esp_rmaker_error_t err = esp_rmaker_credentials_get_thing_name(&thing);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(thing);

    len = esp_rmaker_mqtt_topic_to_cloud(&esp_rmaker_topic_ctx_self, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
#if CONFIG_ESP_RMAKER_MQTT_USE_BASIC_INGEST
    snprintf(expected, sizeof(expected), "$aws/rules/node_to_cloud_rule/rainmaker/nodes/%s/to_cloud", thing);
#else
    snprintf(expected, sizeof(expected), "rainmaker/nodes/%s/to_cloud", thing);
#endif /* CONFIG_ESP_RMAKER_MQTT_USE_BASIC_INGEST */
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    len = esp_rmaker_mqtt_topic_from_cloud(buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    snprintf(expected, sizeof(expected), "rainmaker/nodes/%s/from_cloud", thing);
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    len = esp_rmaker_mqtt_topic_params_to_node(buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    snprintf(expected, sizeof(expected), "rainmaker/nodes/%s/user/params-grp-s1-s2/params", thing);
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    len = esp_rmaker_mqtt_topic_params_named_shadow_update(&esp_rmaker_topic_ctx_self, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    snprintf(expected, sizeof(expected), "$aws/things/%s/shadow/name/params-grp-s1-s2/update", thing);
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    len = esp_rmaker_mqtt_topic_notify(buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
#if CONFIG_ESP_RMAKER_MQTT_USE_BASIC_INGEST
    snprintf(expected, sizeof(expected), "$aws/rules/node_notify_rule/rainmaker/nodes/%s/notify/grp-s1-s2", thing);
#else
    snprintf(expected, sizeof(expected), "rainmaker/nodes/%s/notify/grp-s1-s2", thing);
#endif /* CONFIG_ESP_RMAKER_MQTT_USE_BASIC_INGEST */
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    len = esp_rmaker_mqtt_topic_timeseries_report(&esp_rmaker_topic_ctx_self, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
#if CONFIG_ESP_RMAKER_MQTT_USE_BASIC_INGEST
    snprintf(expected, sizeof(expected), "$aws/rules/node_ts_rule/rainmaker/nodes/%s/ts/grp-s1-s2", thing);
#else
    snprintf(expected, sizeof(expected), "rainmaker/nodes/%s/ts/grp-s1-s2", thing);
#endif /* CONFIG_ESP_RMAKER_MQTT_USE_BASIC_INGEST */
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    len = esp_rmaker_mqtt_topic_params_named_shadow_delete(&esp_rmaker_topic_ctx_self, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    snprintf(expected, sizeof(expected), "$aws/things/%s/shadow/name/params-grp-s1-s2/delete", thing);
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    len = esp_rmaker_mqtt_topic_params_indexed_shadow_update(&esp_rmaker_topic_ctx_self, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    snprintf(expected, sizeof(expected), "$aws/things/%s/shadow/name/iparams/update", thing);
    TEST_ASSERT_EQUAL_STRING(expected, buf);

    /* append_accepted uses the legacy void-arg topic fn type; verify
     * with a self-only builder that still matches that signature */
    char small[16];
    int ret = esp_rmaker_mqtt_topic_append_accepted(esp_rmaker_mqtt_topic_from_cloud, small, sizeof(small));
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ret = esp_rmaker_mqtt_topic_append_accepted(esp_rmaker_mqtt_topic_from_cloud, buf, sizeof(buf));
    TEST_ASSERT_TRUE(ret > 0);
    TEST_ASSERT_TRUE(strstr(buf, "/accepted") != NULL);

    free(thing);
    esp_rmaker_local_config_deinit();
}

void test_mqtt_topics_errors(void)
{
    esp_rmaker_local_config_init();
    char buf[MQTT_TOPIC_BUFFER_SIZE];
    __set_group("grp");

    /* NULL buffer or zero size */
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_to_cloud(&esp_rmaker_topic_ctx_self, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_to_cloud(&esp_rmaker_topic_ctx_self, buf, 0));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_from_cloud(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_from_cloud(buf, 0));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_params_to_node(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_params_to_node(buf, 0));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_params_named_shadow_update(&esp_rmaker_topic_ctx_self, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_params_named_shadow_update(&esp_rmaker_topic_ctx_self, buf, 0));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_params_named_shadow_delete(&esp_rmaker_topic_ctx_self, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_params_named_shadow_delete(&esp_rmaker_topic_ctx_self, buf, 0));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_params_indexed_shadow_update(&esp_rmaker_topic_ctx_self, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_params_indexed_shadow_update(&esp_rmaker_topic_ctx_self, buf, 0));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_notify(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_notify(buf, 0));

    /* append_accepted error cases */
    __set_group("grp-x");
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_append_accepted(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_append_accepted(esp_rmaker_mqtt_topic_from_cloud, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_append_accepted(esp_rmaker_mqtt_topic_from_cloud, buf, 0));

    /* append_accepted should fail if topic_fn fails */
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_append_accepted(__topic_fn_fail, buf, sizeof(buf)));

    /* append_accepted should fail when buffer is exactly ret + 9 */
    __set_group("grp-y");
    char tmp[MQTT_TOPIC_BUFFER_SIZE];
    int base_len = esp_rmaker_mqtt_topic_from_cloud(tmp, sizeof(tmp));
    TEST_ASSERT_TRUE(base_len > 0);
    size_t exact_size = (size_t)base_len + 9; /* "/accepted" */
    char *exact = (char *)malloc(exact_size);
    TEST_ASSERT_NOT_NULL(exact);
    int r = esp_rmaker_mqtt_topic_append_accepted(esp_rmaker_mqtt_topic_from_cloud, exact, exact_size);
    TEST_ASSERT_EQUAL_INT(-1, r);
    free(exact);
    esp_rmaker_local_config_deinit();
}

void test_mqtt_topics_group_control(void)
{
    char buf[MQTT_TOPIC_BUFFER_SIZE];
    int len;

    /* Group broadcast topic */
    len = esp_rmaker_mqtt_topic_group_control_broadcast(buf, sizeof(buf), "grp1");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("rainmaker/nodes/groups/grp1/control", buf);

    /* Group control subgroup topic */
    len = esp_rmaker_mqtt_topic_group_control_subgroup(buf, sizeof(buf), "grp1", "sgX");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("rainmaker/nodes/groups/grp1/subgroups/sgX/control", buf);

    len = esp_rmaker_mqtt_topic_group_control_subgroup(buf, sizeof(buf), "grp1", "sgY");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("rainmaker/nodes/groups/grp1/subgroups/sgY/control", buf);

    /* Error cases: NULL or empty primary/subgroup */
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_group_control_broadcast(NULL, sizeof(buf), "grp1"));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_group_control_broadcast(buf, 0, "grp1"));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_group_control_broadcast(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_group_control_broadcast(buf, sizeof(buf), ""));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_group_control_subgroup(buf, sizeof(buf), "grp1", NULL));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_group_control_subgroup(buf, sizeof(buf), "grp1", ""));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_group_control_subgroup(buf, sizeof(buf), NULL, "sgX"));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_group_control_subgroup(buf, sizeof(buf), "", "sgX"));

    /* Buffer too small */
    char small[8];
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_group_control_broadcast(small, sizeof(small), "grp1"));
}

void test_mqtt_topics_parse_group_control_subgroup(void)
{
    char sg[RMAKER_SUBGROUP_BUFFER_SIZE];

    /* Broadcast topic -> sg == "" */
    const char broadcast[] = "rainmaker/nodes/groups/grp1/control";
    sg[0] = 'X';
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          esp_rmaker_mqtt_topic_parse_group_control_subgroup(broadcast, sizeof(broadcast) - 1, sg, sizeof(sg)));
    TEST_ASSERT_EQUAL_STRING("", sg);

    /* Subgroup topic -> sg extracted */
    const char sub[] = "rainmaker/nodes/groups/grp1/subgroups/sg2/control";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          esp_rmaker_mqtt_topic_parse_group_control_subgroup(sub, sizeof(sub) - 1, sg, sizeof(sg)));
    TEST_ASSERT_EQUAL_STRING("sg2", sg);

    /* Length not NUL-terminated: pass exact length, not buf size */
    const char *embedded = "rainmaker/nodes/groups/grp1/subgroups/sgA/controlEXTRA";
    size_t embedded_len = strlen("rainmaker/nodes/groups/grp1/subgroups/sgA/control");
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          esp_rmaker_mqtt_topic_parse_group_control_subgroup(embedded, embedded_len, sg, sizeof(sg)));
    TEST_ASSERT_EQUAL_STRING("sgA", sg);

    /* Malformed: missing /control suffix */
    const char bad_suffix[] = "rainmaker/nodes/groups/grp1/subgroups/sg2/foo";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          esp_rmaker_mqtt_topic_parse_group_control_subgroup(bad_suffix, sizeof(bad_suffix) - 1, sg, sizeof(sg)));

    /* Malformed: missing prefix */
    const char bad_prefix[] = "other/nodes/groups/grp1/control";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          esp_rmaker_mqtt_topic_parse_group_control_subgroup(bad_prefix, sizeof(bad_prefix) - 1, sg, sizeof(sg)));

    /* Malformed: empty primary */
    const char empty_primary[] = "rainmaker/nodes/groups//control";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          esp_rmaker_mqtt_topic_parse_group_control_subgroup(empty_primary, sizeof(empty_primary) - 1, sg, sizeof(sg)));

    /* Malformed: empty subgroup */
    const char empty_sg[] = "rainmaker/nodes/groups/grp1/subgroups//control";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          esp_rmaker_mqtt_topic_parse_group_control_subgroup(empty_sg, sizeof(empty_sg) - 1, sg, sizeof(sg)));

    /* Malformed: extra path segment between primary and /control */
    const char extra_seg[] = "rainmaker/nodes/groups/grp1/foo/control";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          esp_rmaker_mqtt_topic_parse_group_control_subgroup(extra_seg, sizeof(extra_seg) - 1, sg, sizeof(sg)));

    /* Malformed: extra segment after subgroup */
    const char extra_after[] = "rainmaker/nodes/groups/grp1/subgroups/sg2/extra/control";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          esp_rmaker_mqtt_topic_parse_group_control_subgroup(extra_after, sizeof(extra_after) - 1, sg, sizeof(sg)));

    /* Buffer too small for caller-supplied sg */
    char tiny[2];
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          esp_rmaker_mqtt_topic_parse_group_control_subgroup(sub, sizeof(sub) - 1, tiny, sizeof(tiny)));
}
