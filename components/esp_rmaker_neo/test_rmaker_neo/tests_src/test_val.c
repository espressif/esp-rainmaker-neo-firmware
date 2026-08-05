/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_val.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

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
