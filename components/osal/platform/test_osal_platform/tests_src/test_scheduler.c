/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include <time.h>
#include "osal_scheduler.h"
#include "osal_time_control.h"
#include "osal_task.h"

static volatile int fired = 0;
static void cb(void *arg)
{
    (void)arg; fired++;
}

static volatile int periodic_fired = 0;
static void periodic_cb(void *arg)
{
    (void)arg; periodic_fired++;
}

#define VIRTUAL_SCHEDULER_TOLERANCE_MS 1000 // 1000ms tolerance for the virtual scheduler to run

// note: we give ample delays after time advancement for the virtual scheduler to run
void test_scheduler_basic(void)
{
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_init());
    osal_scheduler_task_handle_t h = NULL;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_schedule_task(&h, 200, cb, NULL));
    TEST_ASSERT_NOT_NULL(h);
    osal_task_delay( osal_ticks_from_ms(VIRTUAL_SCHEDULER_TOLERANCE_MS + 200) );
    TEST_ASSERT_EQUAL_INT(1, fired);

    /* Test virtual scheduler */
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_reset_timer(h, 3600000));
    TEST_ASSERT_EQUAL_INT(1, fired);
    osal_time_control_advance_time(3600);
    osal_task_delay( osal_ticks_from_ms(VIRTUAL_SCHEDULER_TOLERANCE_MS) );
    TEST_ASSERT_EQUAL_INT(2, fired);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_cancel_task(&h));
    TEST_ASSERT_NULL(h);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_schedule_task(&h, 12345678000, cb, NULL));
    osal_time_control_set_time(osal_get_time(NULL) + 12345678);
    osal_task_delay( osal_ticks_from_ms(VIRTUAL_SCHEDULER_TOLERANCE_MS) );
    TEST_ASSERT_EQUAL_INT(3, fired);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_deinit());
}

void test_scheduler_periodic_basic(void)
{
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_init());

    osal_scheduler_task_handle_t h = NULL;
    periodic_fired = 0;

    // Schedule a periodic task with 100ms interval
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_schedule_task_periodic(&h, 100, periodic_cb, NULL));
    TEST_ASSERT_NOT_NULL(h);

    // Wait for multiple executions
    osal_task_delay( osal_ticks_from_ms(VIRTUAL_SCHEDULER_TOLERANCE_MS + 350) );
    TEST_ASSERT_TRUE(periodic_fired >= 3); // Should fire at least 3 times in 350ms with 100ms interval

    // Cancel the periodic task
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_cancel_task(&h));
    TEST_ASSERT_NULL(h);

    // Store current count to verify it stops firing
    int final_count = periodic_fired;
    osal_task_delay( osal_ticks_from_ms(300) );
    TEST_ASSERT_EQUAL_INT(final_count, periodic_fired); // Should not fire anymore

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_deinit());
}

void test_scheduler_periodic_virtual_time_advance(void)
{
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_init());

    osal_scheduler_task_handle_t h = NULL;
    periodic_fired = 0;

    // Schedule a periodic task with 1000ms (1 second) interval
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_schedule_task_periodic(&h, 1000, periodic_cb, NULL));
    TEST_ASSERT_NOT_NULL(h);

    // Advance virtual time by 3500ms (3.5 seconds)
    // This should cause the periodic task to fire 3 times (for the missed 1000ms, 2000ms, 3000ms intervals)
    // plus any additional executions from the time advancement itself
    osal_time_control_advance_time(3500);
    osal_task_delay( osal_ticks_from_ms(VIRTUAL_SCHEDULER_TOLERANCE_MS) );

    // Should have fired additional times due to catch-up
    // Expected: 3 (for 1000ms, 2000ms, 3000ms intervals) + possibly 1 more for 4000ms
    int expected_additional = 3; // At minimum, should catch up for 3 missed periods
    TEST_ASSERT_TRUE(periodic_fired >= expected_additional);

    // Store count before next advance
    int count_after_first_advance = periodic_fired;

    // Advance time by another 2500ms (2.5 seconds)
    // Should fire 2 more times (for 1000ms and 2000ms intervals)
    osal_time_control_advance_time(2500);
    osal_task_delay( osal_ticks_from_ms(VIRTUAL_SCHEDULER_TOLERANCE_MS) );

    expected_additional = 2; // Should catch up for 2 missed periods
    TEST_ASSERT_TRUE(periodic_fired >= count_after_first_advance + expected_additional);

    // Test time set to future (different from advance)
    int count_before_set = periodic_fired;
    time_t future_time = osal_get_time(NULL) + 4000; // 4 seconds in future
    osal_time_control_set_time(future_time);
    osal_task_delay( osal_ticks_from_ms(VIRTUAL_SCHEDULER_TOLERANCE_MS) );

    // Should have caught up for the 4 missed seconds
    expected_additional = 4; // Should catch up for 4 missed periods
    TEST_ASSERT_TRUE(periodic_fired >= count_before_set + expected_additional);

    // Cancel the periodic task
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_cancel_task(&h));
    TEST_ASSERT_NULL(h);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_deinit());
}
