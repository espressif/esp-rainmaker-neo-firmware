/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include "timeseries.h"
#include "esp_rmaker_val.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_queue.h"
#include "json_generator.h"
#include "json_parser.h"

#include "node/node_timeseries.c"

static const char *TEST_TAG = "test_timeseries";

/* Test data structures and constants ******************************************/

#define TEST_MAX_ITEMS_IN_PAYLOAD 20
// ms-scale timestamp: 2024-10-27 00:00:00 UTC in milliseconds. Exceeds INT32_MAX
// so the payload path exercises int64 (regression guard against int truncation).
#define TEST_TIMESTAMP_BASE (1729987200LL * 1000)

typedef struct {
    const char *path;
    esp_rmaker_param_val_t val;
    bool is_cumulative;
    time_t timestamp;
} test_timeseries_data_t;

/* Helper functions ************************************************************/

static __timeseries_data_t __create_test_data(const char *path, esp_rmaker_param_val_t val, bool is_cumulative, time_t timestamp)
{
    esp_rmaker_param_val_t val_copy;
    esp_rmaker_error_t err = esp_rmaker_val_copy(&val, &val_copy);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "Should copy value successfully");
    __timeseries_data_t data = {
        .is_cumulative = is_cumulative,
        .param = {
            .path = strdup(path),
            .val = val_copy
        },
        .timestamp = {
            .iana_tz = strdup("Etc/UTC"),
            .utc_ms = timestamp
        }
    };
    return data;
}

static void __validate_json_payload(const char *payload, test_timeseries_data_t *expected_data)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(payload, "Payload should not be NULL");

    // Parse JSON using the proper JSON parser
    jparse_ctx_t jctx;

    int err = json_parse_start(&jctx, payload, strlen(payload));
    TEST_ASSERT_EQUAL_MESSAGE(0, err, "Should parse JSON successfully");

    // Now we expect a single object (not an array under "data")
    // Each payload contains exactly 1 item
    test_timeseries_data_t *expected = expected_data;
    TEST_ASSERT_NOT_NULL_MESSAGE(expected, "Expected data should not be NULL");

    // Validate parameter path (reported under key "k")
    char param_path[64];
    err = json_obj_get_string(&jctx, "k", param_path, sizeof(param_path));
    TEST_ASSERT_EQUAL_MESSAGE(0, err, "Should get parameter path");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected->path, param_path, "Should have correct parameter path");

    // Validate data type
    char data_type[16];
    err = json_obj_get_string(&jctx, "dt", data_type, sizeof(data_type));
    TEST_ASSERT_EQUAL_MESSAGE(0, err, "Should get data type");
    switch (expected->val.type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        TEST_ASSERT_EQUAL_STRING_MESSAGE("bool", data_type, "Should have correct data type");
        break;
    case RMAKER_VAL_TYPE_INTEGER:
        TEST_ASSERT_EQUAL_STRING_MESSAGE("int", data_type, "Should have correct data type");
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        TEST_ASSERT_EQUAL_STRING_MESSAGE("float", data_type, "Should have correct data type");
        break;
    case RMAKER_VAL_TYPE_STRING:
        TEST_ASSERT_EQUAL_STRING_MESSAGE("string", data_type, "Should have correct data type");
        break;
    default:
        TEST_FAIL_MESSAGE("Should have correct data type");
        break;
    }

    // Validate value
    bool value_bool;
    int value_int;
    float value_float;
    char value_string[128];
    switch (expected->val.type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        err = json_obj_get_bool(&jctx, "v", &value_bool);
        TEST_ASSERT_EQUAL_MESSAGE(0, err, "Should get value");
        TEST_ASSERT_EQUAL_MESSAGE(expected->val.val.b, value_bool, "Should have correct value");
        break;
    case RMAKER_VAL_TYPE_INTEGER:
        err = json_obj_get_int(&jctx, "v", &value_int);
        TEST_ASSERT_EQUAL_MESSAGE(0, err, "Should get value");
        TEST_ASSERT_EQUAL_MESSAGE(expected->val.val.i, value_int, "Should have correct value");
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        err = json_obj_get_float(&jctx, "v", &value_float);
        TEST_ASSERT_EQUAL_MESSAGE(0, err, "Should get value");
        TEST_ASSERT_EQUAL_MESSAGE(expected->val.val.f, value_float, "Should have correct value");
        break;
    case RMAKER_VAL_TYPE_STRING:
        err = json_obj_get_string(&jctx, "v", value_string, sizeof(value_string));
        TEST_ASSERT_EQUAL_MESSAGE(0, err, "Should get value");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected->val.val.s, value_string, "Should have correct value");
        break;
    default:
        TEST_FAIL_MESSAGE("Should have correct value");
        break;
    }

    // Get cumulative flag
    bool cumulative;
    err = json_obj_get_bool(&jctx, "cumulative", &cumulative);
    TEST_ASSERT_EQUAL_MESSAGE(0, err, "Should get cumulative flag");
    TEST_ASSERT_EQUAL_MESSAGE(expected->is_cumulative, cumulative, "Should have correct cumulative flag");

    // Get timestamp
    int64_t timestamp;
    err = json_obj_get_int64(&jctx, "t", &timestamp);
    TEST_ASSERT_EQUAL_MESSAGE(0, err, "Should get timestamp");
    TEST_ASSERT_EQUAL_MESSAGE(expected->timestamp, timestamp, "Should have correct timestamp");

    // Get timezone (optional validation - just check it exists)
    char timezone[64];
    err = json_obj_get_string(&jctx, "tz", timezone, sizeof(timezone));
    TEST_ASSERT_EQUAL_MESSAGE(0, err, "Should get timezone");

    // Clean up parser
    err = json_parse_end_static(&jctx);
    TEST_ASSERT_EQUAL_MESSAGE(0, err, "Should end JSON parsing successfully");
}

/**
 * @brief Helper function to get next payload using the correct API pattern.
 * @return JSON payload string, or NULL if queue is empty. Caller must free the payload.
 */
static char *__get_next_payload(void)
{
    if (__timeseries_queue == NULL) {
        return NULL;
    }

    __timeseries_data_t data = {0};
    osal_err_t queue_err = osal_queue_receive(__timeseries_queue, &data, 0);
    if (queue_err != OSAL_ERR_OK) {
        return NULL;
    }

    char *payload = __timeseries_data_to_json(&data);
    __timeseries_free_data_internals(&data);
    return payload;
}

/* Test functions **************************************************************/

void test_timeseries_init_deinit(void)
{
    OSAL_LOGI(TEST_TAG, "Testing timeseries initialization and deinitialization");

    // Test initialization
    esp_rmaker_error_t err = timeseries_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "Should initialize successfully");

    // Test double initialization (should be idempotent)
    err = timeseries_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "Double initialization should be idempotent");

    // Test deinitialization
    err = timeseries_deinit();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "Should deinitialize successfully");

    // Test double deinitialization (should be idempotent)
    err = timeseries_deinit();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "Double deinitialization should be idempotent");
}

void test_timeseries_push_data_basic(void)
{
    OSAL_LOGI(TEST_TAG, "Testing basic timeseries data push functionality");

    // Initialize timeseries
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_init(), "Should initialize successfully");

    // Test push data with different types
    test_timeseries_data_t test_data[] = {
        {"temperature", esp_rmaker_float(22.5f), false, TEST_TIMESTAMP_BASE},
        {"humidity", esp_rmaker_float(65.2f), false, TEST_TIMESTAMP_BASE + 1},
        {"power", esp_rmaker_bool(true), false, TEST_TIMESTAMP_BASE + 2},
        {"count", esp_rmaker_int(42), false, TEST_TIMESTAMP_BASE + 3},
        {"message", esp_rmaker_str("test"), false, TEST_TIMESTAMP_BASE + 4}
    };

    uint32_t test_count = sizeof(test_data) / sizeof(test_data[0]);

    // Push all test data
    for (uint32_t i = 0; i < test_count; i++) {
        __timeseries_data_t data = __create_test_data(
                                       test_data[i].path,
                                       test_data[i].val,
                                       test_data[i].is_cumulative,
                                       test_data[i].timestamp
                                   );

        esp_rmaker_error_t err = __timeseries_push_data(&data);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "Should push data successfully");
    }

    // Verify data is in queue by getting payloads one by one
    for (uint32_t i = 0; i < test_count; i++) {
        char *payload = __get_next_payload();
        TEST_ASSERT_NOT_NULL_MESSAGE(payload, "Should get valid payload");

        // Validate JSON structure for this single item
        __validate_json_payload(payload, &test_data[i]);

        // Cleanup
        free(payload);
    }

    // Verify no more payloads
    char *payload = __get_next_payload();
    TEST_ASSERT_NULL_MESSAGE(payload, "Should not have more payloads");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_deinit(), "Should deinitialize successfully");
}

void test_timeseries_push_data_validation(void)
{
    OSAL_LOGI(TEST_TAG, "Testing timeseries data validation (reject object/array types)");

    // Initialize timeseries
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_init(), "Should initialize successfully");

    // Test data with object type (should be rejected)
    __timeseries_data_t object_data = __create_test_data(
                                          "object_param",
                                          esp_rmaker_obj("{\"key\":\"value\"}"),
                                          false,
                                          TEST_TIMESTAMP_BASE
                                      );

    esp_rmaker_error_t err = __timeseries_push_data(&object_data);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "Should reject object data type");

    // Test data with array type (should be rejected)
    __timeseries_data_t array_data = __create_test_data(
                                         "array_param",
                                         esp_rmaker_array("[1,2,3]"),
                                         false,
                                         TEST_TIMESTAMP_BASE + 1
                                     );

    err = __timeseries_push_data(&array_data);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "Should reject array data type");

    // Valid data types should still work
    test_timeseries_data_t valid_data_template = {
        .path = "temperature",
        .val = esp_rmaker_float(22.5f),
        .is_cumulative = false,
        .timestamp = TEST_TIMESTAMP_BASE + 2
    };
    __timeseries_data_t valid_data = __create_test_data(
                                         valid_data_template.path,
                                         valid_data_template.val,
                                         valid_data_template.is_cumulative,
                                         valid_data_template.timestamp
                                     );

    err = __timeseries_push_data(&valid_data);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "Should accept valid data types");

    // Verify only valid data is in queue
    char *payload = __get_next_payload();
    TEST_ASSERT_NOT_NULL_MESSAGE(payload, "Should get valid payload");
    __validate_json_payload(payload, &valid_data_template);

    // Cleanup
    free(payload);

    // Verify no more payloads
    payload = __get_next_payload();
    TEST_ASSERT_NULL_MESSAGE(payload, "Should not have more payloads");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_deinit(), "Should deinitialize successfully");
}

void test_timeseries_get_payload_basic(void)
{
    OSAL_LOGI(TEST_TAG, "Testing basic timeseries get payload functionality");

    // Initialize timeseries
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_init(), "Should initialize successfully");

    // Test empty queue
    char *payload = __get_next_payload();
    TEST_ASSERT_NULL_MESSAGE(payload, "Should get NULL payload for empty queue");

    // Add single item
    __timeseries_data_t data = __create_test_data(
                                   "temperature",
                                   esp_rmaker_float(22.5f),
                                   false,
                                   TEST_TIMESTAMP_BASE
                               );

    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, __timeseries_push_data(&data), "Should push data successfully");

    // Get payload
    payload = __get_next_payload();
    TEST_ASSERT_NOT_NULL_MESSAGE(payload, "Should get valid payload");

    // Validate payload
    test_timeseries_data_t expected_data[] = {
        {"temperature", esp_rmaker_float(22.5f), false, TEST_TIMESTAMP_BASE}
    };
    __validate_json_payload(payload, expected_data);

    // Cleanup
    if (payload) {
        free(payload);
    }

    // Verify no more payloads
    payload = __get_next_payload();
    TEST_ASSERT_NULL_MESSAGE(payload, "Should not have more payloads");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_deinit(), "Should deinitialize successfully");
}

void test_timeseries_get_payload_multiple(void)
{
    OSAL_LOGI(TEST_TAG, "Testing timeseries get payload with multiple items");

    // Initialize timeseries
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_init(), "Should initialize successfully");

    // Add multiple items
    uint32_t item_count = 3;
    test_timeseries_data_t test_data[item_count];

    for (uint32_t i = 0; i < item_count; i++) {
        test_data[i].path = "temperature";
        test_data[i].val = esp_rmaker_float(20.0f + i);
        test_data[i].is_cumulative = false;
        test_data[i].timestamp = TEST_TIMESTAMP_BASE + i;

        __timeseries_data_t data = __create_test_data(
                                       test_data[i].path,
                                       test_data[i].val,
                                       test_data[i].is_cumulative,
                                       test_data[i].timestamp
                                   );

        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, __timeseries_push_data(&data), "Should push data successfully");
    }

    // Get payloads one by one and validate each
    for (uint32_t i = 0; i < item_count; i++) {
        char *payload = __get_next_payload();
        TEST_ASSERT_NOT_NULL_MESSAGE(payload, "Should get valid payload");

        // Validate payload for this single item
        __validate_json_payload(payload, &test_data[i]);

        // Cleanup
        free(payload);
    }

    // Verify no more payloads
    char *payload = __get_next_payload();
    TEST_ASSERT_NULL_MESSAGE(payload, "Should not have more payloads");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_deinit(), "Should deinitialize successfully");
}

void test_timeseries_get_payload_max_items(void)
{
    OSAL_LOGI(TEST_TAG, "Testing timeseries get payload with large number of items");

    // Initialize timeseries
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_init(), "Should initialize successfully");

    // Add multiple items to test individual payload retrieval
    uint32_t total_items = TEST_MAX_ITEMS_IN_PAYLOAD + 5; // More than the configured max
    test_timeseries_data_t test_data[total_items];

    for (uint32_t i = 0; i < total_items; i++) {
        test_data[i].path = "temperature";
        test_data[i].val = esp_rmaker_float(20.0f + i);
        test_data[i].is_cumulative = false;
        test_data[i].timestamp = TEST_TIMESTAMP_BASE + i;

        __timeseries_data_t data = __create_test_data(
                                       test_data[i].path,
                                       test_data[i].val,
                                       test_data[i].is_cumulative,
                                       test_data[i].timestamp
                                   );

        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, __timeseries_push_data(&data), "Should push data successfully");
    }

    // Get payloads one by one and validate each
    for (uint32_t i = 0; i < total_items; i++) {
        char *payload = __get_next_payload();
        char not_null_message[128];
        snprintf(not_null_message, sizeof(not_null_message), "Should get valid payload for item %" PRIu32, i);
        TEST_ASSERT_NOT_NULL_MESSAGE(payload, not_null_message);

        // Each payload should contain exactly one item
        __validate_json_payload(payload, &test_data[i]);

        // Cleanup payload
        free(payload);
    }

    // Verify no more payloads
    char *payload = __get_next_payload();
    TEST_ASSERT_NULL_MESSAGE(payload, "Should not have more payloads");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_deinit(), "Should deinitialize successfully");
}


void test_timeseries_uninitialized_access(void)
{
    OSAL_LOGI(TEST_TAG, "Testing timeseries access when not initialized");

    // Test push data without initialization
    __timeseries_data_t data = __create_test_data(
                                   "temperature",
                                   esp_rmaker_float(22.5f),
                                   false,
                                   TEST_TIMESTAMP_BASE
                               );

    esp_rmaker_error_t err = __timeseries_push_data(&data);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_FAIL, err, "Should fail when not initialized");
    __timeseries_free_data_internals(&data);

    // Test get payload without initialization
    char *payload = __get_next_payload();
    TEST_ASSERT_NULL_MESSAGE(payload, "Should get NULL payload when not initialized");
}

void test_timeseries_data_types(void)
{
    OSAL_LOGI(TEST_TAG, "Testing timeseries with different data types");

    // Initialize timeseries
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_init(), "Should initialize successfully");

    // Test all supported data types
    test_timeseries_data_t test_cases[] = {
        {"bool_param", esp_rmaker_bool(true), false, TEST_TIMESTAMP_BASE},
        {"int_param", esp_rmaker_int(42), false, TEST_TIMESTAMP_BASE + 1},
        {"float_param", esp_rmaker_float(22.5f), false, TEST_TIMESTAMP_BASE + 2},
        {"string_param", esp_rmaker_str("test_string"), false, TEST_TIMESTAMP_BASE + 3},
        {"cumulative_int", esp_rmaker_int(100), true, TEST_TIMESTAMP_BASE + 4},
        {"cumulative_float", esp_rmaker_float(50.5f), true, TEST_TIMESTAMP_BASE + 5}
    };

    uint32_t test_count = sizeof(test_cases) / sizeof(test_cases[0]);

    // Push all test data
    for (uint32_t i = 0; i < test_count; i++) {
        __timeseries_data_t data = __create_test_data(
                                       test_cases[i].path,
                                       test_cases[i].val,
                                       test_cases[i].is_cumulative,
                                       test_cases[i].timestamp
                                   );

        esp_rmaker_error_t err = __timeseries_push_data(&data);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "Should push data successfully");
    }

    // Get payloads one by one and validate each
    for (uint32_t i = 0; i < test_count; i++) {
        char *payload = __get_next_payload();
        char not_null_message[128];
        snprintf(not_null_message, sizeof(not_null_message), "Should get valid payload for item %" PRIu32, i);
        TEST_ASSERT_NOT_NULL_MESSAGE(payload, not_null_message);

        // Validate JSON structure for this single item
        __validate_json_payload(payload, &test_cases[i]);

        // Cleanup
        free(payload);
    }

    // Verify no more payloads
    char *payload = __get_next_payload();
    TEST_ASSERT_NULL_MESSAGE(payload, "Should not have more payloads");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_deinit(), "Should deinitialize successfully");
}

void test_timeseries_error_paths(void)
{
    OSAL_LOGI(TEST_TAG, "Testing timeseries error paths and edge cases");

    // Test push null data
    esp_rmaker_error_t err = __timeseries_push_data(NULL);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "Should reject null data pointer");

    // Initialize timeseries
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_init(), "Should initialize successfully");

    // Test push null data after initialization
    err = __timeseries_push_data(NULL);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "Should reject null data pointer after init");

    // Test with invalid data (empty id)
    __timeseries_data_t invalid_data = __create_test_data(
                                           "", // Empty id
                                           esp_rmaker_float(22.5f),
                                           false,
                                           TEST_TIMESTAMP_BASE
                                       );

    err = __timeseries_push_data(&invalid_data);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "Should accept empty id (implementation allows it)");

    // Test with NULL id
    invalid_data.param.path = NULL;
    err = __timeseries_push_data(&invalid_data);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "Should reject NULL id");

    // Test with NULL IANA timezone
    invalid_data.timestamp.iana_tz = NULL;
    err = __timeseries_push_data(&invalid_data);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "Should reject NULL IANA timezone");

    // Cleanup
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_deinit(), "Should deinitialize successfully");
}

void test_timeseries_queue_behavior(void)
{
    OSAL_LOGI(TEST_TAG, "Testing timeseries queue behavior (FIFO order)");

    // Initialize timeseries
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_init(), "Should initialize successfully");

    // Add items in specific order
    const char *param_names[] = {"temp1", "temp2", "temp3", "temp4", "temp5"};
    uint32_t item_count = sizeof(param_names) / sizeof(param_names[0]);
    test_timeseries_data_t test_data[item_count];

    for (uint32_t i = 0; i < item_count; i++) {
        test_data[i].path = param_names[i];
        test_data[i].val = esp_rmaker_float(20.0f + i);
        test_data[i].is_cumulative = false;
        test_data[i].timestamp = TEST_TIMESTAMP_BASE + i;

        __timeseries_data_t data = __create_test_data(
                                       param_names[i],
                                       test_data[i].val,
                                       test_data[i].is_cumulative,
                                       test_data[i].timestamp
                                   );
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, __timeseries_push_data(&data), "Should push data successfully");
    }

    // Get payloads one by one and verify FIFO order (first items should come first)
    for (uint32_t i = 0; i < item_count; i++) {
        char *payload = __get_next_payload();
        char not_null_message[128];
        snprintf(not_null_message, sizeof(not_null_message), "Should get valid payload for item %" PRIu32, i);
        TEST_ASSERT_NOT_NULL_MESSAGE(payload, not_null_message);

        // Validation is done in FIFO order - each payload should match the corresponding test data
        __validate_json_payload(payload, &test_data[i]);

        // Cleanup
        free(payload);
    }

    // Verify no more payloads
    char *payload = __get_next_payload();
    TEST_ASSERT_NULL_MESSAGE(payload, "Should not have more payloads");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, timeseries_deinit(), "Should deinitialize successfully");
}
