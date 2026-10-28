/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_osal_console_prototypes.h
 * @brief Prototypes for the POSIX esp_console shim test suite.
 */

#ifndef __TEST_OSAL_CONSOLE_PROTOTYPES_H__
#define __TEST_OSAL_CONSOLE_PROTOTYPES_H__

/* Individual tests */
void test_console_register_and_dispatch(void);
void test_console_unknown_command(void);
void test_console_empty_line(void);
void test_console_replace_same_name(void);
void test_console_help_command(void);

/* Aggregate runner */
int test_osal_console_all_tests_unity(void);

#endif /* __TEST_OSAL_CONSOLE_PROTOTYPES_H__ */
