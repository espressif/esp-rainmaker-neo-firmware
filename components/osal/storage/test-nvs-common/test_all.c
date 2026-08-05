/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_nvs_common_prototypes.h"

int test_nvs_common_all_tests_unity(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nvs_basic_flow);
    RUN_TEST(test_nvs_iterator_basic_flow);
    RUN_TEST(test_nvs_error_paths);
    RUN_TEST(test_nvs_partition_deinit_isolates_labels);
    RUN_TEST(test_nvs_partition_open_after_deinit_own_label_only);
    RUN_TEST(test_nvs_partition_reset_isolates_labels);
    RUN_TEST(test_nvs_partition_entry_find_after_peer_deinit);
    return UNITY_END();
}
