/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_http_basic.c
 * @brief Test basic HTTP functionality
 */

#include "unity.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "osal_http_impl.h"
#include "test_http_common_config.h"

#define TIMEOUT_MS 5000

static osal_http_impl_t http_impl;

/* Helper function to initialize HTTP implementation */
static void init_http_impl(void)
{
    osal_err_t status = osal_http_impl_setup(&http_impl);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    TEST_ASSERT_TRUE(http_impl.setup_done);
}

typedef void (*basic_config_func_t)(osal_http_config_t *config, const char *url, osal_http_method_t method);

/* Helper function to create basic config for HTTP */
static void create_basic_config_http(osal_http_config_t *config, const char *url, osal_http_method_t method)
{
    memset(config, 0, sizeof(osal_http_config_t));
    config->url = url;
    config->method = method;
    config->timeout_ms = TEST_HTTP_COMMON_TIMEOUT_MS;
    config->buffer_size_tx = TEST_HTTP_COMMON_BUFFER_SIZE;
    config->buffer_size_rx = TEST_HTTP_COMMON_BUFFER_SIZE;
    config->transport_type = OSAL_HTTP_TRANSPORT_OVER_TCP;
}

/* Helper function to create basic config for TLS (uses platform CA bundle: ca-bundle-posix on POSIX, ESP cert bundle on ESP) */
static void create_basic_config_tls(osal_http_config_t *config, const char *url, osal_http_method_t method)
{
    create_basic_config_http(config, url, method);
    config->transport_type = OSAL_HTTP_TRANSPORT_OVER_SSL;
}

static void free_basic_config_tls(osal_http_config_t *config)
{
    (void)config;
}

/* Global variables for callback testing */
static uint8_t response_buffer[TEST_HTTP_COMMON_BUFFER_SIZE];
static size_t response_buffer_pos = 0;

/* Read callback function for collecting response data */
static int test_read_callback(uint8_t *data, size_t data_len, int64_t content_length)
{
    /* Make sure we don't overflow the buffer */
    if (response_buffer_pos + data_len > sizeof(response_buffer)) {
        return -1; /* Signal error */
    }

    memcpy(response_buffer + response_buffer_pos, data, data_len);
    response_buffer_pos += data_len;

    return data_len; /* Return number of bytes processed */
}

static void _http_get_basic_no_close(osal_http_handle_t handle, int request_id, const char *url)
{
    osal_err_t status;
    int status_code;
    int64_t content_length;

    /* Set URL and method */
    status = http_impl.set_url(handle, url);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    status = http_impl.set_method(handle, OSAL_HTTP_METHOD_GET);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Set request ID header */
    char request_id_int_str[16];
    snprintf(request_id_int_str, sizeof(request_id_int_str), "%d", request_id);
    status = http_impl.set_header(handle, "X-Request-Number", request_id_int_str);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Set read callback */
    status = http_impl.set_read_callback(handle, test_read_callback);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    response_buffer_pos = 0;

    /* Perform the request synchronously */
    status = http_impl.perform(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Get status and content length */
    status_code = http_impl.get_status_code(handle);
    TEST_ASSERT_TRUE(status_code >= 200 && status_code < 300);

    content_length = http_impl.get_content_length(handle);
    TEST_ASSERT_GREATER_THAN(0, content_length);

    /* Verify we received exact content length */
    TEST_ASSERT_EQUAL(content_length, response_buffer_pos);
    response_buffer[response_buffer_pos] = '\0';

    /* Basic validation - should contain JSON response */
    TEST_ASSERT_TRUE(strstr((char *)response_buffer, "url") != NULL);
    char request_id_str[64];
    snprintf(request_id_str, sizeof(request_id_str), "\"X-Request-Number\": \"%s\"", request_id_int_str);
    TEST_ASSERT_TRUE(strstr((char *)response_buffer, request_id_str) != NULL);
}

static void _http_post_basic_no_close(osal_http_handle_t handle, int request_id, const char *url)
{
    osal_err_t status;
    int status_code;
    int64_t content_length;
    const char *post_data = "{\"test\": \"data\", \"message\": \"hello world\"}";

    /* Reset response buffer */
    response_buffer_pos = 0;

    /* Set URL and method */
    status = http_impl.set_url(handle, url);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    status = http_impl.set_method(handle, OSAL_HTTP_METHOD_POST);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Set request ID header */
    char request_id_int_str[16];
    snprintf(request_id_int_str, sizeof(request_id_int_str), "%d", request_id);
    status = http_impl.set_header(handle, "X-Request-Number", request_id_int_str);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Set request body */
    status = http_impl.set_request_body(handle, (uint8_t *)post_data, strlen(post_data), "application/json");
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Set read callback */
    status = http_impl.set_read_callback(handle, test_read_callback);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Perform the request synchronously */
    status = http_impl.perform(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Get status and content length */
    status_code = http_impl.get_status_code(handle);
    TEST_ASSERT_TRUE(status_code >= 200 && status_code < 300);

    content_length = http_impl.get_content_length(handle);
    TEST_ASSERT_GREATER_THAN(0, content_length);

    /* Verify we received exact content length */
    TEST_ASSERT_EQUAL(content_length, response_buffer_pos);
    response_buffer[response_buffer_pos] = '\0';

    /* Basic validation - should contain our POST data */
    TEST_ASSERT_TRUE(strstr((char *)response_buffer, "\"test\": \"data\"") != NULL);
    TEST_ASSERT_TRUE(strstr((char *)response_buffer, "\"message\": \"hello world\"") != NULL);
    TEST_ASSERT_TRUE(strstr((char *)response_buffer, "\"Content-Type\": \"application/json\"") != NULL);
    char request_id_str[64];
    snprintf(request_id_str, sizeof(request_id_str), "\"X-Request-Number\": \"%d\"", request_id);
}

void _test_http_get_basic(basic_config_func_t config_func, const char *url)
{
    osal_http_handle_t handle = NULL;
    osal_http_config_t config;
    osal_err_t status;

    /* Step 1: Initialize HTTP implementation */
    init_http_impl();

    /* Create config with certificate for HTTPS */
    config_func(&config, url, OSAL_HTTP_METHOD_GET);

    /* Step 2: Initialize HTTP client */
    status = http_impl.init(&config, &handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    TEST_ASSERT_NOT_NULL(handle);

    /* Test GET request */
    _http_get_basic_no_close(handle, 0, url);

    /* Step 10: Cleanup */
    status = http_impl.cleanup(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    free_basic_config_tls(&config);
}

void _test_http_post_basic(basic_config_func_t config_func, const char *url)
{
    osal_http_handle_t handle = NULL;
    osal_http_config_t config;
    osal_err_t status;

    /* Step 1: Initialize HTTP implementation */
    init_http_impl();

    /* Create config with certificate for HTTPS */
    config_func(&config, url, OSAL_HTTP_METHOD_POST);

    /* Step 2: Initialize HTTP client */
    status = http_impl.init(&config, &handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    TEST_ASSERT_NOT_NULL(handle);

    /* Test POST request */
    _http_post_basic_no_close(handle, 0, url);

    /* Step 10: Cleanup */
    status = http_impl.cleanup(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    free_basic_config_tls(&config);
}

void test_http_get_no_tls(void)
{
    _test_http_get_basic(create_basic_config_http, TEST_HTTP_COMMON_URL_TCP "/get");
}

void test_http_post_no_tls(void)
{
    _test_http_post_basic(create_basic_config_http, TEST_HTTP_COMMON_URL_TCP "/post");
}

void test_http_get_tls(void)
{
    _test_http_get_basic(create_basic_config_tls, TEST_HTTP_COMMON_URL_TLS "/get");
}

void test_http_post_tls(void)
{
    _test_http_post_basic(create_basic_config_tls, TEST_HTTP_COMMON_URL_TLS "/post");
}

void test_http_streaming(void)
{
    osal_http_handle_t handle = NULL;
    osal_http_config_t config;
    osal_err_t status;
    int status_code;

    /* Reset response buffer */
    response_buffer_pos = 0;

    /* Step 1: Initialize HTTP implementation */
    init_http_impl();

    /* Create config with certificate for HTTPS */
    create_basic_config_tls(&config, TEST_HTTP_COMMON_URL_TLS "/stream/10", OSAL_HTTP_METHOD_GET);

    /* Step 2: Initialize HTTP client */
    status = http_impl.init(&config, &handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    TEST_ASSERT_NOT_NULL(handle);

    status = http_impl.set_method(handle, OSAL_HTTP_METHOD_GET);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Set read callback */
    status = http_impl.set_read_callback(handle, test_read_callback);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Perform the request synchronously */
    status = http_impl.perform(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Get status and content length */
    status_code = http_impl.get_status_code(handle);
    TEST_ASSERT_TRUE(status_code >= 200 && status_code < 300);

    /* Should have read some data */
    TEST_ASSERT_TRUE(response_buffer_pos > 0);

    /* Step 10: Cleanup */
    status = http_impl.cleanup(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    free_basic_config_tls(&config);
}

void test_http_head_method(void)
{
    osal_http_handle_t handle = NULL;
    osal_http_config_t config;
    osal_err_t status;
    int status_code;

    /* Reset response buffer */
    response_buffer_pos = 0;

    /* Step 1: Initialize HTTP implementation */
    init_http_impl();

    /* Create config with certificate for HTTPS */
    create_basic_config_tls(&config, TEST_HTTP_COMMON_URL_TLS "/get", OSAL_HTTP_METHOD_HEAD);

    /* Step 2: Initialize HTTP client */
    status = http_impl.init(&config, &handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    TEST_ASSERT_NOT_NULL(handle);

    /* Set read callback (should not be called for HEAD) */
    status = http_impl.set_read_callback(handle, test_read_callback);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Perform the request synchronously */
    status = http_impl.perform(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Get status and content length */
    status_code = http_impl.get_status_code(handle);
    TEST_ASSERT_TRUE(status_code >= 200 && status_code < 300);

    int64_t content_length = http_impl.get_content_length(handle);
    TEST_ASSERT_GREATER_THAN(0, content_length);

    /* HEAD requests should not have a response body */
    TEST_ASSERT_EQUAL(0, response_buffer_pos);

    /* Step 10: Cleanup */
    status = http_impl.cleanup(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    free_basic_config_tls(&config);
}

void test_http_options_method(void)
{
    osal_http_handle_t handle = NULL;
    osal_http_config_t config;
    osal_err_t status;
    int status_code;

    /* Reset response buffer */
    response_buffer_pos = 0;

    /* Step 1: Initialize HTTP implementation */
    init_http_impl();

    /* Create config with certificate for HTTPS */
    create_basic_config_tls(&config, TEST_HTTP_COMMON_URL_TLS "/get", OSAL_HTTP_METHOD_OPTIONS);

    /* Step 2: Initialize HTTP client */
    status = http_impl.init(&config, &handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    TEST_ASSERT_NOT_NULL(handle);

    /* Set read callback */
    status = http_impl.set_read_callback(handle, test_read_callback);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Perform the request synchronously */
    status = http_impl.perform(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Get status and content length */
    status_code = http_impl.get_status_code(handle);
    TEST_ASSERT_TRUE(status_code >= 200 && status_code < 300);

    int64_t content_length = http_impl.get_content_length(handle);
    /* OPTIONS might return content length 0 */
    TEST_ASSERT_TRUE(content_length >= 0);

    /* Step 10: Cleanup */
    status = http_impl.cleanup(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    free_basic_config_tls(&config);
}

void test_http_put_method(void)
{
    osal_http_handle_t handle = NULL;
    osal_http_config_t config;
    osal_err_t status;
    int status_code;
    const char *put_data = "{\"test\": \"put_data\", \"method\": \"PUT\"}";

    /* Reset response buffer */
    response_buffer_pos = 0;

    /* Step 1: Initialize HTTP implementation */
    init_http_impl();

    /* Create config with certificate for HTTPS */
    create_basic_config_tls(&config, TEST_HTTP_COMMON_URL_TLS "/put", OSAL_HTTP_METHOD_PUT);

    /* Step 2: Initialize HTTP client */
    status = http_impl.init(&config, &handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    TEST_ASSERT_NOT_NULL(handle);

    /* Set request body */
    status = http_impl.set_request_body(handle, (uint8_t *)put_data, strlen(put_data), "application/json");
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Set read callback */
    status = http_impl.set_read_callback(handle, test_read_callback);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Perform the request synchronously */
    status = http_impl.perform(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Get status and content length */
    status_code = http_impl.get_status_code(handle);
    TEST_ASSERT_TRUE(status_code >= 200 && status_code < 300);

    int64_t content_length = http_impl.get_content_length(handle);
    TEST_ASSERT_GREATER_THAN(0, content_length);

    /* Verify we received exact content length */
    TEST_ASSERT_EQUAL(content_length, response_buffer_pos);
    response_buffer[response_buffer_pos] = '\0';

    /* Should contain our PUT data */
    TEST_ASSERT_TRUE(strstr((char *)response_buffer, "\"test\": \"put_data\"") != NULL);
    TEST_ASSERT_TRUE(strstr((char *)response_buffer, "\"method\": \"PUT\"") != NULL);

    /* Step 10: Cleanup */
    status = http_impl.cleanup(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    free_basic_config_tls(&config);
}

void test_http_patch_method(void)
{
    osal_http_handle_t handle = NULL;
    osal_http_config_t config;
    osal_err_t status;
    int status_code;
    const char *patch_data = "{\"test\": \"patch_data\", \"method\": \"PATCH\"}";

    /* Reset response buffer */
    response_buffer_pos = 0;

    /* Step 1: Initialize HTTP implementation */
    init_http_impl();

    /* Create config with certificate for HTTPS */
    create_basic_config_tls(&config, TEST_HTTP_COMMON_URL_TLS "/patch", OSAL_HTTP_METHOD_PATCH);

    /* Step 2: Initialize HTTP client */
    status = http_impl.init(&config, &handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    TEST_ASSERT_NOT_NULL(handle);

    /* Set request body */
    status = http_impl.set_request_body(handle, (uint8_t *)patch_data, strlen(patch_data), "application/json");
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Set read callback */
    status = http_impl.set_read_callback(handle, test_read_callback);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Perform the request synchronously */
    status = http_impl.perform(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Get status and content length */
    status_code = http_impl.get_status_code(handle);
    TEST_ASSERT_TRUE(status_code >= 200 && status_code < 300);

    int64_t content_length = http_impl.get_content_length(handle);
    TEST_ASSERT_GREATER_THAN(0, content_length);

    /* Verify we received exact content length */
    TEST_ASSERT_EQUAL(content_length, response_buffer_pos);
    response_buffer[response_buffer_pos] = '\0';

    /* Should contain our PATCH data */
    TEST_ASSERT_TRUE(strstr((char *)response_buffer, "\"test\": \"patch_data\"") != NULL);
    TEST_ASSERT_TRUE(strstr((char *)response_buffer, "\"method\": \"PATCH\"") != NULL);

    /* Step 10: Cleanup */
    status = http_impl.cleanup(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    free_basic_config_tls(&config);
}

void test_http_delete_method(void)
{
    osal_http_handle_t handle = NULL;
    osal_http_config_t config;
    osal_err_t status;
    int status_code;
    const char *delete_data = "{\"test\": \"delete_data\", \"method\": \"DELETE\"}";

    /* Reset response buffer */
    response_buffer_pos = 0;

    /* Step 1: Initialize HTTP implementation */
    init_http_impl();

    /* Create config with certificate for HTTPS */
    create_basic_config_tls(&config, TEST_HTTP_COMMON_URL_TLS "/delete", OSAL_HTTP_METHOD_DELETE);

    /* Step 2: Initialize HTTP client */
    status = http_impl.init(&config, &handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    TEST_ASSERT_NOT_NULL(handle);

    /* Set request body */
    status = http_impl.set_request_body(handle, (uint8_t *)delete_data, strlen(delete_data), "application/json");
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Set read callback */
    status = http_impl.set_read_callback(handle, test_read_callback);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Perform the request synchronously */
    status = http_impl.perform(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);

    /* Get status and content length */
    status_code = http_impl.get_status_code(handle);
    TEST_ASSERT_TRUE(status_code >= 200 && status_code < 300);

    int64_t content_length = http_impl.get_content_length(handle);
    TEST_ASSERT_GREATER_THAN(0, content_length);

    /* Verify we received exact content length */
    TEST_ASSERT_EQUAL(content_length, response_buffer_pos);
    response_buffer[response_buffer_pos] = '\0';

    /* Should contain DELETE method info */
    TEST_ASSERT_TRUE(strstr((char *)response_buffer, "\"test\": \"delete_data\"") != NULL);
    TEST_ASSERT_TRUE(strstr((char *)response_buffer, "\"method\": \"DELETE\"") != NULL);

    /* Step 10: Cleanup */
    status = http_impl.cleanup(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    free_basic_config_tls(&config);
}

void test_http_multiple_requests(void)
{
    osal_http_handle_t handle = NULL;
    osal_http_config_t config;
    osal_err_t status;

    /* Step 1: Initialize HTTP implementation */
    init_http_impl();

    /* Create config with certificate for HTTPS */
    create_basic_config_tls(&config, TEST_HTTP_COMMON_URL_TLS "/get", OSAL_HTTP_METHOD_GET);

    /* Step 2: Initialize HTTP client once */
    status = http_impl.init(&config, &handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    TEST_ASSERT_NOT_NULL(handle);

    /* Test multiple requests with the same handle */
    for (int i = 0; i < 3; i++) {
        _http_get_basic_no_close(handle, i, TEST_HTTP_COMMON_URL_TLS "/get");
    }

    for (int i = 0; i < 3; i++) {
        _http_post_basic_no_close(handle, i, TEST_HTTP_COMMON_URL_TLS "/post");
    }

    /* Step 10: Cleanup */
    status = http_impl.cleanup(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, status);
    free_basic_config_tls(&config);
}
