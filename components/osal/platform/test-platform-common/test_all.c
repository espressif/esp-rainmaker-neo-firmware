/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_platform_common_prototypes.h"

int test_platform_common_all_tests_unity(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ticks_basic);
    RUN_TEST(test_tick_math_conversion);
    RUN_TEST(test_tick_math_period_clamp);
    RUN_TEST(test_timer_chain_boundary);
    RUN_TEST(test_timer_chain_no_split);
    RUN_TEST(test_timer_chain_one_shot_split);
    RUN_TEST(test_timer_chain_one_shot_split_boundaries);
    RUN_TEST(test_timer_chain_periodic_no_drift);
    RUN_TEST(test_timer_chain_periodic_restart);
    RUN_TEST(test_timer_chain_real_cap);
    RUN_TEST(test_timer_chain_invariants);
    RUN_TEST(test_mutex_basic);
    RUN_TEST(test_recursive_mutex_basic);
    RUN_TEST(test_recursive_mutex_unbalanced_give_fails);
    RUN_TEST(test_binary_basic);
    RUN_TEST(test_counting_basic);
    RUN_TEST(test_queue_basic);
    RUN_TEST(test_queue_ext_basic);
    RUN_TEST(test_event_group_basic);
    RUN_TEST(test_event_group_sync);
    RUN_TEST(test_event_group_null_handle_contract);
    RUN_TEST(test_semaphore_null_handle_contract);
    RUN_TEST(test_event_loop_basic);
    RUN_TEST(test_scheduler_basic);
    RUN_TEST(test_scheduler_periodic_basic);
    RUN_TEST(test_scheduler_periodic_virtual_time_advance);
    RUN_TEST(test_task_basic);
    RUN_TEST(test_random_basic);
    RUN_TEST(test_sysinfo_platform_name);
    RUN_TEST(test_sysinfo_base_mac);
    return UNITY_END();
}
