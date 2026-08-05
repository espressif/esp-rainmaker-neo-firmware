/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_core_start_stop.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <stddef.h>

#include "esp_rmaker_core.h"
#include "osal_event_group.h"

static osal_event_group_handle_t start_event_group;
#define START_EVENT_FLAG 0x01
static void __start_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == RMAKER_EVENT && event_id == RMAKER_EVENT_CORE_STARTED) {
        osal_event_group_set_bits(start_event_group, START_EVENT_FLAG);
    }
}

void test_core_start_without_init(void)
{
    /* Start without init should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, esp_rmaker_start());
}

void test_core_start_stop(void)
{
    esp_rmaker_config_t config = {
        .enable_time_sync = false,
    };

    /* Init node */
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "test", "type");
    TEST_ASSERT_NOT_NULL(node);

    /* Wait for start event */
    start_event_group = osal_event_group_create();
    TEST_ASSERT_NOT_NULL(start_event_group);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_event_handler_register(RMAKER_EVENT, RMAKER_EVENT_CORE_STARTED, __start_event_handler, NULL));

    /* Start should succeed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_start());

    /* Start twice should be OK (idempotent) */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_start());

    /* Wait for start event */
    osal_event_group_wait_bits(start_event_group, START_EVENT_FLAG, true, true, OSAL_MAX_DELAY);
    osal_event_group_delete(start_event_group);
    osal_event_handler_unregister(RMAKER_EVENT, RMAKER_EVENT_CORE_STARTED, __start_event_handler);

    /* Cleanup */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_stop());

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_deinit(node));
}

void test_core_stop_without_start(void)
{
    /* Stop without start should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, esp_rmaker_stop());
}
