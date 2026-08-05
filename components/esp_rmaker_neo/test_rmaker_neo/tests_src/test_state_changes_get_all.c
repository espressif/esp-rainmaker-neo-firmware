/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_state_changes_get_all.c
 * @brief Ctx-filtering tests for the data model's
 *        ``get_all`` adapter - verifies that update IDs are emitted
 *        only for the ctx that owns them (self vs bridge child).
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "sdkconfig.h"

#include "esp_rmaker_flow.h"
#include "node_internal.h"
#include "data_model_internal.h"
#include "network/state_changes.h"
#include "network/mqtt_topics.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "esp_rmaker_bridge.h"
#include "bridge/bridge_internal.h"
#endif

#include "osal_event_loop.h"

#include <stddef.h>
#include <stdlib.h>
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

static void __release_all(esp_rmaker_state_update_id_t *ids, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (ids[i]) {
            data_model_state_update_id_release(ids[i]);
        }
    }
    if (ids) {
        free(ids);
    }
}

void test_get_all_rejects_null_ctx(void)
{
    osal_event_loop_create_default();
    esp_rmaker_node_t *node = __setup_node();

    esp_rmaker_state_update_id_t *ids = NULL;
    size_t n = 99;
    esp_rmaker_error_t err = data_model_state_update_id_get_all(NULL, &ids, &n);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, err);
    TEST_ASSERT_NULL(ids);

    __teardown_node(node);
    osal_event_loop_delete_default();
}

void test_get_all_self_ctx_returns_self_params_only(void)
{
    osal_event_loop_create_default();
    esp_rmaker_node_t *node = __setup_node();

    esp_rmaker_device_t *dev = esp_rmaker_device_create("self_dev", "t", NULL);
    TEST_ASSERT_NOT_NULL(dev);
    esp_rmaker_param_t *p_a = esp_rmaker_param_create("a", "int", esp_rmaker_int(1), PROP_FLAG_READ);
    esp_rmaker_param_t *p_b = esp_rmaker_param_create("b", "int", esp_rmaker_int(2), PROP_FLAG_READ);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, p_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, p_b));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, dev));

    esp_rmaker_state_update_id_t *ids = NULL;
    size_t n = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_update_id_get_all(node, &ids, &n));
    TEST_ASSERT_EQUAL(2, n);
    /* Every returned id maps back to the self node. */
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_PTR(node, data_model_state_update_id_to_node(ids[i]));
    }
    __release_all(ids, n);

    __teardown_node(node);
    osal_event_loop_delete_default();
}

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

void test_get_all_filters_by_child_ctx(void)
{
    osal_event_loop_create_default();
    bridge_internal_deinit();
    esp_rmaker_node_t *node = __setup_node();

    /* Self-owned device with two params. */
    esp_rmaker_device_t *self_dev = esp_rmaker_device_create("self_dev", "t", NULL);
    esp_rmaker_param_t *p_s = esp_rmaker_param_create("s1", "int", esp_rmaker_int(10), PROP_FLAG_READ);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(self_dev, p_s));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, self_dev));

    /* Child-owned device on the child's own node. */
    esp_rmaker_bridge_child_handle_t child = bridge_internal_test_seed_child("a", "lid-a", "parent--a");
    TEST_ASSERT_NOT_NULL(child);
    esp_rmaker_node_t *child_node = esp_rmaker_bridge_child_node(child);
    TEST_ASSERT_NOT_NULL(child_node);

    esp_rmaker_device_t *child_dev = esp_rmaker_device_create("child_dev", "t", NULL);
    esp_rmaker_param_t *c1 = esp_rmaker_param_create("c1", "int", esp_rmaker_int(100), PROP_FLAG_READ);
    esp_rmaker_param_t *c2 = esp_rmaker_param_create("c2", "int", esp_rmaker_int(200), PROP_FLAG_READ);
    esp_rmaker_param_t *c3 = esp_rmaker_param_create("c3", "int", esp_rmaker_int(300), PROP_FLAG_READ);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(child_dev, c1));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(child_dev, c2));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(child_dev, c3));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(child_node, child_dev));

    /* self node - only self params. */
    {
        esp_rmaker_state_update_id_t *ids = NULL;
        size_t n = 0;
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_update_id_get_all(node, &ids, &n));
        TEST_ASSERT_EQUAL(1, n);
        TEST_ASSERT_EQUAL_PTR(node, data_model_state_update_id_to_node(ids[0]));
        __release_all(ids, n);
    }

    /* child node - only that child's params. */
    {
        esp_rmaker_state_update_id_t *ids = NULL;
        size_t n = 0;
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_update_id_get_all(child_node, &ids, &n));
        TEST_ASSERT_EQUAL(3, n);
        for (size_t i = 0; i < n; i++) {
            TEST_ASSERT_EQUAL_PTR(child_node, data_model_state_update_id_to_node(ids[i]));
        }
        __release_all(ids, n);
    }

    __teardown_node(node);
    bridge_internal_deinit();
    osal_event_loop_delete_default();
}

void test_get_all_unknown_ctx_returns_zero(void)
{
    osal_event_loop_create_default();
    bridge_internal_deinit();
    esp_rmaker_node_t *node = __setup_node();

    /* Build a self-owned device so get_all has something to walk past. */
    esp_rmaker_device_t *self_dev = esp_rmaker_device_create("self_dev", "t", NULL);
    esp_rmaker_param_t *p_s = esp_rmaker_param_create("s1", "int", esp_rmaker_int(10), PROP_FLAG_READ);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(self_dev, p_s));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, self_dev));

    /* Seed a child but don't attach any device to it. */
    esp_rmaker_bridge_child_handle_t child = bridge_internal_test_seed_child("a", "lid-a", "parent--a");
    TEST_ASSERT_NOT_NULL(child);
    esp_rmaker_node_t *child_node = esp_rmaker_bridge_child_node(child);

    esp_rmaker_state_update_id_t *ids = NULL;
    size_t n = 99;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_update_id_get_all(child_node, &ids, &n));
    TEST_ASSERT_EQUAL(0, n);
    TEST_ASSERT_NULL(ids);

    __teardown_node(node);
    bridge_internal_deinit();
    osal_event_loop_delete_default();
}

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
