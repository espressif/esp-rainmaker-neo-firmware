/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_event_loop.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <stdint.h>
#include "esp_rmaker_event_loop.h"
#include "event_loop.h"

static void dummy_handler(void *arg, osal_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
}

void test_event_loop_init_and_handlers(void)
{
    /* invalid arg */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, event_loop_init(NULL));

    /* valid init */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_loop_init(dummy_handler));

    /* on-complete handlers */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_loop_register_mqtt_on_complete_handler(dummy_handler));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_loop_unregister_mqtt_on_complete_handler(dummy_handler));

    /* timezone handlers */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_loop_register_timezone_change_handler(dummy_handler));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_loop_unregister_timezone_change_handler(dummy_handler));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_loop_deinit());
}
