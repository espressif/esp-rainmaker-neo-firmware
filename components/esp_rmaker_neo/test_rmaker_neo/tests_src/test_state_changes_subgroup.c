/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_state_changes_subgroup.c
 * @brief Subgroup-membership tests for esp_rmaker_node_is_in_subgroup -
 *        the filter used by the group-control dispatcher in
 *        state_changes.c to skip nodes outside the targeted subgroup.
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "sdkconfig.h"

#include "esp_rmaker_flow.h"
#include "node_internal.h"
#include "local_config.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "esp_rmaker_bridge.h"
#include "bridge/bridge_internal.h"
#endif

#include "osal_event_loop.h"

#include <stddef.h>
#include <string.h>

static esp_rmaker_node_t *__setup_node(void)
{
    esp_rmaker_config_t config = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "tnode", "ttype");
    TEST_ASSERT_NOT_NULL(node);
    return node;
}

static void __teardown_node(esp_rmaker_node_t *node)
{
    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
}

void test_node_is_in_subgroup_rejects_invalid_args(void)
{
    osal_event_loop_create_default();
    esp_rmaker_node_t *node = __setup_node();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_init());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_group_info_str("grp1-sg1"));

    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(NULL, "sg1"));
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node, NULL));
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node, ""));

    /* Reset NVS state so the next test starts from a known empty group. */
    (void)esp_rmaker_local_config_set_group_info_str("");
    esp_rmaker_local_config_deinit();
    __teardown_node(node);
    osal_event_loop_delete_default();
}

void test_node_is_in_subgroup_self_membership(void)
{
    osal_event_loop_create_default();
    esp_rmaker_node_t *node = __setup_node();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_init());
    /* Clear any group_info_str left over in NVS from prior tests. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_group_info_str(""));

    /* Self has no group info -> never matches. */
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node, "sg1"));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_group_info_str("grp1-sg1-sg2"));
    TEST_ASSERT_TRUE(esp_rmaker_node_is_in_subgroup(node, "sg1"));
    TEST_ASSERT_TRUE(esp_rmaker_node_is_in_subgroup(node, "sg2"));
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node, "sg3"));
    /* Primary is not a subgroup. */
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node, "grp1"));

    /* Broadcast-only membership (no subgroups). */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_group_info_str("grp1"));
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node, "sg1"));

    (void)esp_rmaker_local_config_set_group_info_str("");
    esp_rmaker_local_config_deinit();
    __teardown_node(node);
    osal_event_loop_delete_default();
}

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

void test_node_is_in_subgroup_bridge_child_membership(void)
{
    osal_event_loop_create_default();
    bridge_internal_deinit();
    esp_rmaker_node_t *node = __setup_node();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_init());
    /* Reset NVS group state before exercising self+child membership. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_group_info_str(""));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_group_info_str("grp1-sg1"));

    esp_rmaker_bridge_child_handle_t child_a = bridge_internal_test_seed_child("a", "lid-a", "parent--a");
    TEST_ASSERT_NOT_NULL(child_a);
    esp_rmaker_node_t *node_a = esp_rmaker_bridge_child_node(child_a);
    TEST_ASSERT_NOT_NULL(node_a);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_internal_child_set_group_info_str(child_a, "grp1-sg1-sg2"));

    esp_rmaker_bridge_child_handle_t child_b = bridge_internal_test_seed_child("b", "lid-b", "parent--b");
    TEST_ASSERT_NOT_NULL(child_b);
    esp_rmaker_node_t *node_b = esp_rmaker_bridge_child_node(child_b);
    TEST_ASSERT_NOT_NULL(node_b);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_internal_child_set_group_info_str(child_b, "grp1-sg3"));

    /* sg1 -> self + child A. */
    TEST_ASSERT_TRUE(esp_rmaker_node_is_in_subgroup(node, "sg1"));
    TEST_ASSERT_TRUE(esp_rmaker_node_is_in_subgroup(node_a, "sg1"));
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node_b, "sg1"));

    /* sg2 -> child A only. */
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node, "sg2"));
    TEST_ASSERT_TRUE(esp_rmaker_node_is_in_subgroup(node_a, "sg2"));
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node_b, "sg2"));

    /* sg3 -> child B only. */
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node, "sg3"));
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node_a, "sg3"));
    TEST_ASSERT_TRUE(esp_rmaker_node_is_in_subgroup(node_b, "sg3"));

    /* Child with empty group info -> no membership. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_internal_child_set_group_info_str(child_b, ""));
    TEST_ASSERT_FALSE(esp_rmaker_node_is_in_subgroup(node_b, "sg3"));

    (void)esp_rmaker_local_config_set_group_info_str("");
    esp_rmaker_local_config_deinit();
    __teardown_node(node);
    bridge_internal_deinit();
    osal_event_loop_delete_default();
}

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
