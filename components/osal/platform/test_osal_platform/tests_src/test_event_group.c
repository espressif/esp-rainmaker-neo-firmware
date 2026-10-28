/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "osal_event_group.h"
#include "osal_task.h"
#include <stddef.h>

#define __SYNC_BITS_TO_SET 0x03
#define __SYNC_BITS_TO_WAIT_FOR 0x28

void test_event_group_basic(void)
{
    osal_event_group_handle_t eg = osal_event_group_create();
    TEST_ASSERT_NOT_NULL(eg);
    /* Initially zero */
    TEST_ASSERT_EQUAL_HEX32(0, osal_event_group_get_bits(eg));

    /* Set bits */
    osal_event_group_bits_t before = osal_event_group_set_bits(eg, 0x5);
    TEST_ASSERT_EQUAL_HEX32(0x5, before);
    TEST_ASSERT_EQUAL_HEX32(0x5, osal_event_group_get_bits(eg));

    /* Wait any without clear */
    osal_event_group_bits_t waited = osal_event_group_wait_bits(eg, 0x1, false, false, 0);
    TEST_ASSERT_BITS_HIGH(0x5, waited);
    TEST_ASSERT_EQUAL_HEX32(0x5, osal_event_group_get_bits(eg));

    /* Wait all with clear */
    waited = osal_event_group_wait_bits(eg, 0x5, true, true, 0);
    TEST_ASSERT_BITS_HIGH(0x5, waited);
    TEST_ASSERT_EQUAL_HEX32(0x0, osal_event_group_get_bits(eg));

    osal_event_group_delete(eg);
}

static void __sync_task(void *arg)
{
    osal_event_group_handle_t eg = (osal_event_group_handle_t)arg;
    osal_event_group_wait_bits(eg, __SYNC_BITS_TO_SET, false, false, 0);
    osal_event_group_set_bits(eg, __SYNC_BITS_TO_WAIT_FOR);

    osal_task_delete(NULL);
}

void test_event_group_sync(void)
{
    osal_event_group_handle_t eg = osal_event_group_create();
    TEST_ASSERT_NOT_NULL(eg);
    TEST_ASSERT_EQUAL_HEX32(0, osal_event_group_get_bits(eg));

    osal_task_handle_t task = NULL;
    osal_task_create(__sync_task, "sync_task", 1024, eg, 1, &task);
    TEST_ASSERT_NOT_NULL(task);

    osal_event_group_bits_t before = osal_event_group_sync(eg, __SYNC_BITS_TO_SET, __SYNC_BITS_TO_WAIT_FOR, osal_ticks_from_ms(100));
    TEST_ASSERT_EQUAL_HEX32(__SYNC_BITS_TO_SET | __SYNC_BITS_TO_WAIT_FOR, before);
    TEST_ASSERT_EQUAL_HEX32(__SYNC_BITS_TO_SET, osal_event_group_get_bits(eg));
}

void test_event_group_null_handle_contract(void)
{
    /* Every bit op reports "no bits" on NULL instead of asserting. */
    TEST_ASSERT_EQUAL_HEX32(0, osal_event_group_get_bits(NULL));
    TEST_ASSERT_EQUAL_HEX32(0, osal_event_group_set_bits(NULL, 0x1));
    TEST_ASSERT_EQUAL_HEX32(0, osal_event_group_clear_bits(NULL, 0x1));
    TEST_ASSERT_EQUAL_HEX32(0, osal_event_group_wait_bits(NULL, 0x1, false, false, 0));
    TEST_ASSERT_EQUAL_HEX32(0, osal_event_group_sync(NULL, 0x1, 0x1, 0));
    /* No-op, not a fault: reaching the next line is the assertion. */
    osal_event_group_delete(NULL);
}
