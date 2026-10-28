/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_osal_json_prototypes.h
 * @brief Prototypes for the JSON Common test
 */

#ifndef __TEST_OSAL_JSON_PROTOTYPES_H__
#define __TEST_OSAL_JSON_PROTOTYPES_H__

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

int test_osal_json_all_tests_unity(void);

#endif /* __TEST_OSAL_JSON_PROTOTYPES_H__ */
