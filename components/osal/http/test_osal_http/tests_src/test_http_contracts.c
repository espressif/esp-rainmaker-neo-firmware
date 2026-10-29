/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_http_contracts.c
 * @brief Test the NULL-argument and method/body contracts of osal_http
 */

#include "unity.h"
#include <stdint.h>
#include <string.h>

#include "osal_http_impl.h"
#include "test_osal_http_config.h"

/** Echoes the request method, headers and body */
#define ANYTHING_URL TEST_OSAL_HTTP_URL_TCP "/anything"

static osal_http_impl_t http_impl;
static uint8_t response_buffer[TEST_OSAL_HTTP_BUFFER_SIZE];
static size_t response_buffer_pos;

static int collect_response(uint8_t *data, size_t data_len, int64_t content_length)
{
    if (response_buffer_pos + data_len >= sizeof(response_buffer)) {
        return -1;
    }
    memcpy(response_buffer + response_buffer_pos, data, data_len);
    response_buffer_pos += data_len;
    return data_len;
}

/* Open a handle on the echo endpoint with the response callback installed */
static osal_http_handle_t open_echo_handle(osal_http_method_t method)
{
    osal_http_config_t config;
    osal_http_handle_t handle = NULL;

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_http_impl_setup(&http_impl));
    TEST_ASSERT_TRUE(http_impl.setup_done);

    memset(&config, 0, sizeof(config));
    config.url = ANYTHING_URL;
    config.method = method;
    config.timeout_ms = TEST_OSAL_HTTP_TIMEOUT_MS;
    config.buffer_size_tx = TEST_OSAL_HTTP_BUFFER_SIZE;
    config.buffer_size_rx = TEST_OSAL_HTTP_BUFFER_SIZE;
    config.transport_type = OSAL_HTTP_TRANSPORT_OVER_TCP;

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.init(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.set_read_callback(handle, collect_response));
    return handle;
}

/* Perform the request and return the echo as a NULL-terminated string */
static const char *perform_and_echo(osal_http_handle_t handle)
{
    response_buffer_pos = 0;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.perform(handle));

    int status_code = http_impl.get_status_code(handle);
    TEST_ASSERT_TRUE(status_code >= 200 && status_code < 300);

    response_buffer[response_buffer_pos] = '\0';
    return (const char *)response_buffer;
}

void test_http_contract_null_value_removes_header(void)
{
    osal_http_handle_t handle = open_echo_handle(OSAL_HTTP_METHOD_GET);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.set_header(handle, "X-Osal-Contract", "present"));
    TEST_ASSERT_NOT_NULL(strstr(perform_and_echo(handle), "\"X-Osal-Contract\": \"present\""));

    /* The same handle, reused: a NULL value must drop the header from the next request */
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.set_header(handle, "X-Osal-Contract", NULL));
    TEST_ASSERT_NULL(strstr(perform_and_echo(handle), "X-Osal-Contract"));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.cleanup(handle));
}

void test_http_contract_removing_absent_header_succeeds(void)
{
    osal_http_handle_t handle = open_echo_handle(OSAL_HTTP_METHOD_GET);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.set_header(handle, "X-Never-Set", NULL));
    TEST_ASSERT_NULL(strstr(perform_and_echo(handle), "X-Never-Set"));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.cleanup(handle));
}

void test_http_contract_null_content_type_keeps_header(void)
{
    const char *first = "{\"pass\": 1}";
    const char *second = "{\"pass\": 2}";
    osal_http_handle_t handle = open_echo_handle(OSAL_HTTP_METHOD_POST);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK,
                      http_impl.set_request_body(handle, (const uint8_t *)first, strlen(first), "application/json"));
    TEST_ASSERT_NOT_NULL(strstr(perform_and_echo(handle), "\"Content-Type\": \"application/json\""));

    /* A NULL content type must keep the header, not delete it */
    TEST_ASSERT_EQUAL(OSAL_ERR_OK,
                      http_impl.set_request_body(handle, (const uint8_t *)second, strlen(second), NULL));
    const char *echo = perform_and_echo(handle);
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"Content-Type\": \"application/json\""));
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"pass\": 2"));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.cleanup(handle));
}

void test_http_contract_body_before_method(void)
{
    const char *body = "{\"order\": \"body-first\"}";
    osal_http_handle_t handle = open_echo_handle(OSAL_HTTP_METHOD_GET);

    /* The body lands first; the method must still win, and the body must still go out */
    TEST_ASSERT_EQUAL(OSAL_ERR_OK,
                      http_impl.set_request_body(handle, (const uint8_t *)body, strlen(body), "application/json"));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.set_method(handle, OSAL_HTTP_METHOD_PUT));

    const char *echo = perform_and_echo(handle);
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"method\": \"PUT\""));
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"order\": \"body-first\""));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.cleanup(handle));
}

void test_http_contract_body_keeps_method(void)
{
    const char *body = "{\"verb\": \"delete\"}";
    osal_http_handle_t handle = open_echo_handle(OSAL_HTTP_METHOD_DELETE);

    /* A body must not turn a DELETE into a POST */
    TEST_ASSERT_EQUAL(OSAL_ERR_OK,
                      http_impl.set_request_body(handle, (const uint8_t *)body, strlen(body), "application/json"));

    const char *echo = perform_and_echo(handle);
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"method\": \"DELETE\""));
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"verb\": \"delete\""));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.cleanup(handle));
}

void test_http_contract_empty_body_sends_length_zero(void)
{
    osal_http_handle_t handle = open_echo_handle(OSAL_HTTP_METHOD_POST);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.set_request_body(handle, NULL, 0, NULL));

    const char *echo = perform_and_echo(handle);
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"method\": \"POST\""));
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"Content-Length\": \"0\""));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.cleanup(handle));
}

void test_http_contract_method_not_stale(void)
{
    const char *body = "{\"first\": \"post\"}";
    osal_http_handle_t handle = open_echo_handle(OSAL_HTTP_METHOD_POST);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK,
                      http_impl.set_request_body(handle, (const uint8_t *)body, strlen(body), "application/json"));
    TEST_ASSERT_NOT_NULL(strstr(perform_and_echo(handle), "\"method\": \"POST\""));

    /* The same handle, reused: clear the body, then the next method must not inherit the POST */
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.set_request_body(handle, NULL, 0, NULL));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.set_method(handle, OSAL_HTTP_METHOD_GET));

    const char *echo = perform_and_echo(handle);
    TEST_ASSERT_NOT_NULL(strstr(echo, "\"method\": \"GET\""));
    TEST_ASSERT_NULL(strstr(echo, "\"first\": \"post\""));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, http_impl.cleanup(handle));
}
