/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_val.c
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <string.h>
#include <stdlib.h>

#include "esp_rmaker_val.h"

void test_val_constructors(void)
{
    esp_rmaker_param_val_t vb = esp_rmaker_bool(true);
    TEST_ASSERT_EQUAL(RMAKER_VAL_TYPE_BOOLEAN, vb.type);
    TEST_ASSERT_TRUE(vb.val.b);

    esp_rmaker_param_val_t vi = esp_rmaker_int(42);
    TEST_ASSERT_EQUAL(RMAKER_VAL_TYPE_INTEGER, vi.type);
    TEST_ASSERT_EQUAL(42, vi.val.i);

    esp_rmaker_param_val_t vf = esp_rmaker_float(3.14f);
    TEST_ASSERT_EQUAL(RMAKER_VAL_TYPE_FLOAT, vf.type);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14f, vf.val.f);

    esp_rmaker_param_val_t vs = esp_rmaker_str("abc");
    TEST_ASSERT_EQUAL(RMAKER_VAL_TYPE_STRING, vs.type);
    TEST_ASSERT_EQUAL_STRING("abc", vs.val.s);
    free(vs.val.s);

    esp_rmaker_param_val_t vo = esp_rmaker_obj("{\"k\":1}");
    TEST_ASSERT_EQUAL(RMAKER_VAL_TYPE_OBJECT, vo.type);
    TEST_ASSERT_EQUAL_STRING("{\"k\":1}", vo.val.s);
    free(vo.val.s);

    esp_rmaker_param_val_t va = esp_rmaker_array("[1,2,3]");
    TEST_ASSERT_EQUAL(RMAKER_VAL_TYPE_ARRAY, va.type);
    TEST_ASSERT_EQUAL_STRING("[1,2,3]", va.val.s);
    free(va.val.s);
}

void test_val_int64(void)
{
    const int64_t big = 5000000000LL; /* above INT32_MAX */
    esp_rmaker_param_val_t a = esp_rmaker_int64(big);
    esp_rmaker_param_val_t b = esp_rmaker_int64(big + 1);
    TEST_ASSERT_EQUAL(RMAKER_VAL_TYPE_INT64, a.type);
    TEST_ASSERT_TRUE(a.val.i64 == big);

    /* Compare uses all 64 bits: the low 32 bits of big and big + 2^32 are equal */
    esp_rmaker_param_val_t c = esp_rmaker_int64(big + 0x100000000LL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_val_compare(&a, &c, RMAKER_VAL_COMPARE_EQ));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_val_compare(&a, &a, RMAKER_VAL_COMPARE_EQ));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_val_compare(&a, &b, RMAKER_VAL_COMPARE_NEQ));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_val_compare(&a, &b, RMAKER_VAL_COMPARE_LT));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_val_compare(&a, &b, RMAKER_VAL_COMPARE_LTE));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_val_compare(&b, &a, RMAKER_VAL_COMPARE_GT));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_val_compare(&b, &a, RMAKER_VAL_COMPARE_GTE));
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_val_compare(&b, &a, RMAKER_VAL_COMPARE_LT));

    /* int and int64 are different types */
    esp_rmaker_param_val_t i32 = esp_rmaker_int(1);
    esp_rmaker_param_val_t i64 = esp_rmaker_int64(1);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_val_compare(&i32, &i64, RMAKER_VAL_COMPARE_EQ));

    esp_rmaker_param_val_t copy = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_val_copy(&a, &copy));
    TEST_ASSERT_EQUAL(RMAKER_VAL_TYPE_INT64, copy.type);
    TEST_ASSERT_TRUE(copy.val.i64 == big);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_val_free(&copy));
}
