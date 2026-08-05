/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_json_common_prototypes.h
 * @brief Prototypes for the JSON Common test
 */

#ifndef TEST_JSON_COMMON_PROTOTYPES_H
#define TEST_JSON_COMMON_PROTOTYPES_H

/* Prototypes ****************************************************************/

/* --- Basic tests --- */

/**
 * @brief Test the basic JSON parser functionality
 */
void test_json_parser_basic(void);

/**
 * @brief Test the basic JSON generator functionality
 */
void test_json_generator_basic(void);

/* --- All tests --- */

int test_json_common_all_tests_unity(void);

#endif /* TEST_JSON_COMMON_PROTOTYPES_H */
