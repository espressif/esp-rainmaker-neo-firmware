/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "osal_storage.h"

static const char *TEST_PARTITION = "nvs"; // use default partition for ESP
static const char *TEST_NAMESPACE = "test_ns";

void test_nvs_basic_flow(void)
{
    osal_err_t err;
    osal_storage_handle_t handle = NULL;

    err = osal_storage_init(NULL);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_open(TEST_PARTITION, TEST_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    TEST_ASSERT_NOT_NULL(handle);

    const char *key = "k";
    const char *payload = "test payload that is quite long";
    size_t payload_len = strlen(payload) + 1;
    err = osal_storage_set(handle, key, payload, payload_len, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    char buf[64] = {0};
    size_t read_len = sizeof(buf);
    err = osal_storage_get(handle, key, buf, &read_len, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    TEST_ASSERT_EQUAL_UINT(payload_len, read_len);
    TEST_ASSERT_EQUAL_STRING(payload, buf);

    err = osal_storage_commit(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_erase(handle, key);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_erase_all(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_close(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_deinit(NULL);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
}

void test_nvs_iterator_basic_flow(void)
{
    osal_err_t err;
    osal_storage_handle_t handle = NULL;
    osal_storage_iterator_t iterator = NULL;

    err = osal_storage_init(NULL);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_open(TEST_PARTITION, TEST_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    TEST_ASSERT_NOT_NULL(handle);

    /* Set multiple values of different types */
    const char *key1 = "key1";
    const char *key2 = "key2";
    const char *key3 = "key3";
    const char *key4 = "key4";
    const char *val1 = "test string 1";
    uint16_t val2 = 1234;
    const char *val3 = "test string 2";
    int32_t val4 = -5678;

    err = osal_storage_set(handle, key1, val1, strlen(val1) + 1, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_set(handle, key2, &val2, sizeof(val2), OSAL_STORAGE_TYPE_U16);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_set(handle, key3, val3, strlen(val3) + 1, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_set(handle, key4, &val4, sizeof(val4), OSAL_STORAGE_TYPE_I32);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_commit(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    /* Test finding binary entries */
    err = osal_storage_entry_find(TEST_PARTITION, TEST_NAMESPACE, OSAL_STORAGE_TYPE_BINARY, &iterator);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    TEST_ASSERT_NOT_NULL(iterator);

    /* Check first entry */
    osal_storage_entry_t entry;
    err = osal_storage_entry_get_info(iterator, &entry);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    TEST_ASSERT_EQUAL_STRING(key1, entry.key);

    /* Move to next entry */
    err = osal_storage_entry_next(&iterator);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    /* Check second entry */
    err = osal_storage_entry_get_info(iterator, &entry);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    TEST_ASSERT_EQUAL_STRING(key3, entry.key);

    /* No more entries */
    err = osal_storage_entry_next(&iterator);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_NVS_KEY_NOT_FOUND, err);

    /* Release iterator */
    err = osal_storage_release_iterator(iterator);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    /* Clean up */
    err = osal_storage_erase(handle, key1);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_erase(handle, key2);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_erase(handle, key3);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_erase(handle, key4);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_close(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_deinit(NULL);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
}

void test_nvs_error_paths(void)
{
    osal_err_t err;
    osal_storage_handle_t handle = NULL;

    /* Uninitialized operations */
    char buf[8];
    size_t len = sizeof(buf);
    err = osal_storage_get(handle, "key", buf, &len, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_INVALID_ARG, err, "Expected OSAL_ERR_INVALID_ARG for NULL handle");

    /* Bad parameters */
    err = osal_storage_init(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_OK, err, "Expected OSAL_ERR_OK for NULL partition");

    err = osal_storage_open(NULL, TEST_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_OK, err, "Expected OSAL_ERR_OK for NULL partition with namespace");

    err = osal_storage_open(TEST_PARTITION, NULL, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_INVALID_ARG, err, "Expected OSAL_ERR_INVALID_ARG for NULL namespace");

    /* Proper open */
    err = osal_storage_open(TEST_PARTITION, TEST_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_OK, err, "Expected OSAL_ERR_OK for proper open");

    err = osal_storage_set(handle, NULL, "x", 2, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_INVALID_ARG, err, "Expected OSAL_ERR_INVALID_ARG for NULL key");

    err = osal_storage_get(handle, NULL, buf, &len, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_INVALID_ARG, err, "Expected OSAL_ERR_INVALID_ARG for NULL key");

    err = osal_storage_close(handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_OK, err, "Expected OSAL_ERR_OK for proper close");

    err = osal_storage_deinit(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_OK, err, "Expected OSAL_ERR_OK for proper deinit");
}
