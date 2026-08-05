/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include <string.h>
#include "osal_event_loop.h"
#include "osal_task.h"

OSAL_EVENT_DEFINE_BASE(TEST_EVENT_BASE);

static volatile int handler_calls = 0;
static volatile int last_event_id = 0;
static volatile int last_value = 0;

static void handler(void *arg, osal_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    handler_calls++;
    last_event_id = id;
    if (data) {
        last_value = *(int *)data;
    }
}

void test_event_loop_basic(void)
{
    osal_err_t err;
    err = osal_event_loop_create_default();
    if (err != OSAL_ERR_OK && err != OSAL_ERR_INVALID_STATE) {
        TEST_FAIL();
    }
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_event_handler_register(TEST_EVENT_BASE, 42, handler, NULL));

    int v = 123;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_event_post(TEST_EVENT_BASE, 42, &v, sizeof(v), OSAL_MAX_DELAY));

    /* allow dispatch */
    osal_task_delay( osal_ticks_from_ms(100) );

    TEST_ASSERT_TRUE(handler_calls >= 1);
    TEST_ASSERT_EQUAL(42, last_event_id);
    TEST_ASSERT_EQUAL(123, last_value);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_event_handler_unregister(TEST_EVENT_BASE, 42, handler));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_event_loop_delete_default());
}
