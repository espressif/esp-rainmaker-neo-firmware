/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_json_basic.c
 * @brief Test the basic JSON functionality
 */

#include "unity.h"
#include "test_json_common_prototypes.h"

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "json_parser.h"
#include "json_generator.h"

/* Test data ******************************************************************/

static char *json_test_str = "{\"str_val\": \"JSON Parser\"," \
                             "\"float_val\":2.0,\n" \
                             "\"float_val_neg\":-3.2345,\n" \
                             "\"int_val\":2017,\n" \
                             "\"int_val_neg\" :-24564,\n" \
                             "\"bool_val\":false,\n" \
                             "\"supported_el\": [\"bool\",\"int\","\
                             "\"float\",\"str\"" \
                             ",\"object\",\"array\"],\n" \
                             "\"features\" : { \"objects\":true, "\
                             "\"arrays\":\"yes\"}}";

/* Test functions **************************************************************/

static void __parse(char *json_str)
{
    jparse_ctx_t jctx;
    int ret = json_parse_start(&jctx, json_str, strlen(json_str));
    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, ret, "JSON parser failed");

    char str_val[64];
    float float_val;
    int int_val;
    bool bool_val;
    int num_elem;

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_obj_get_string(&jctx, "str_val", str_val, sizeof(str_val)), "Failed to get string value");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("JSON Parser", str_val, "String value mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_obj_get_float(&jctx, "float_val", &float_val), "Failed to get float value");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(2.0, float_val, "Float value mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_obj_get_float(&jctx, "float_val_neg", &float_val), "Failed to get float value");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(-3.2345, float_val, "Negative float value mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_obj_get_int(&jctx, "int_val", &int_val), "Failed to get int value");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2017, int_val, "Int value mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_obj_get_int(&jctx, "int_val_neg", &int_val), "Failed to get int value");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-24564, int_val, "Negative int value mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_obj_get_bool(&jctx, "bool_val", &bool_val), "Failed to get bool value");
    TEST_ASSERT_EQUAL_MESSAGE(false, bool_val, "Bool value mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_obj_get_array(&jctx, "supported_el", &num_elem), "Failed to get array value");
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, num_elem, "Array size mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_arr_get_string(&jctx, 0, str_val, sizeof(str_val)), "Failed to get string value");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bool", str_val, "Array element mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_arr_get_string(&jctx, 1, str_val, sizeof(str_val)), "Failed to get string value");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("int", str_val, "Array element mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_arr_get_string(&jctx, 2, str_val, sizeof(str_val)), "Failed to get string value");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("float", str_val, "Array element mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_arr_get_string(&jctx, 3, str_val, sizeof(str_val)), "Failed to get string value");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("str", str_val, "Array element mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_arr_get_string(&jctx, 4, str_val, sizeof(str_val)), "Failed to get string value");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("object", str_val, "Array element mismatch");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_arr_get_string(&jctx, 5, str_val, sizeof(str_val)), "Failed to get string value");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("array", str_val, "Array element mismatch");

    json_obj_leave_array(&jctx);

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_obj_get_object(&jctx, "features"), "Failed to get object value for features");
    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_obj_get_bool(&jctx, "objects", &bool_val), "Failed to get bool value for objects");
    TEST_ASSERT_EQUAL_MESSAGE(true, bool_val, "Bool value mismatch for objects");

    TEST_ASSERT_EQUAL_MESSAGE(OS_SUCCESS, json_obj_get_string(&jctx, "arrays", str_val, sizeof(str_val)), "Failed to get string value");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("yes", str_val, "String value mismatch for arrays");

    json_obj_leave_object(&jctx);

    json_parse_end(&jctx);
}

static int __generate(char *json_str, int json_str_size)
{
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, json_str, json_str_size, NULL, NULL);
    json_gen_start_object(&jstr);
    json_gen_obj_set_string(&jstr, "str_val", "JSON Parser");
    json_gen_obj_set_float(&jstr, "float_val", 2.0);
    json_gen_obj_set_float(&jstr, "float_val_neg", -3.2345);
    json_gen_obj_set_int(&jstr, "int_val", 2017);
    json_gen_obj_set_int(&jstr, "int_val_neg", -24564);
    json_gen_obj_set_bool(&jstr, "bool_val", false);
    json_gen_push_array(&jstr, "supported_el");
    json_gen_arr_set_string(&jstr, "bool");
    json_gen_arr_set_string(&jstr, "int");
    json_gen_arr_set_string(&jstr, "float");
    json_gen_arr_set_string(&jstr, "str");
    json_gen_arr_set_string(&jstr, "object");
    json_gen_arr_set_string(&jstr, "array");
    json_gen_pop_array(&jstr);
    json_gen_push_object(&jstr, "features");
    json_gen_obj_set_bool(&jstr, "objects", true);
    json_gen_obj_set_string(&jstr, "arrays", "yes");
    json_gen_pop_object(&jstr);
    json_gen_end_object(&jstr);
    return json_gen_str_end(&jstr);
}

void test_json_parser_basic(void)
{
    __parse(json_test_str);
}

void test_json_generator_basic(void)
{
    int size = __generate(NULL, 0);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, size, "Generated JSON string is empty");
    char json_str[size];
    int gen_size = __generate(json_str, size);
    TEST_ASSERT_EQUAL_MESSAGE(size, gen_size, "Generated JSON string size mismatch");
    __parse(json_str);
}
