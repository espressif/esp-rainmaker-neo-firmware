/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_osal_http_config.h
 * @brief Endpoints for the HTTP test suite.
 */

#ifndef __TEST_OSAL_HTTP_CONFIG_H__
#define __TEST_OSAL_HTTP_CONFIG_H__

/**
 * @brief HTTP Test Server base URLs
 *
 * @note Default to the public httpbin.org API. CI overrides these to a local
 * httpbin sidecar (HTTP :8080, HTTPS :8443) because the public service
 * rate-limits/503s and made these tests flaky; see README.md. Endpoints
 * (/get, /post, /put, ...) are appended by the tests.
 *
 * Set via Kconfig (CONFIG_TEST_OSAL_HTTP_URL_{TCP,TLS}); see test_osal_http/Kconfig.
 * Defined without parentheses so tests can concatenate endpoint paths as string literals,
 * e.g. TEST_OSAL_HTTP_URL_TLS "/get".
 */
#include "sdkconfig.h"

#define TEST_OSAL_HTTP_URL_TCP             CONFIG_TEST_OSAL_HTTP_URL_TCP
#define TEST_OSAL_HTTP_URL_TLS             CONFIG_TEST_OSAL_HTTP_URL_TLS

/**
 * @brief HTTP Test Timeout (ms)
 *
 * @note Timeout for HTTP test operations in milliseconds.
 */
#define TEST_OSAL_HTTP_TIMEOUT_MS          ( 10000 )

/**
 * @brief HTTP Test Buffer Size
 *
 * @note Buffer size for HTTP test operations.
 */
#define TEST_OSAL_HTTP_BUFFER_SIZE         ( 4096 )

#endif /* __TEST_OSAL_HTTP_CONFIG_H__ */
