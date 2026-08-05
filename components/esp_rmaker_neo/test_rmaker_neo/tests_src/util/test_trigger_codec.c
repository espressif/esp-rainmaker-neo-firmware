/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_trigger_codec.c
 * @brief Unit tests for the binary trigger-details codec (encode + iterate).
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "util/esp_rmaker_trigger_codec.h"

/* Helpers *******************************************************************/

static void encode_ok(const char *json, uint8_t **out, size_t *out_len)
{
    *out = NULL;
    *out_len = 0;
    esp_rmaker_error_t err = esp_rmaker_trigger_details_encode(json, strlen(json), out, out_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "encode should succeed");
    TEST_ASSERT_NOT_NULL(*out);
    TEST_ASSERT_GREATER_THAN(0, *out_len);
}

/* Encode `json`, then fetch the single trigger it contains into `entry`.
 * Keeps the blob alive via *blob (caller frees) since entry points into it. */
static void encode_one(const char *json, uint8_t **blob, esp_rmaker_trigger_entry_t *entry)
{
    size_t blen = 0;
    encode_ok(json, blob, &blen);

    esp_rmaker_trigger_iter_t it;
    size_t count = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_trigger_details_iter_begin(*blob, blen, &it, &count));
    TEST_ASSERT_EQUAL_MESSAGE(1, count, "expected exactly one trigger");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_trigger_details_iter_next(&it, entry));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_NOT_FOUND, esp_rmaker_trigger_details_iter_next(&it, entry),
                              "iterator must end after the single trigger");
}

static void assert_str_eq(const char *expect, const char *got, size_t got_len, const char *msg)
{
    TEST_ASSERT_EQUAL_MESSAGE(strlen(expect), got_len, msg);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expect, got, got_len, msg);
}

/* Value-type fidelity *******************************************************/

void test_trigger_codec_value_bool(void)
{
    uint8_t *blob = NULL;
    esp_rmaker_trigger_entry_t e;
    encode_one("[{\"id\":\"a1\",\"enabled\":true,\"path\":\"Light.Power\",\"operator\":\"eq\",\"value\":true}]",
               &blob, &e);
    TEST_ASSERT_EQUAL_UINT8(RMAKER_TRIGGER_VT_BOOL, e.value_type);
    TEST_ASSERT_TRUE(e.value_bool);
    TEST_ASSERT_EQUAL_UINT8(RMAKER_TRIGGER_OP_EQ, e.op_code);
    TEST_ASSERT_TRUE(e.enabled);
    free(blob);
}

void test_trigger_codec_value_int(void)
{
    uint8_t *blob = NULL;
    esp_rmaker_trigger_entry_t e;
    encode_one("[{\"id\":\"a1\",\"enabled\":true,\"path\":\"Light.Brightness\",\"operator\":\"gt\",\"value\":50}]",
               &blob, &e);
    TEST_ASSERT_EQUAL_UINT8(RMAKER_TRIGGER_VT_INT, e.value_type);
    TEST_ASSERT_EQUAL_INT32(50, e.value_int);
    TEST_ASSERT_EQUAL_UINT8(RMAKER_TRIGGER_OP_GT, e.op_code);
    free(blob);
}

void test_trigger_codec_value_float(void)
{
    uint8_t *blob = NULL;
    esp_rmaker_trigger_entry_t e;
    encode_one("[{\"id\":\"a1\",\"enabled\":true,\"path\":\"Temp.Value\",\"operator\":\"le\",\"value\":23.5}]",
               &blob, &e);
    TEST_ASSERT_EQUAL_UINT8(RMAKER_TRIGGER_VT_FLOAT, e.value_type);
    TEST_ASSERT_EQUAL_FLOAT(23.5f, e.value_float);
    TEST_ASSERT_EQUAL_UINT8(RMAKER_TRIGGER_OP_LE, e.op_code);
    free(blob);
}

void test_trigger_codec_value_string(void)
{
    uint8_t *blob = NULL;
    esp_rmaker_trigger_entry_t e;
    encode_one("[{\"id\":\"a1\",\"enabled\":true,\"path\":\"Mode.Current\",\"operator\":\"ne\",\"value\":\"away\"}]",
               &blob, &e);
    TEST_ASSERT_EQUAL_UINT8(RMAKER_TRIGGER_VT_STRING, e.value_type);
    assert_str_eq("away", e.value_str, e.value_str_len, "string value");
    free(blob);
}

void test_trigger_codec_value_object(void)
{
    uint8_t *blob = NULL;
    esp_rmaker_trigger_entry_t e;
    encode_one("[{\"id\":\"a1\",\"enabled\":true,\"path\":\"X.Y\",\"operator\":\"eq\",\"value\":{\"k\":1}}]",
               &blob, &e);
    TEST_ASSERT_EQUAL_UINT8(RMAKER_TRIGGER_VT_OBJECT, e.value_type);
    /* Raw object JSON preserved (whitespace-free as the parser emits it). */
    TEST_ASSERT_TRUE(e.value_str_len > 0);
    TEST_ASSERT_NOT_NULL(memchr(e.value_str, 'k', e.value_str_len));
    free(blob);
}

void test_trigger_codec_value_array(void)
{
    uint8_t *blob = NULL;
    esp_rmaker_trigger_entry_t e;
    encode_one("[{\"id\":\"a1\",\"enabled\":true,\"path\":\"X.Y\",\"operator\":\"eq\",\"value\":[1,2,3]}]",
               &blob, &e);
    TEST_ASSERT_EQUAL_UINT8(RMAKER_TRIGGER_VT_ARRAY, e.value_type);
    TEST_ASSERT_TRUE(e.value_str_len > 0);
    free(blob);
}

/* Field fidelity ************************************************************/

void test_trigger_codec_preserves_fields(void)
{
    uint8_t *blob = NULL;
    esp_rmaker_trigger_entry_t e;
    encode_one("[{\"id\":\"auto-42\",\"enabled\":true,\"path\":\"Device.Param\",\"operator\":\"gt\",\"value\":7}]",
               &blob, &e);
    assert_str_eq("auto-42", e.id, e.id_len, "id");
    assert_str_eq("Device.Param", e.path, e.path_len, "path");
    TEST_ASSERT_EQUAL_UINT8(RMAKER_TRIGGER_OP_GT, e.op_code);
    TEST_ASSERT_TRUE(e.enabled);
    TEST_ASSERT_EQUAL_UINT8(RMAKER_TRIGGER_VT_INT, e.value_type);
    TEST_ASSERT_EQUAL_INT32(7, e.value_int);
    free(blob);
}

void test_trigger_codec_enabled_defaults_true_when_omitted(void)
{
    uint8_t *blob = NULL;
    esp_rmaker_trigger_entry_t e;
    encode_one("[{\"id\":\"a\",\"path\":\"p\",\"operator\":\"eq\",\"value\":1}]", &blob, &e);
    TEST_ASSERT_TRUE_MESSAGE(e.enabled, "enabled defaults to true when omitted");
    free(blob);
}

void test_trigger_codec_multiple_and_operators(void)
{
    uint8_t *blob = NULL;
    size_t blen = 0;
    encode_ok(
        "[{\"id\":\"x\",\"enabled\":true,\"path\":\"p1\",\"operator\":\"eq\",\"value\":1},"
        "{\"id\":\"y\",\"enabled\":false,\"path\":\"p2\",\"operator\":\"ne\",\"value\":2},"
        "{\"id\":\"z\",\"enabled\":true,\"path\":\"p3\",\"operator\":\"ge\",\"value\":3.5},"
        "{\"id\":\"w\",\"enabled\":true,\"path\":\"p4\",\"operator\":\"lt\",\"value\":\"s\"}]",
        &blob, &blen);

    /* The disabled trigger 'y' is dropped at encode, so only x, z, w remain. */
    esp_rmaker_trigger_iter_t it;
    size_t count = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_trigger_details_iter_begin(blob, blen, &it, &count));
    TEST_ASSERT_EQUAL_MESSAGE(3, count, "disabled trigger must be dropped");

    esp_rmaker_trigger_entry_t e;
    uint8_t expect_op[3] = { RMAKER_TRIGGER_OP_EQ, RMAKER_TRIGGER_OP_GE, RMAKER_TRIGGER_OP_LT };
    uint8_t expect_vt[3] = { RMAKER_TRIGGER_VT_INT, RMAKER_TRIGGER_VT_FLOAT, RMAKER_TRIGGER_VT_STRING };
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_trigger_details_iter_next(&it, &e));
        TEST_ASSERT_TRUE(e.enabled);
        TEST_ASSERT_EQUAL_UINT8(expect_op[i], e.op_code);
        TEST_ASSERT_EQUAL_UINT8(expect_vt[i], e.value_type);
    }
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_trigger_details_iter_next(&it, &e));
    free(blob);
}

void test_trigger_codec_empty_array(void)
{
    uint8_t *blob = NULL;
    size_t blen = 0;
    encode_ok("[]", &blob, &blen);
    TEST_ASSERT_EQUAL_MESSAGE(2, blen, "empty array blob is [version][count=0]");

    esp_rmaker_trigger_iter_t it;
    size_t count = 99;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_trigger_details_iter_begin(blob, blen, &it, &count));
    TEST_ASSERT_EQUAL(0, count);
    esp_rmaker_trigger_entry_t e;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_trigger_details_iter_next(&it, &e));
    free(blob);
}

void test_trigger_codec_is_smaller_than_json(void)
{
    const char *json =
        "[{\"id\":\"automation-001\",\"enabled\":true,\"path\":\"Light.Brightness\",\"operator\":\"gt\",\"value\":50}]";
    uint8_t *blob = NULL;
    size_t blen = 0;
    encode_ok(json, &blob, &blen);
    TEST_ASSERT_LESS_THAN_MESSAGE(strlen(json), blen, "binary form must be smaller than the JSON");
    free(blob);
}

/* Failure paths *************************************************************/

void test_trigger_codec_encode_rejects_unknown_operator(void)
{
    const char *json = "[{\"id\":\"a\",\"path\":\"p\",\"operator\":\"between\",\"value\":1}]";
    uint8_t *b = NULL;
    size_t blen = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_trigger_details_encode(json, strlen(json), &b, &blen));
    TEST_ASSERT_NULL(b);
}

void test_trigger_codec_encode_rejects_missing_path(void)
{
    const char *json = "[{\"id\":\"a\",\"operator\":\"eq\",\"value\":1}]";
    uint8_t *b = NULL;
    size_t blen = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_trigger_details_encode(json, strlen(json), &b, &blen));
    TEST_ASSERT_NULL(b);
}

void test_trigger_codec_encode_rejects_null_value(void)
{
    const char *json = "[{\"id\":\"a\",\"path\":\"p\",\"operator\":\"eq\",\"value\":null}]";
    uint8_t *b = NULL;
    size_t blen = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_trigger_details_encode(json, strlen(json), &b, &blen));
    TEST_ASSERT_NULL(b);
}

void test_trigger_codec_iter_rejects_wrong_version(void)
{
    /* Raw JSON bytes (older release): first byte '[' != codec version. */
    const char *old_json = "[{\"id\":\"a\"}]";
    esp_rmaker_trigger_iter_t it;
    size_t count = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL,
                      esp_rmaker_trigger_details_iter_begin((const uint8_t *)old_json, strlen(old_json), &it, &count));
}

void test_trigger_codec_iter_rejects_truncated(void)
{
    uint8_t *blob = NULL;
    size_t blen = 0;
    encode_ok("[{\"id\":\"abcdef\",\"enabled\":true,\"path\":\"some.path\",\"operator\":\"gt\",\"value\":99}]",
              &blob, &blen);

    /* Header still valid, but the entry runs past the truncated end. */
    esp_rmaker_trigger_iter_t it;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_trigger_details_iter_begin(blob, blen - 1, &it, NULL));
    esp_rmaker_trigger_entry_t e;
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_trigger_details_iter_next(&it, &e));
    free(blob);
}

void test_trigger_codec_invalid_args(void)
{
    uint8_t *b = NULL;
    size_t blen = 0;
    esp_rmaker_trigger_iter_t it;
    esp_rmaker_trigger_entry_t e;
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_trigger_details_encode(NULL, 0, &b, &blen));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_trigger_details_encode("[]", 2, NULL, &blen));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_trigger_details_iter_begin(NULL, 0, &it, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_trigger_details_iter_next(NULL, &e));
}
