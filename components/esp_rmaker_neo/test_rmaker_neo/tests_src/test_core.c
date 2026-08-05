/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_core.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "esp_rmaker_core.h"

void test_core_error_paths(void)
{
    /* Node init with NULL config should fail */
    TEST_ASSERT_EQUAL_MESSAGE(NULL, esp_rmaker_node_init(NULL, "n", "t"), "node init should fail with NULL config");

    /* Start without init may fail with generic error if work queue not initialized */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_RMAKER_OK, esp_rmaker_start(), "start should fail without init");

    /* Stop without start should fail */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_RMAKER_OK, esp_rmaker_stop(), "stop should fail without start");
}
