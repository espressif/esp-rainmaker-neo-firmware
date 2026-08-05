/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_console_prototypes.h"

int test_esp_console_posix_all_tests_unity(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_console_register_and_dispatch);
    RUN_TEST(test_console_unknown_command);
    RUN_TEST(test_console_empty_line);
    RUN_TEST(test_console_replace_same_name);
    RUN_TEST(test_console_help_command);
    return UNITY_END();
}
