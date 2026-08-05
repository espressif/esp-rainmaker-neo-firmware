/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include <time.h>
#include "osal_task.h"

static void sleeper(void *arg)
{
    (void)arg;
    osal_task_delay(50);
}

void test_task_basic(void)
{
    osal_task_handle_t h = NULL;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_task_create(sleeper, "sleeper", 2048, NULL, 1, &h));
    TEST_ASSERT_NOT_NULL(h);
    const char *name = osal_task_get_name(h);
    TEST_ASSERT_NOT_NULL(name);
    osal_task_delay(10);
    TEST_ASSERT_TRUE(osal_task_get_tick_count() > 0);
    osal_task_delete(h);

    /* allow join */
    osal_task_delay(50);
}
