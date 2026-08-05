/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_core_node_lifecycle.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "esp_rmaker_core.h"

void test_core_node_init_null_config(void)
{
    /* Node init with NULL config should return NULL */
    esp_rmaker_node_t *node = esp_rmaker_node_init(NULL, "test", "type");
    TEST_ASSERT_NULL(node);
}

void test_core_node_init_success(void)
{
    /* Node init with valid config should succeed */
    esp_rmaker_config_t config = {
        .enable_time_sync = false,
    };

    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "test", "type");
    TEST_ASSERT_NOT_NULL(node);

    /* Cleanup */
    esp_rmaker_node_deinit(node);
}

void test_core_node_deinit_states(void)
{
    esp_rmaker_config_t config = {
        .enable_time_sync = false,
    };

    /* Init node */
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "test", "type");
    TEST_ASSERT_NOT_NULL(node);

    /* Deinit when stopped should succeed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_deinit(node));

    /* Deinit without init should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, esp_rmaker_node_deinit(node));
}
