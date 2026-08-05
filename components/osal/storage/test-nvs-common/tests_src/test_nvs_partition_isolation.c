/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_nvs_partition_isolation.c
 * @brief Per-partition init/deinit/reset must not affect other partition labels (POSIX and ESP-IDF).
 *
 * Labels match RainMaker Neo defaults (see components/esp_rmaker_neo_common/include/constants/esp_rmaker_nvs_common.h)
 * and the ESP-IDF test app partition table (test/apps/esp-idf/partitions.csv: nvs, fctry).
 *
 * Expected error codes follow ESP-IDF mapping in nvs_esp_impl.c: e.g. open on a deinited partition
 * -> OSAL_ERR_NVS_PARTITION_NOT_FOUND; repeat nvs_flash_deinit_partition -> OSAL_ERR_NVS_NOT_INITIALIZED.
 */

#include <string.h>
#include "unity.h"
#include "osal_storage.h"

static const char *PART_MAIN = "nvs";
static const char *PART_FACTORY = "fctry";
static const char *NS_MAIN = "iso_ns";
static const char *KEY = "k";
static const char *VAL = "v1";

static void cleanup_partition(const char *label, const char *ns)
{
    osal_storage_handle_t h = NULL;
    if (osal_storage_open(label, ns, OSAL_STORAGE_OPEN_READWRITE, &h) == OSAL_ERR_OK && h) {
        (void)osal_storage_erase_all(h);
        (void)osal_storage_commit(h);
        (void)osal_storage_close(h);
    }
    (void)osal_storage_deinit((char *)label);
}

void test_nvs_partition_deinit_isolates_labels(void)
{
    osal_err_t err;
    osal_storage_handle_t handle = NULL;

    cleanup_partition(PART_MAIN, NS_MAIN);
    cleanup_partition(PART_FACTORY, NS_MAIN);

    err = osal_storage_init((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_init((char *)PART_FACTORY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_open(PART_MAIN, NS_MAIN, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_set(handle, KEY, VAL, strlen(VAL) + 1, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_commit(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_close(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    /* Deinit factory only (same pattern as esp_rmaker_factory_part_deinit). */
    err = osal_storage_deinit((char *)PART_FACTORY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    /* Main partition must remain usable without re-init. */
    handle = NULL;
    err = osal_storage_open(PART_MAIN, NS_MAIN, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_OK, err, "main open after factory deinit");
    TEST_ASSERT_NOT_NULL(handle);

    char buf[16] = {0};
    size_t len = sizeof(buf);
    err = osal_storage_get(handle, KEY, buf, &len, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    TEST_ASSERT_EQUAL_STRING(VAL, buf);

    err = osal_storage_close(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_deinit((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    /* ESP-IDF: second deinit on same partition -> ESP_ERR_NVS_NOT_INITIALIZED */
    err = osal_storage_deinit((char *)PART_FACTORY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_NVS_NOT_INITIALIZED, err);
}

void test_nvs_partition_open_after_deinit_own_label_only(void)
{
    osal_err_t err;
    osal_storage_handle_t handle = NULL;

    cleanup_partition(PART_MAIN, NS_MAIN);
    cleanup_partition(PART_FACTORY, NS_MAIN);

    err = osal_storage_init((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_init((char *)PART_FACTORY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_deinit((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_open(PART_MAIN, NS_MAIN, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_NVS_PARTITION_NOT_FOUND, err,
                                  "ESP-IDF: deinited partition open -> ESP_ERR_NVS_PART_NOT_FOUND");

    handle = NULL;
    err = osal_storage_open(PART_FACTORY, NS_MAIN, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_OK, err, "factory should still open after main deinit");
    TEST_ASSERT_NOT_NULL(handle);
    err = osal_storage_close(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_init((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    handle = NULL;
    err = osal_storage_open(PART_MAIN, NS_MAIN, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_close(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_deinit((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_deinit((char *)PART_FACTORY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
}

void test_nvs_partition_reset_isolates_labels(void)
{
    osal_err_t err;
    osal_storage_handle_t handle = NULL;

    cleanup_partition(PART_MAIN, NS_MAIN);
    cleanup_partition(PART_FACTORY, NS_MAIN);

    err = osal_storage_init((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_init((char *)PART_FACTORY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_open(PART_FACTORY, NS_MAIN, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_set(handle, KEY, VAL, strlen(VAL) + 1, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_commit(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_close(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_reset((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    /* Factory data and init flag for factory must survive reset of main. */
    handle = NULL;
    err = osal_storage_open(PART_FACTORY, NS_MAIN, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_OK, err, "factory open after main reset");
    char buf[16] = {0};
    size_t len = sizeof(buf);
    err = osal_storage_get(handle, KEY, buf, &len, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    TEST_ASSERT_EQUAL_STRING(VAL, buf);
    err = osal_storage_close(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    /* Main erased: ESP-IDF open before re-init -> PARTITION_NOT_FOUND. */
    handle = NULL;
    err = osal_storage_open(PART_MAIN, NS_MAIN, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_NVS_PARTITION_NOT_FOUND, err);

    err = osal_storage_init((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    handle = NULL;
    err = osal_storage_open(PART_MAIN, NS_MAIN, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_close(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_deinit((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_deinit((char *)PART_FACTORY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
}

void test_nvs_partition_entry_find_after_peer_deinit(void)
{
    osal_err_t err;
    osal_storage_handle_t handle = NULL;
    osal_storage_iterator_t iterator = NULL;

    cleanup_partition(PART_MAIN, NS_MAIN);
    cleanup_partition(PART_FACTORY, NS_MAIN);

    err = osal_storage_init((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_init((char *)PART_FACTORY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_open(PART_MAIN, NS_MAIN, OSAL_STORAGE_OPEN_READWRITE, &handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_set(handle, KEY, VAL, strlen(VAL) + 1, OSAL_STORAGE_TYPE_BINARY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_commit(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    err = osal_storage_close(handle);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_deinit((char *)PART_FACTORY);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_entry_find(PART_MAIN, NS_MAIN, OSAL_STORAGE_TYPE_BINARY, &iterator);
    TEST_ASSERT_EQUAL_INT_MESSAGE(OSAL_ERR_OK, err, "entry_find on main after factory deinit");
    TEST_ASSERT_NOT_NULL(iterator);

    osal_storage_entry_t entry;
    err = osal_storage_entry_get_info(iterator, &entry);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
    TEST_ASSERT_EQUAL_STRING(KEY, entry.key);

    err = osal_storage_release_iterator(iterator);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);

    err = osal_storage_deinit((char *)PART_MAIN);
    TEST_ASSERT_EQUAL_INT(OSAL_ERR_OK, err);
}
