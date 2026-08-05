/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_console_prototypes.h
 * @brief Prototypes for the POSIX esp_console shim test suite.
 */

#pragma once

/* Individual tests */
void test_console_register_and_dispatch(void);
void test_console_unknown_command(void);
void test_console_empty_line(void);
void test_console_replace_same_name(void);
void test_console_help_command(void);

/* Aggregate runner */
int test_esp_console_posix_all_tests_unity(void);
