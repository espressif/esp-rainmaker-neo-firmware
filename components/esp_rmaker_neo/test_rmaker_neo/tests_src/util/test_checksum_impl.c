/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_checksum_impl.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "constants/nvs.h"
#include "osal_storage.h"
#include "checksum_impl.h"
#include "esp_rmaker_error_types.h"


static void cleanup_nvs(void)
{
    /* Clear any existing test data */
    osal_storage_handle_t handle;
    osal_err_t err = osal_storage_open(RMAKER_NVS_PART_NAME, RMAKER_NVS_CHECKSUM_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &handle);
    if (err == OSAL_ERR_OK) {
        osal_storage_erase_all(handle);
        osal_storage_commit(handle);
        osal_storage_close(handle);
    }
}

static void setup_nvs(void)
{
    /* Ensure the partition is initialized for checksum namespace operations */
    osal_storage_init(RMAKER_NVS_PART_NAME);
    cleanup_nvs();
}

void test_checksum_basic(void)
{
    setup_nvs();

    /* Init/deinit should succeed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_checksum_init());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_checksum_deinit());
}

void test_checksum_compare_store(void)
{
    setup_nvs();

    uint8_t a[RMAKER_CHECKSUM_LEN];
    uint8_t b[RMAKER_CHECKSUM_LEN];
    memset(a, 0xAA, sizeof(a));
    memset(b, 0xBB, sizeof(b));

    /* Init */
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, esp_rmaker_checksum_init(), "Failed to initialize checksum");

    /* No key -> treated as changed */
    TEST_ASSERT_EQUAL_MESSAGE(RMAKER_CHECKSUM_CHANGED, esp_rmaker_checksum_compare(a, "ckey"), "Failed to compare checksum");

    /* Store and compare -> not changed */
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, esp_rmaker_checksum_store(a, "ckey"), "Failed to store checksum A");
    TEST_ASSERT_EQUAL_MESSAGE(RMAKER_CHECKSUM_NOT_CHANGED, esp_rmaker_checksum_compare(a, "ckey"), "Failed to compare checksum A");

    /* Different value -> changed */
    TEST_ASSERT_EQUAL_MESSAGE(RMAKER_CHECKSUM_CHANGED, esp_rmaker_checksum_compare(b, "ckey"), "Failed to compare checksum B");

    /* Deinit */
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, esp_rmaker_checksum_deinit(), "Failed to deinitialize checksum");

    cleanup_nvs();
}

void test_checksum_compare_invalid_key(void)
{
    setup_nvs();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, esp_rmaker_checksum_init(), "Failed to initialize checksum");

    uint8_t checksum[RMAKER_CHECKSUM_LEN] = {0};
    TEST_ASSERT_EQUAL_MESSAGE(RMAKER_CHECKSUM_FAILED, esp_rmaker_checksum_compare(checksum, NULL), "compare should fail for NULL key");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, esp_rmaker_checksum_deinit(), "Failed to deinitialize checksum");
    cleanup_nvs();
}

void test_checksum_store_invalid_key(void)
{
    setup_nvs();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, esp_rmaker_checksum_init(), "Failed to initialize checksum");

    uint8_t checksum[RMAKER_CHECKSUM_LEN] = {0};
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_FAIL, esp_rmaker_checksum_store(checksum, NULL), "store should fail for NULL key");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, esp_rmaker_checksum_deinit(), "Failed to deinitialize checksum");
    cleanup_nvs();
}
