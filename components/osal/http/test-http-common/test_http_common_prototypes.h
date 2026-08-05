/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_http_common_prototypes.h
 * @brief Prototypes for the HTTP Common test component
 */

/* Includes ********************************************************************/

/* Standard C includes */
#include <stdbool.h>

/* Functions ******************************************************************/

/* --- Basic tests --- */

/**
 * @brief Test basic HTTP GET functionality over TCP
 */
void test_http_get_no_tls(void);

/**
 * @brief Test basic HTTP POST functionality over TCP
 */
void test_http_post_no_tls(void);

/**
 * @brief Test basic HTTPS GET functionality
 */
void test_http_get_tls(void);

/**
 * @brief Test basic HTTPS POST functionality
 */
void test_http_post_tls(void);

/**
 * @brief Test HTTP streaming operations
 */
void test_http_streaming(void);

/**
 * @brief Test HTTP HEAD method
 */
void test_http_head_method(void);

/**
 * @brief Test HTTP OPTIONS method
 */
void test_http_options_method(void);

/**
 * @brief Test HTTP PUT method
 */
void test_http_put_method(void);

/**
 * @brief Test HTTP PATCH method
 */
void test_http_patch_method(void);

/**
 * @brief Test HTTP DELETE method
 */
void test_http_delete_method(void);

/**
 * @brief Test multiple requests with the same handle
 */
void test_http_multiple_requests(void);

/* --- All tests --- */

/**
 * @brief Run all tests for the HTTP Common test component
 */
int test_http_common_all_tests_unity(void);
