/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_timesync_common_prototypes.h"

int test_timesync_common_all_tests_unity(void)
{
    UNITY_BEGIN();

    // Basic tests
    RUN_TEST(test_timesync_init_basic);
    RUN_TEST(test_timesync_init_with_config);
    RUN_TEST(test_timesync_sync_status);
    RUN_TEST(test_timesync_time_validation);
    RUN_TEST(test_timesync_function_pointers);
    RUN_TEST(test_timesync_local_time_string);
    RUN_TEST(test_timesync_set_time_rejects_invalid);
    RUN_TEST(test_timesync_set_time_preserves_timezone);
    RUN_TEST(test_timesync_epoch_ms_is_valid);
    RUN_TEST(test_timesync_is_synced_not_gated_on_init);

    // Timezone tests
    RUN_TEST(test_timesync_timezone_posix);
    RUN_TEST(test_timesync_timezone_location);
    RUN_TEST(test_timesync_timezone_database);
    RUN_TEST(test_timesync_timezone_error_cases);

    // Storage tests
    RUN_TEST(test_timesync_storage_basic);
    RUN_TEST(test_timesync_storage_error_cases);
    RUN_TEST(test_timesync_timezone_persistence);

    return UNITY_END();
}
