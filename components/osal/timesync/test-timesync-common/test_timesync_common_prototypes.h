/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_timesync_common_prototypes.h
 * @brief Prototypes for the Timesync Common test
 */

#ifndef TEST_OSAL_TIMESYNC_PROTOTYPES_H
#define TEST_OSAL_TIMESYNC_PROTOTYPES_H

/* Prototypes ****************************************************************/

/* --- Basic tests --- */

/**
 * @brief Test basic timesync initialization and cleanup
 */
void test_timesync_init_basic(void);

/**
 * @brief Test timesync initialization with configuration
 */
void test_timesync_init_with_config(void);

/**
 * @brief Test time synchronization status checking
 */
void test_timesync_sync_status(void);

/**
 * @brief Test time validation functions
 */
void test_timesync_time_validation(void);

/**
 * @brief Test function pointer injection for testing
 */
void test_timesync_function_pointers(void);

/**
 * @brief Test local time string generation
 */
void test_timesync_local_time_string(void);

/**
 * @brief Test that osal_timesync_set_time rejects non-positive epoch-ms values
 */
void test_timesync_set_time_rejects_invalid(void);

/**
 * @brief Test that osal_timesync_set_time sets UTC only and preserves the timezone
 */
void test_timesync_set_time_preserves_timezone(void);

/**
 * @brief Test the epoch-ms validity predicate against the reference floor
 */
void test_timesync_epoch_ms_is_valid(void);

/**
 * @brief Test that osal_timesync_is_synced tracks the clock, not the init flag
 */
void test_timesync_is_synced_not_gated_on_init(void);

/* --- Timezone tests --- */

/**
 * @brief Test setting and getting POSIX timezone
 */
void test_timesync_timezone_posix(void);

/**
 * @brief Test setting and getting location-based timezone
 */
void test_timesync_timezone_location(void);

/**
 * @brief Test timezone database lookup
 */
void test_timesync_timezone_database(void);

/**
 * @brief Test error handling for invalid timezone inputs
 */
void test_timesync_timezone_error_cases(void);

/* --- Storage tests --- */

/**
 * @brief Test storage operations for timezone persistence
 */
void test_timesync_storage_basic(void);

/**
 * @brief Test storage error handling
 */
void test_timesync_storage_error_cases(void);

/**
 * @brief Test timezone persistence across storage operations
 */
void test_timesync_timezone_persistence(void);

/* --- All tests --- */

int test_timesync_common_all_tests_unity(void);

#endif /* TEST_OSAL_TIMESYNC_PROTOTYPES_H */
