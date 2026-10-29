/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_aws_sign_port.c
 * @brief Tests for the osal-backed rmaker_aws_sign port: its HTTP and JSON ops.
 */

#include "unity.h"
#include "test_rmaker_neo_common_prototypes.h"
#include "sdkconfig.h"

/* The port exists only with CONFIG_ESP_RMAKER_NEO_AWS_SIGN_PORT. */
#if CONFIG_ESP_RMAKER_NEO_AWS_SIGN_PORT

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_rmaker_aws_sign_port_osal.h"

#define PORT_TIMEOUT_MS 10000

/* open() does not connect, so a test that sends nothing can use any URL */
#define OFFLINE_URL "http://127.0.0.1/anything"

static const esp_rmaker_aws_sign_port_config_t *__port(void)
{
    const esp_rmaker_aws_sign_port_config_t *port = esp_rmaker_aws_sign_port_osal_get();
    TEST_ASSERT_NOT_NULL(port);
    return port;
}

static esp_rmaker_aws_sign_http_handle_t __open(const char *url, esp_rmaker_aws_sign_http_method_t method)
{
    const esp_rmaker_aws_sign_http_config_t cfg = {
        .url = url,
        .method = method,
        .timeout_ms = PORT_TIMEOUT_MS,
        .keep_alive = true,
    };
    esp_rmaker_aws_sign_http_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, __port()->http.open(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);
    return h;
}

void test_aws_sign_port_table(void)
{
    const esp_rmaker_aws_sign_port_config_t *port = __port();

    TEST_ASSERT_NOT_NULL(port->http.open);
    TEST_ASSERT_NOT_NULL(port->http.perform);
    TEST_ASSERT_NOT_NULL(port->json.read_strs);
    TEST_ASSERT_NOT_NULL(port->crypto.init);
    TEST_ASSERT_NOT_NULL(port->crypto.sha256_init);
    /* A second call returns the same table */
    TEST_ASSERT_EQUAL_PTR(port, esp_rmaker_aws_sign_port_osal_get());
}

void test_aws_sign_port_long_content_type(void)
{
    char long_type[80];
    memset(long_type, 'a', sizeof(long_type) - 1);
    long_type[sizeof(long_type) - 1] = '\0';
    esp_rmaker_aws_sign_http_handle_t h = __open(OFFLINE_URL, ESP_RMAKER_AWS_SIGN_HTTP_POST);

    /* Truncated, the value on the wire would differ from the signed one */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, __port()->http.set_header(h, "Content-Type", long_type));
    TEST_ASSERT_EQUAL(ESP_OK, __port()->http.set_header(h, "Content-Type", "application/json"));

    __port()->http.close(h);
}

/* JSON ***************************************************************************/

#define CREDS_JSON \
    "{\"credentials\":{\"accessKeyId\":\"AKIA\",\"sessionToken\":\"a\\/b\\u00e9\\n\"},\"ttl\":\"3600\"}"

void test_aws_sign_port_json_reads_nested_and_root(void)
{
    char key_id[16];
    char token[16];
    char ttl[8];
    esp_rmaker_aws_sign_json_field_t nested[] = {
        { .key = "accessKeyId", .buf = key_id, .buf_size = sizeof(key_id) },
        { .key = "sessionToken", .buf = token, .buf_size = sizeof(token) },
        { .key = "absent", .buf = ttl, .buf_size = sizeof(ttl) },
    };
    esp_rmaker_aws_sign_json_field_t root[] = {
        { .key = "ttl", .buf = ttl, .buf_size = sizeof(ttl) },
    };

    TEST_ASSERT_EQUAL(ESP_OK, __port()->json.read_strs(CREDS_JSON, strlen(CREDS_JSON), "credentials", nested, 3));
    TEST_ASSERT_TRUE(nested[0].found);
    TEST_ASSERT_EQUAL_STRING("AKIA", key_id);
    TEST_ASSERT_TRUE(nested[1].found);
    /* \/ is "/", \u00e9 is UTF-8 C3 A9, \n is a newline */
    TEST_ASSERT_EQUAL_STRING("a/b\xc3\xa9\n", token);
    TEST_ASSERT_EQUAL(6, nested[1].len);
    TEST_ASSERT_FALSE(nested[2].found);

    TEST_ASSERT_EQUAL(ESP_OK, __port()->json.read_strs(CREDS_JSON, strlen(CREDS_JSON), NULL, root, 1));
    TEST_ASSERT_TRUE(root[0].found);
    TEST_ASSERT_EQUAL_STRING("3600", ttl);
}

void test_aws_sign_port_json_refuses_bad_documents(void)
{
    static const char duplicate[] = "{\"k\":\"one\",\"k\":\"two\"}";
    static const char not_string[] = "{\"k\":42}";
    static const char long_value[] = "{\"k\":\"0123456789\"}";
    char buf[8];
    esp_rmaker_aws_sign_json_field_t field = { .key = "k", .buf = buf, .buf_size = sizeof(buf) };

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE,
                      __port()->json.read_strs(duplicate, strlen(duplicate), NULL, &field, 1));
    /* Fail closed: nothing of a refused document is reported */
    TEST_ASSERT_FALSE(field.found);
    TEST_ASSERT_EQUAL_STRING("", buf);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE,
                      __port()->json.read_strs(not_string, strlen(not_string), NULL, &field, 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, __port()->json.read_strs(long_value, strlen(long_value), NULL, &field, 1));
    TEST_ASSERT_FALSE(field.found);
}

/* HTTP against the sidecar *******************************************************/

/* The sidecar runs on the POSIX host only. */
#ifndef ESP_PLATFORM

#define SIDECAR_URL CONFIG_TEST_OSAL_HTTP_URL_TCP
/* 100 and 10 bytes, with a declared length */
#define LENGTH_100_URL SIDECAR_URL "/base64/" \
    "YWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYQ=="
#define LENGTH_10_URL SIDECAR_URL "/base64/YmJiYmJiYmJiYg=="
#define NO_CONTENT_URL SIDECAR_URL "/status/204"
#define SIDECAR_ANYTHING_URL SIDECAR_URL "/anything"

/* What the receiver saw in one perform */
typedef struct {
    int hint_calls;
    size_t hint_len;
    int hint_return;
    bool sink_before_hint;
    char body[1024];
    size_t body_len;
} __record_t;

static int __hint(void *ctx, size_t content_length)
{
    __record_t *r = ctx;
    r->hint_calls++;
    r->hint_len = content_length;
    return r->hint_return;
}

static int __sink(void *ctx, const uint8_t *data, size_t len)
{
    __record_t *r = ctx;
    if (r->hint_calls == 0) {
        r->sink_before_hint = true;
    }
    if (r->body_len + len >= sizeof(r->body)) {
        return 0;
    }
    memcpy(r->body + r->body_len, data, len);
    r->body_len += len;
    r->body[r->body_len] = '\0';
    return (int) len;
}

static esp_err_t __perform(esp_rmaker_aws_sign_http_handle_t h, const char *body, __record_t *r)
{
    const esp_rmaker_aws_sign_http_receiver_t rx = { .hint = __hint, .sink = __sink, .ctx = r };
    return __port()->http.perform(h, (const uint8_t *) body, body ? strlen(body) : 0, &rx);
}

void test_aws_sign_port_hint_before_body(void)
{
    __record_t r = { 0 };
    esp_rmaker_aws_sign_http_handle_t h = __open(LENGTH_100_URL, ESP_RMAKER_AWS_SIGN_HTTP_GET);

    TEST_ASSERT_EQUAL(ESP_OK, __perform(h, NULL, &r));
    TEST_ASSERT_EQUAL(200, __port()->http.status_code(h));
    TEST_ASSERT_EQUAL(1, r.hint_calls);
    TEST_ASSERT_FALSE(r.sink_before_hint);
    TEST_ASSERT_EQUAL(100, r.hint_len);
    TEST_ASSERT_EQUAL(100, r.body_len);

    /* The same handle, reused: the hint carries the new length */
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_EQUAL(ESP_OK, __port()->http.set_url(h, LENGTH_10_URL));
    TEST_ASSERT_EQUAL(ESP_OK, __perform(h, NULL, &r));
    TEST_ASSERT_EQUAL(1, r.hint_calls);
    TEST_ASSERT_EQUAL(10, r.hint_len);
    TEST_ASSERT_EQUAL(10, r.body_len);

    __port()->http.close(h);
}

void test_aws_sign_port_hint_on_empty_body(void)
{
    __record_t r = { 0 };
    esp_rmaker_aws_sign_http_handle_t h = __open(NO_CONTENT_URL, ESP_RMAKER_AWS_SIGN_HTTP_GET);

    TEST_ASSERT_EQUAL(ESP_OK, __perform(h, NULL, &r));
    TEST_ASSERT_EQUAL(204, __port()->http.status_code(h));
    /* The component needs the hint to know that the server answered */
    TEST_ASSERT_EQUAL(1, r.hint_calls);
    TEST_ASSERT_EQUAL(0, r.hint_len);
    TEST_ASSERT_EQUAL(0, r.body_len);

    __port()->http.close(h);
}

void test_aws_sign_port_hint_aborts(void)
{
    __record_t r = { .hint_return = -1 };
    esp_rmaker_aws_sign_http_handle_t h = __open(LENGTH_100_URL, ESP_RMAKER_AWS_SIGN_HTTP_GET);

    TEST_ASSERT_NOT_EQUAL(ESP_OK, __perform(h, NULL, &r));
    TEST_ASSERT_EQUAL(1, r.hint_calls);
    TEST_ASSERT_EQUAL(0, r.body_len);

    __port()->http.close(h);
}

void test_aws_sign_port_post_body_and_content_type(void)
{
    __record_t r = { 0 };
    esp_rmaker_aws_sign_http_handle_t h = __open(SIDECAR_ANYTHING_URL, ESP_RMAKER_AWS_SIGN_HTTP_POST);

    TEST_ASSERT_EQUAL(ESP_OK, __port()->http.set_header(h, "Content-Type", "application/json"));
    TEST_ASSERT_EQUAL(ESP_OK, __perform(h, "{\"port\": \"osal\"}", &r));
    TEST_ASSERT_EQUAL(200, __port()->http.status_code(h));
    TEST_ASSERT_NOT_NULL(strstr(r.body, "\"method\": \"POST\""));
    TEST_ASSERT_NOT_NULL(strstr(r.body, "\"Content-Type\": \"application/json\""));
    TEST_ASSERT_NOT_NULL(strstr(r.body, "\"port\": \"osal\""));

    __port()->http.close(h);
}

#endif /* ESP_PLATFORM */

#endif /* CONFIG_ESP_RMAKER_NEO_AWS_SIGN_PORT */
