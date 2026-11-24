/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_controller_topics.c
 * @brief Tests for the shadow names and MQTT topics the controller addresses nodes through.
 *
 * A wrong topic is accepted by the broker and simply never delivers, so these are pinned rather
 * than left to an integration test to notice. The subgroup path in particular is easy to get
 * wrong: the shadow of a node one subgroup deep is "params-<group>-<subgroup>", the ids are sorted,
 * and a missing or doubled dash addresses a shadow that does not exist.
 */

#include <string.h>

#include "unity.h"

#include "controller_internal.h"

static char s_out[CONTROLLER_TOPIC_MAX_LEN];

/* Build a standalone node with this subgroup path, as a sync would. */
static ctrl_node_t *make_node(const char *node_id, const char *const *subgroups, size_t num)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc(node_id);
    TEST_ASSERT_NOT_NULL(node);
    for (size_t i = 0; i < num; i++) {
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_node_add_subgroup(node, subgroups[i]));
    }
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_node_set_group(node, "grp123"));
    return node;
}

static void free_node(ctrl_node_t *node)
{
    controller_model_free_node_list(node);
}

void test_controller_shadow_name_plain_group(void)
{
    ctrl_node_t *node = make_node("node9", NULL, 0);
    TEST_ASSERT_EQUAL_STRING("grp123", node->group_info);
    TEST_ASSERT_EQUAL_STRING("params-grp123", node->shadow);
    free_node(node);
}

void test_controller_shadow_name_joins_subgroups_with_a_dash(void)
{
    const char *sg[] = { "sgA", "sgB" };
    ctrl_node_t *node = make_node("node9", sg, 2);
    TEST_ASSERT_EQUAL_STRING("params-grp123-sgA-sgB", node->shadow);
    free_node(node);
}

void test_controller_shadow_name_sorts_the_subgroup_path(void)
{
    /* The cloud keys the shadow on the sorted path, so a node handed "sgB" first must still
     * address "params-<group>-sgA-sgB". Getting this wrong is a silent timeout. */
    const char *sg[] = { "sgB", "sgA" };
    ctrl_node_t *node = make_node("node9", sg, 2);
    TEST_ASSERT_EQUAL_STRING("params-grp123-sgA-sgB", node->shadow);
    free_node(node);
}

void test_controller_shadow_name_ignores_a_duplicate_subgroup(void)
{
    const char *sg[] = { "sgA", "sgA" };
    ctrl_node_t *node = make_node("node9", sg, 2);
    TEST_ASSERT_EQUAL_size_t(1, node->num_subgroups);
    TEST_ASSERT_EQUAL_STRING("params-grp123-sgA", node->shadow);
    free_node(node);
}

void test_controller_topic_shadow_get(void)
{
    const char *sg[] = { "sgA" };
    ctrl_node_t *node = make_node("node9", sg, 1);
    esp_rmaker_topic_ctx_t ctx;
    controller_node_topic_ctx(node, &ctx);
    TEST_ASSERT_GREATER_THAN_INT(0,
                                 esp_rmaker_mqtt_topic_params_named_shadow_get(&ctx, s_out, sizeof(s_out)));
    TEST_ASSERT_EQUAL_STRING("$aws/things/node9/shadow/name/params-grp123-sgA/get", s_out);
    free_node(node);
}

void test_controller_topic_node_params(void)
{
    ctrl_node_t *node = make_node("node9", NULL, 0);
    esp_rmaker_topic_ctx_t ctx;
    controller_node_topic_ctx(node, &ctx);
    TEST_ASSERT_GREATER_THAN_INT(0,
                                 esp_rmaker_mqtt_topic_params_to_node_for_node(&ctx, s_out, sizeof(s_out)));
    TEST_ASSERT_EQUAL_STRING("rainmaker/nodes/node9/user/params-grp123/params", s_out);
    free_node(node);
}

void test_controller_topic_filters_use_a_single_level_wildcard(void)
{
    /* "+" matches exactly one level, so one filter covers every thing and no other shadow. */
    esp_rmaker_mqtt_topic_params_named_shadow_get_accepted_filter("params-grp123", s_out, sizeof(s_out));
    TEST_ASSERT_EQUAL_STRING("$aws/things/+/shadow/name/params-grp123/get/accepted", s_out);
    esp_rmaker_mqtt_topic_params_named_shadow_update_accepted_filter("params-grp123", s_out, sizeof(s_out));
    TEST_ASSERT_EQUAL_STRING("$aws/things/+/shadow/name/params-grp123/update/accepted", s_out);
}

void test_controller_topic_group_control_without_subgroup(void)
{
    esp_rmaker_mqtt_topic_group_control_broadcast(s_out, sizeof(s_out), "grp123");
    TEST_ASSERT_EQUAL_STRING("rainmaker/nodes/groups/grp123/control", s_out);
}

void test_controller_topic_group_control_with_subgroup(void)
{
    esp_rmaker_mqtt_topic_group_control_subgroup(s_out, sizeof(s_out), "grp123", "sgA");
    TEST_ASSERT_EQUAL_STRING("rainmaker/nodes/groups/grp123/subgroups/sgA/control", s_out);
}

void test_controller_topic_rejects_an_undersized_buffer(void)
{
    /* Truncation must be a failure: a half-written topic is a valid topic that nobody listens on. */
    char small[16];
    ctrl_node_t *node = make_node("node9", NULL, 0);
    esp_rmaker_topic_ctx_t ctx;
    controller_node_topic_ctx(node, &ctx);
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_params_named_shadow_get(&ctx, small, sizeof(small)));
    TEST_ASSERT_EQUAL_INT(-1, esp_rmaker_mqtt_topic_params_to_node_for_node(&ctx, small, sizeof(small)));
    TEST_ASSERT_EQUAL_INT(-1,
                          esp_rmaker_mqtt_topic_params_named_shadow_get_accepted_filter("params-grp123", small, sizeof(small)));
    free_node(node);
}

void test_controller_thing_name_extracted_from_shadow_topic(void)
{
    const char *topic = "$aws/things/node9/shadow/name/params-grp/get/accepted";
    const char *name = NULL;
    size_t len = 0;
    TEST_ASSERT_TRUE(esp_rmaker_mqtt_topic_parse_thing_name(topic, strlen(topic), &name, &len));
    TEST_ASSERT_EQUAL_size_t(5, len);
    TEST_ASSERT_EQUAL_INT(0, strncmp("node9", name, len));
}

void test_controller_thing_name_honours_topic_len_without_nul(void)
{
    /* The MQTT layer hands over a topic that is not NUL-terminated. */
    const char raw[] = "$aws/things/node9/shadow/name/xTRAILING GARBAGE";
    size_t topic_len = strlen("$aws/things/node9/shadow/name/x");
    const char *name = NULL;
    size_t len = 0;
    TEST_ASSERT_TRUE(esp_rmaker_mqtt_topic_parse_thing_name(raw, topic_len, &name, &len));
    TEST_ASSERT_EQUAL_size_t(5, len);
    TEST_ASSERT_EQUAL_INT(0, strncmp("node9", name, len));
}

void test_controller_thing_name_rejects_a_foreign_prefix(void)
{
    const char *topic = "rainmaker/nodes/node9/params";
    const char *name = NULL;
    size_t len = 0;
    TEST_ASSERT_FALSE(esp_rmaker_mqtt_topic_parse_thing_name(topic, strlen(topic), &name, &len));
}

void test_controller_thing_name_rejects_a_missing_separator(void)
{
    const char *topic = "$aws/things/node9";
    const char *name = NULL;
    size_t len = 0;
    TEST_ASSERT_FALSE(esp_rmaker_mqtt_topic_parse_thing_name(topic, strlen(topic), &name, &len));
}

void test_controller_thing_name_rejects_an_empty_name(void)
{
    /* An empty name would match a caller waiting on an empty node id. */
    const char *topic = "$aws/things//shadow/name/params-grp/get/accepted";
    const char *name = NULL;
    size_t len = 0;
    TEST_ASSERT_FALSE(esp_rmaker_mqtt_topic_parse_thing_name(topic, strlen(topic), &name, &len));
}

/* Shadow bookkeeping *********************************************************/

/* Build a node list, as a sync would, with one subgroup id per node. */
static ctrl_node_t *make_list(const char *const *ids, const char *const *subgroups, size_t num)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *head = NULL;
    ctrl_node_t **tail = &head;
    for (size_t i = 0; i < num; i++) {
        ctrl_node_t *node = controller_model_node_alloc(ids[i]);
        TEST_ASSERT_NOT_NULL(node);
        if (subgroups[i]) {
            TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_node_add_subgroup(node, subgroups[i]));
        }
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_node_set_group(node, "grp123"));
        *tail = node;
        tail = &node->next;
    }
    return head;
}

void test_controller_shadow_collect_reports_each_name_once(void)
{
    const char *ids[] = { "n1", "n2", "n3" };
    const char *sg[] = { "sgA", "sgA", NULL };
    ctrl_node_t *head = make_list(ids, sg, 3);

    char names[3][CONTROLLER_SHADOW_NAME_BUFFER_SIZE];
    size_t num = controller_model_collect_shadows(head, names, 3);

    /* n1 and n2 share a subgroup, so they share one shadow name. */
    TEST_ASSERT_EQUAL_UINT(2, num);
    TEST_ASSERT_EQUAL_STRING("params-grp123-sgA", names[0]);
    TEST_ASSERT_EQUAL_STRING("params-grp123", names[1]);
    free_node(head);
}

void test_controller_shadow_collect_honours_the_buffer_bound(void)
{
    const char *ids[] = { "n1", "n2" };
    const char *sg[] = { "sgA", "sgB" };
    ctrl_node_t *head = make_list(ids, sg, 2);

    char names[1][CONTROLLER_SHADOW_NAME_BUFFER_SIZE];
    TEST_ASSERT_EQUAL_UINT(1, controller_model_collect_shadows(head, names, 1));
    TEST_ASSERT_EQUAL_STRING("params-grp123-sgA", names[0]);
    free_node(head);
}

void test_controller_shadow_collect_reports_none_for_an_empty_list(void)
{
    char names[1][CONTROLLER_SHADOW_NAME_BUFFER_SIZE];
    TEST_ASSERT_EQUAL_UINT(0, controller_model_collect_shadows(NULL, names, 1));
}

void test_controller_shadow_in_use_finds_a_name_the_list_holds(void)
{
    const char *ids[] = { "n1" };
    const char *sg[] = { "sgA" };
    ctrl_node_t *head = make_list(ids, sg, 1);

    TEST_ASSERT_TRUE(controller_model_list_uses_shadow(head, "params-grp123-sgA"));
    free_node(head);
}

void test_controller_shadow_in_use_rejects_a_name_the_migration_dropped(void)
{
    const char *ids[] = { "n1" };
    const char *sg[] = { "sgA" };
    ctrl_node_t *head = make_list(ids, sg, 1);

    /* The shadow the node held before the group moved. It is what the sync unsubscribes. */
    TEST_ASSERT_FALSE(controller_model_list_uses_shadow(head, "params-oldgrp-sgA"));
    TEST_ASSERT_FALSE(controller_model_list_uses_shadow(NULL, "params-grp123-sgA"));
    free_node(head);
}
