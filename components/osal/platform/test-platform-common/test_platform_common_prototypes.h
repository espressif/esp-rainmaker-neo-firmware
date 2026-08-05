/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_platform_common_prototypes.h
 * @brief Prototypes for the OSAL platform test suite.
 */

#ifndef OSAL_TEST_PROTOTYPES_H
#define OSAL_TEST_PROTOTYPES_H

/* --- Basic tests --- */

/**
 * @brief Test the ticks functionality
 */
void test_ticks_basic(void);

/**
 * @brief Test overflow-safe ms-to-tick conversion
 */
void test_tick_math_conversion(void);

/**
 * @brief Test clamping a tick count to one legal timer period
 */
void test_tick_math_period_clamp(void);

/**
 * @brief Test the periodic boundary arithmetic a split delay chains through
 */
void test_timer_chain_boundary(void);

/**
 * @brief Test that a delay fitting one timer period is not chained
 */
void test_timer_chain_no_split(void);

/**
 * @brief Test the arms a split one-shot delay walks through
 */
void test_timer_chain_one_shot_split(void);

/**
 * @brief Test a split one-shot delay at the cap, its multiples and a short clock
 */
void test_timer_chain_one_shot_split_boundaries(void);

/**
 * @brief Test that a split periodic delay holds its cadence instead of drifting
 */
void test_timer_chain_periodic_no_drift(void);

/**
 * @brief Test that an elapsed split periodic delay both runs and restarts
 */
void test_timer_chain_periodic_restart(void);

/**
 * @brief Test split delays against the real 32-bit timer period cap
 */
void test_timer_chain_real_cap(void);

/**
 * @brief Test the invariants a split delay must hold for arbitrary inputs
 */
void test_timer_chain_invariants(void);

/**
 * @brief Test the mutex functionality
 */
void test_mutex_basic(void);
void test_recursive_mutex_basic(void);
void test_recursive_mutex_unbalanced_give_fails(void);

/**
 * @brief Test the binary semaphore functionality
 */
void test_binary_basic(void);

/**
 * @brief Test the counting semaphore functionality
 */
void test_counting_basic(void);

/**
 * @brief Test the queue functionality
 */
void test_queue_basic(void);

/**
 * @brief Test the external-RAM queue functionality
 */
void test_queue_ext_basic(void);

/**
 * @brief Test the event group functionality
 */
void test_event_group_basic(void);

/**
 * @brief Test the event group sync functionality
 */
void test_event_group_sync(void);

/**
 * @brief Test that NULL event-group handles return no bits instead of asserting
 */
void test_event_group_null_handle_contract(void);

/**
 * @brief Test that NULL semaphore handles return an error instead of asserting
 */
void test_semaphore_null_handle_contract(void);

/**
 * @brief Test the event loop functionality
 */
void test_event_loop_basic(void);

/**
 * @brief Test the scheduler functionality
 */
void test_scheduler_basic(void);

/**
 * @brief Test the periodic scheduler basic functionality
 */
void test_scheduler_periodic_basic(void);

/**
 * @brief Test the periodic scheduler virtual time advance functionality
 */
void test_scheduler_periodic_virtual_time_advance(void);
/**
 * @brief Test the task functionality
 */
void test_task_basic(void);

/**
 * @brief Test the random number generation functionality
 */
void test_random_basic(void);

/**
 * @brief Test the platform name lookup
 */
void test_sysinfo_platform_name(void);

/**
 * @brief Test the base MAC address lookup
 */
void test_sysinfo_base_mac(void);

/* --- All tests --- */

/**
 * @brief Run all tests for the Platform Common test component
 */
int test_platform_common_all_tests_unity(void);

#endif /* OSAL_TEST_PROTOTYPES_H */
