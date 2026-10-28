/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_osal_json_prototypes.h"

int test_osal_json_all_tests_unity(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_json_parser_basic);
    RUN_TEST(test_json_generator_basic);
    return UNITY_END();
}
