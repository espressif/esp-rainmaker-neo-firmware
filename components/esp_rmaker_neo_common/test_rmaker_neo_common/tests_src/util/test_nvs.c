/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_nvs.c
 * @brief Test the NVS utility functions.
 */

#include "unity.h"
#include "test_rmng_common_prototypes.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "util/esp_rmaker_nvs.h"
#include "osal_storage.h"

/* Test data ******************************************************************/

#define TEST_PARTITION "nvs"
#define TEST_NAMESPACE "test_ns"
#define TEST_KEY_INT "tk_int"
#define TEST_KEY_STRING "tk_string"
#define TEST_INVALID_KEY "no_key"

static const char *test_string_values[] = {
    "hello world",
    "test123",
    "a very long test string with special characters !@#$%^&*()_+-={}[]|\\:;\"'<>?,./",
    "short"
};

static const int test_int_values[] = {
    42,
    -1,
    0,
    2147483647,  // INT_MAX
    -2147483648  // INT_MIN
};

#define TEST_STRING_COUNT (sizeof(test_string_values) / sizeof(test_string_values[0]))
#define TEST_INT_COUNT (sizeof(test_int_values) / sizeof(test_int_values[0]))

/* Helper functions **********************************************************/

/**
 * @brief Setup NVS for testing - initialize partition and clear test namespace
 */
static void setup_test_nvs(void)
{
    // Initialize the test partition
    osal_storage_init(TEST_PARTITION);

    // Clear any existing test data by opening and erasing namespace
    osal_storage_handle_t handle;
    osal_err_t err = osal_storage_open(TEST_PARTITION, TEST_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &handle);
    if (err == OSAL_ERR_OK) {
        osal_storage_erase_all(handle);
        osal_storage_commit(handle);
        osal_storage_close(handle);
    }
}

/**
 * @brief Cleanup NVS after testing
 */
static void cleanup_test_nvs(void)
{
    // Clean up any test data
    osal_storage_handle_t handle;
    osal_err_t err = osal_storage_open(TEST_PARTITION, TEST_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &handle);
    if (err == OSAL_ERR_OK) {
        osal_storage_erase_all(handle);
        osal_storage_commit(handle);
        osal_storage_close(handle);
    }
}

/* Integer Tests *************************************************************/

void test_rmaker_nvs_update_int_success(void)
{
    setup_test_nvs();

    for (int i = 0; i < TEST_INT_COUNT; i++) {
        esp_rmaker_error_t err = esp_rmaker_nvs_update_int(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, test_int_values[i]);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "nvs_update_int should succeed");

        // Verify the value was actually stored by reading it back
        int stored_value = esp_rmaker_nvs_get_int_default(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, -1);
        TEST_ASSERT_EQUAL_MESSAGE(test_int_values[i], stored_value, "stored int value should match");
    }

    cleanup_test_nvs();
}

void test_rmaker_nvs_update_int_null_params(void)
{
    setup_test_nvs();

    // Test with NULL partition name
    esp_rmaker_error_t err = esp_rmaker_nvs_update_int(NULL, TEST_NAMESPACE, TEST_KEY_INT, 42);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "nvs_update_int should pass with NULL partition");

    // Test with NULL namespace
    err = esp_rmaker_nvs_update_int(TEST_PARTITION, NULL, TEST_KEY_INT, 42);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "nvs_update_int should fail with NULL namespace");

    // Test with NULL key
    err = esp_rmaker_nvs_update_int(TEST_PARTITION, TEST_NAMESPACE, NULL, 42);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "nvs_update_int should fail with NULL key");

    cleanup_test_nvs();
}

void test_rmaker_nvs_get_int_success(void)
{
    setup_test_nvs();

    for (int i = 0; i < TEST_INT_COUNT; i++) {
        // First store a value
        esp_rmaker_error_t err = esp_rmaker_nvs_update_int(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, test_int_values[i]);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "update should succeed");

        // Then retrieve it
        int result = esp_rmaker_nvs_get_int_default(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, -1);
        TEST_ASSERT_EQUAL_MESSAGE(test_int_values[i], result, "get_int should return stored value");
    }

    cleanup_test_nvs();
}

void test_rmaker_nvs_get_int_not_found(void)
{
    setup_test_nvs();

    // Try to get a key that doesn't exist
    int result = esp_rmaker_nvs_get_int_default(TEST_PARTITION, TEST_NAMESPACE, TEST_INVALID_KEY, -1);
    TEST_ASSERT_EQUAL_MESSAGE(-1, result, "get_int should return -1 when key not found");

    cleanup_test_nvs();
}

/* String Tests **************************************************************/

void test_rmaker_nvs_update_string_success(void)
{
    setup_test_nvs();

    for (int i = 0; i < TEST_STRING_COUNT; i++) {
        esp_rmaker_error_t err = esp_rmaker_nvs_update_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING, test_string_values[i]);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "nvs_update_string should succeed");

        // Verify the value was actually stored by reading it back
        char *stored_value = esp_rmaker_nvs_get_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING);
        TEST_ASSERT_NOT_NULL_MESSAGE(stored_value, "stored string should not be NULL");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(test_string_values[i], stored_value, "stored string value should match");
        free(stored_value);
    }

    cleanup_test_nvs();
}

void test_rmaker_nvs_update_string_null_params(void)
{
    setup_test_nvs();

    // Test with NULL partition name
    esp_rmaker_error_t err = esp_rmaker_nvs_update_string(NULL, TEST_NAMESPACE, TEST_KEY_STRING, "test");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "nvs_update_string should pass with NULL partition");

    // Test with NULL namespace
    err = esp_rmaker_nvs_update_string(TEST_PARTITION, NULL, TEST_KEY_STRING, "test");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "nvs_update_string should fail with NULL namespace");

    // Test with NULL key
    err = esp_rmaker_nvs_update_string(TEST_PARTITION, TEST_NAMESPACE, NULL, "test");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "nvs_update_string should fail with NULL key");

    // Note: NULL value would cause strlen() to crash in the actual implementation
    // so we don't test that case here

    cleanup_test_nvs();
}

void test_rmaker_nvs_get_string_success(void)
{
    setup_test_nvs();

    for (int i = 0; i < TEST_STRING_COUNT; i++) {
        // First store a value
        esp_rmaker_error_t err = esp_rmaker_nvs_update_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING, test_string_values[i]);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "update should succeed");

        // Then retrieve it
        char *result = esp_rmaker_nvs_get_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "get_string should return valid pointer");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(test_string_values[i], result, "get_string should return stored value");

        // Verify null terminator
        TEST_ASSERT_EQUAL_MESSAGE('\0', result[strlen(test_string_values[i])], "string should be null terminated");

        // Free allocated memory
        free(result);
    }

    cleanup_test_nvs();
}

void test_rmaker_nvs_get_string_not_found(void)
{
    setup_test_nvs();

    // Try to get a key that doesn't exist
    char *result = esp_rmaker_nvs_get_string(TEST_PARTITION, TEST_NAMESPACE, TEST_INVALID_KEY);
    TEST_ASSERT_NULL_MESSAGE(result, "get_string should return NULL when key not found");

    cleanup_test_nvs();
}

/* Edge case and integration tests *******************************************/

void test_rmaker_nvs_round_trip_int(void)
{
    setup_test_nvs();

    for (int i = 0; i < TEST_INT_COUNT; i++) {
        // Update
        esp_rmaker_error_t err = esp_rmaker_nvs_update_int(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, test_int_values[i]);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "update should succeed");

        // Get
        int result = esp_rmaker_nvs_get_int_default(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, -1);
        TEST_ASSERT_EQUAL_MESSAGE(test_int_values[i], result, "round trip should preserve value");
    }

    cleanup_test_nvs();
}

void test_rmaker_nvs_round_trip_string(void)
{
    setup_test_nvs();

    for (int i = 0; i < TEST_STRING_COUNT; i++) {
        // Update
        esp_rmaker_error_t err = esp_rmaker_nvs_update_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING, test_string_values[i]);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "update should succeed");

        // Get
        char *result = esp_rmaker_nvs_get_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "get should return valid pointer");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(test_string_values[i], result, "round trip should preserve value");

        free(result);
    }

    cleanup_test_nvs();
}

void test_rmaker_nvs_multiple_keys(void)
{
    setup_test_nvs();

    // Test storing multiple different keys in the same namespace
    esp_rmaker_error_t err1 = esp_rmaker_nvs_update_int(TEST_PARTITION, TEST_NAMESPACE, "key1", 42);
    esp_rmaker_error_t err2 = esp_rmaker_nvs_update_int(TEST_PARTITION, TEST_NAMESPACE, "key2", 99);
    esp_rmaker_error_t err3 = esp_rmaker_nvs_update_string(TEST_PARTITION, TEST_NAMESPACE, "key3", "hello");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err1, "first int update should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err2, "second int update should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err3, "string update should succeed");

    // Verify all values are stored correctly
    int val1 = esp_rmaker_nvs_get_int_default(TEST_PARTITION, TEST_NAMESPACE, "key1", -1);
    int val2 = esp_rmaker_nvs_get_int_default(TEST_PARTITION, TEST_NAMESPACE, "key2", -1);
    char *val3 = esp_rmaker_nvs_get_string(TEST_PARTITION, TEST_NAMESPACE, "key3");

    TEST_ASSERT_EQUAL_MESSAGE(42, val1, "first int value should match");
    TEST_ASSERT_EQUAL_MESSAGE(99, val2, "second int value should match");
    TEST_ASSERT_NOT_NULL_MESSAGE(val3, "string should not be NULL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("hello", val3, "string value should match");

    free(val3);
    cleanup_test_nvs();
}

void test_rmaker_nvs_overwrite_values(void)
{
    setup_test_nvs();

    // Store initial values
    esp_rmaker_nvs_update_int(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, 100);
    esp_rmaker_nvs_update_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING, "initial");

    // Overwrite with new values
    esp_rmaker_error_t err1 = esp_rmaker_nvs_update_int(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, 200);
    esp_rmaker_error_t err2 = esp_rmaker_nvs_update_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING, "updated");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err1, "int update should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err2, "string update should succeed");

    // Verify new values
    int int_val = esp_rmaker_nvs_get_int_default(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, -1);
    char *str_val = esp_rmaker_nvs_get_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING);

    TEST_ASSERT_EQUAL_MESSAGE(200, int_val, "int should be updated value");
    TEST_ASSERT_NOT_NULL_MESSAGE(str_val, "string should not be NULL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("updated", str_val, "string should be updated value");

    free(str_val);
    cleanup_test_nvs();
}

void test_rmaker_clear_nvs_namespace_removes_entries(void)
{
    setup_test_nvs();

    // Populate namespace with data
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_nvs_update_int(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, 111));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_nvs_update_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING, "persist me"));

    // Sanity check values exist
    TEST_ASSERT_EQUAL(111, esp_rmaker_nvs_get_int_default(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, -1));
    char *existing = esp_rmaker_nvs_get_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING);
    TEST_ASSERT_NOT_NULL(existing);
    free(existing);

    // Clear namespace
    esp_rmaker_error_t clear_err = esp_rmaker_clear_nvs_namespace(TEST_PARTITION, TEST_NAMESPACE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, clear_err);

    // Verify data removed
    TEST_ASSERT_EQUAL(-123, esp_rmaker_nvs_get_int_default(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, -123));
    char *cleared = esp_rmaker_nvs_get_string(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING);
    TEST_ASSERT_NULL(cleared);

    cleanup_test_nvs();
}

void test_rmaker_nvs_update_bool_success(void)
{
    setup_test_nvs();

    esp_rmaker_error_t err = esp_rmaker_nvs_update_bool(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_STRING, true);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "nvs_update_bool should succeed");

    osal_storage_handle_t handle;
    osal_storage_open(TEST_PARTITION, TEST_NAMESPACE, OSAL_STORAGE_OPEN_READONLY, &handle);
    bool value = false;
    esp_rmaker_error_t get_err = esp_rmaker_nvs_get_bool_with_handle(handle, TEST_KEY_STRING, &value);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, get_err, "nvs_get_bool should succeed");
    TEST_ASSERT_TRUE(value);
    osal_storage_close(handle);

    cleanup_test_nvs();
}

void test_rmaker_nvs_update_bool_invalid_params(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_nvs_update_bool(NULL, TEST_NAMESPACE, TEST_KEY_STRING, true));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_nvs_update_bool(TEST_PARTITION, NULL, TEST_KEY_STRING, true));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_nvs_update_bool(TEST_PARTITION, TEST_NAMESPACE, NULL, true));
}

void test_rmaker_nvs_update_u16_and_get(void)
{
    setup_test_nvs();

    esp_rmaker_error_t err = esp_rmaker_nvs_update_u16(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, 1234);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "nvs_update_u16 should succeed");

    uint16_t value = 0;
    err = esp_rmaker_nvs_get_u16(TEST_PARTITION, TEST_NAMESPACE, TEST_KEY_INT, &value);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "nvs_get_u16 should succeed");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1234, value, "value should match stored u16");

    cleanup_test_nvs();
}

void test_rmaker_nvs_get_binary_with_handle(void)
{
    setup_test_nvs();

    osal_storage_handle_t handle;
    osal_storage_open(TEST_PARTITION, TEST_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &handle);
    uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    osal_storage_set(handle, "bin", payload, sizeof(payload), OSAL_STORAGE_TYPE_BINARY);
    osal_storage_commit(handle);

    size_t data_len = 0;
    uint8_t *data = esp_rmaker_nvs_get_binary_with_handle(handle, "bin", &data_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(data, "binary data should not be null");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)sizeof(payload), (uint32_t)data_len, "binary length should match");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(payload, data, data_len, "binary payload should match");
    free(data);
    osal_storage_close(handle);

    cleanup_test_nvs();
}

void test_rmaker_nvs_update_binary_round_trip(void)
{
    setup_test_nvs();

    /* Embedded NULs must round-trip verbatim (no implicit strlen truncation). */
    uint8_t payload[] = {0x00, 0x01, 0xFF, 0x00, 0x7F, 0x80, 0x00};
    esp_rmaker_error_t err = esp_rmaker_nvs_update_binary(TEST_PARTITION, TEST_NAMESPACE, "bin",
                             payload, sizeof(payload));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "nvs_update_binary should succeed");

    size_t data_len = 0;
    uint8_t *data = esp_rmaker_nvs_get_binary(TEST_PARTITION, TEST_NAMESPACE, "bin", &data_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(data, "binary data should not be null");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)sizeof(payload), (uint32_t)data_len, "length must be exact, not strlen-truncated");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(payload, data, data_len, "all bytes incl embedded NULs must round-trip");
    free(data);

    cleanup_test_nvs();
}

void test_rmaker_nvs_update_binary_overwrite(void)
{
    setup_test_nvs();

    uint8_t first[] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t second[] = {0x11, 0x22};

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_nvs_update_binary(TEST_PARTITION, TEST_NAMESPACE, "bin", first, sizeof(first)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_nvs_update_binary(TEST_PARTITION, TEST_NAMESPACE, "bin", second, sizeof(second)));

    size_t data_len = 0;
    uint8_t *data = esp_rmaker_nvs_get_binary(TEST_PARTITION, TEST_NAMESPACE, "bin", &data_len);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(second), (uint32_t)data_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second, data, data_len);
    free(data);

    cleanup_test_nvs();
}

void test_rmaker_nvs_update_binary_invalid_params(void)
{
    setup_test_nvs();

    uint8_t payload[] = {0x01};

    /* NULL partition opens default - passes, like the other update_* tests. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_nvs_update_binary(NULL, TEST_NAMESPACE, "bin", payload, sizeof(payload)));
    /* NULL namespace / key -> INVALID_ARG. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_nvs_update_binary(TEST_PARTITION, NULL, "bin", payload, sizeof(payload)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_nvs_update_binary(TEST_PARTITION, TEST_NAMESPACE, NULL, payload, sizeof(payload)));
    /* NULL data with non-zero len is invalid. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_nvs_update_binary(TEST_PARTITION, TEST_NAMESPACE, "bin", NULL, sizeof(payload)));

    /* with_handle variant: NULL handle -> INVALID_ARG. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_nvs_update_binary_with_handle(NULL, "bin", payload, sizeof(payload)));

    cleanup_test_nvs();
}

void test_rmaker_nvs_update_binary_with_handle_round_trip(void)
{
    setup_test_nvs();

    osal_storage_handle_t handle;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK,
                      osal_storage_open(TEST_PARTITION, TEST_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &handle));

    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    esp_rmaker_error_t err = esp_rmaker_nvs_update_binary_with_handle(handle, "bin", payload, sizeof(payload));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "update_binary_with_handle should succeed");

    /* NULL key/handle invalid. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_nvs_update_binary_with_handle(handle, NULL, payload, sizeof(payload)));

    size_t data_len = 0;
    uint8_t *data = esp_rmaker_nvs_get_binary_with_handle(handle, "bin", &data_len);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(payload), (uint32_t)data_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, data, data_len);
    free(data);

    osal_storage_close(handle);
    cleanup_test_nvs();
}

void test_rmaker_nvs_update_binary_zero_length(void)
{
    setup_test_nvs();

    uint8_t payload[] = {0x01};

    /* A zero-length blob is valid; data == NULL is permitted only when len == 0. */
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK,
                              esp_rmaker_nvs_update_binary(TEST_PARTITION, TEST_NAMESPACE, "empty", NULL, 0),
                              "zero-length blob with NULL data should be allowed");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK,
                              esp_rmaker_nvs_update_binary(TEST_PARTITION, TEST_NAMESPACE, "empty", payload, 0),
                              "zero-length blob with non-NULL data should be allowed");

    cleanup_test_nvs();
}
