/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_node_config_pending.c
 * @brief Unit tests for the node-config pending-list module.
 *
 * Covers the data-structure surface only (add / remove /
 * clear_inflight / clear_all_inflight). The fire_all path exercises
 * the cloud manager + NVS, which is covered by higher-level tests.
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include "node_config_pending.h"
#include "esp_rmaker_bridge.h"
#include "bridge/bridge_internal.h"
#include "network/mqtt_topics.h"
#include "node_internal.h"

#include "osal_event_loop.h"

#include <stddef.h>
#include <string.h>

/* Stack-stable fake node used in lieu of a real esp_rmaker_node_init() call.
 * The pending list only does pointer-identity lookups, so any stable
 * pointer works. */
static _esp_rmaker_node_t __fake_self_node;

static const esp_rmaker_node_t *__fake_node(void)
{
    return (const esp_rmaker_node_t *)&__fake_self_node;
}

static void __setup(void)
{
    osal_event_loop_create_default();
    bridge_internal_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, node_config_pending_init());
    /* Reset state to a known list = {fake_self}. */
    node_config_pending_remove(__fake_node());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, node_config_pending_add(__fake_node()));
}

static void __teardown(void)
{
    bridge_internal_deinit();
    osal_event_loop_delete_default();
}

void test_node_config_pending_init_idempotent(void)
{
    __setup();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, node_config_pending_init());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, node_config_pending_init());
    __teardown();
}

void test_node_config_pending_add_rejects_null_ctx(void)
{
    __setup();
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, node_config_pending_add(NULL));
    __teardown();
}

void test_node_config_pending_add_idempotent(void)
{
    __setup();
    /* self already present from setup; re-add returns OK. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, node_config_pending_add(__fake_node()));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, node_config_pending_add(__fake_node()));
    __teardown();
}

void test_node_config_pending_remove_self_and_readd(void)
{
    __setup();
    node_config_pending_remove(__fake_node());
    /* No way to introspect; re-add must succeed (i.e. slot was freed). */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, node_config_pending_add(__fake_node()));
    __teardown();
}

void test_node_config_pending_remove_unknown_noop(void)
{
    __setup();
    /* A valid-but-non-pending node should not crash. remove() writes the node's
     * embedded ::pending_cfg, so it requires a real node-sized object - a smaller
     * stack stand-in would be written out of bounds (stack-buffer-overflow). */
    _esp_rmaker_node_t other = {0};
    node_config_pending_remove((const esp_rmaker_node_t *)&other);
    __teardown();
}

void test_node_config_pending_clear_inflight_unknown_noop(void)
{
    __setup();
    /* Valid-but-non-pending node: clear_inflight writes the node's embedded
     * ::pending_cfg, so it needs a real node-sized object (see remove test). */
    _esp_rmaker_node_t other = {0};
    node_config_pending_clear_inflight((const esp_rmaker_node_t *)&other);
    node_config_pending_clear_inflight(NULL);
    __teardown();
}

void test_node_config_pending_clear_all_inflight_safe(void)
{
    __setup();
    /* Mass-clear should be safe with only self present. */
    node_config_pending_clear_all_inflight();
    __teardown();
}

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
