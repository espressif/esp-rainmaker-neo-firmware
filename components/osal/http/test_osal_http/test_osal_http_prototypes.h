/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_osal_http_prototypes.h
 * @brief Prototypes for the HTTP Common test component
 */

#ifndef __TEST_OSAL_HTTP_PROTOTYPES_H__
#define __TEST_OSAL_HTTP_PROTOTYPES_H__

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

/* --- Contract tests --- */

/**
 * @brief A NULL header value removes the header from the next request
 */
void test_http_contract_null_value_removes_header(void);

/**
 * @brief The removal of a header that was never set succeeds
 */
void test_http_contract_removing_absent_header_succeeds(void);

/**
 * @brief A NULL content type keeps the current Content-Type header
 */
void test_http_contract_null_content_type_keeps_header(void);

/**
 * @brief A body set before the method keeps both
 */
void test_http_contract_body_before_method(void);

/**
 * @brief A body does not change the method
 */
void test_http_contract_body_keeps_method(void);

/**
 * @brief POST with an empty body sends Content-Length: 0
 */
void test_http_contract_empty_body_sends_length_zero(void);

/**
 * @brief A reused handle does not inherit the previous method
 */
void test_http_contract_method_not_stale(void);

/* --- All tests --- */

/**
 * @brief Run all tests for the HTTP Common test component
 */
int test_osal_http_all_tests_unity(void);

#endif /* __TEST_OSAL_HTTP_PROTOTYPES_H__ */
