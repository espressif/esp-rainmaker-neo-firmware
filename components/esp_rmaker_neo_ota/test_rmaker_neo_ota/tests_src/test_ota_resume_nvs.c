/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_ota_resume_nvs.c
 * @brief Unit tests for the OTA auto-resume NVS helpers (ota_nvs.c).
 *
 * Uses the real POSIX NVS implementation (file-backed in nvs_persistent/).
 * Each test group manages its own init/reset so NVS state never leaks between
 * test cases. esp_rmaker_ota_nvs_resume_matches() is pure in-memory and needs
 * no NVS at all.
 */

#include "unity.h"
#include "test_rmng_ota_prototypes.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "esp_rmaker_error_types.h"
#include "osal_storage.h"
#include "ota_nvs.h"
#include "constants/esp_rmaker_nvs_common.h"

/* =========================================================================
 * NVS lifecycle helpers
 * ========================================================================= */

static void nvs_test_begin(void)
{
    osal_storage_reset((char *)RMAKER_NVS_PART_NAME);
    osal_storage_init((char *)RMAKER_NVS_PART_NAME);
}

static void nvs_test_end(void)
{
    osal_storage_reset((char *)RMAKER_NVS_PART_NAME);
}

/* =========================================================================
 * Fixture helpers
 * ========================================================================= */

static void make_desc(esp_rmaker_ota_resume_desc_t *d,
                      const char *md5, uint32_t filesize,
                      esp_rmaker_ota_transport_t transport, uint32_t block_size)
{
    memset(d, 0, sizeof(*d));
    strncpy(d->md5_hex, md5, ESP_RMAKER_OTA_MD5_HEX_LEN);
    d->md5_hex[ESP_RMAKER_OTA_MD5_HEX_LEN] = '\0';
    d->filesize   = filesize;
    d->transport  = (uint8_t)transport;
    d->block_size = block_size;
}

static const char *TEST_MD5 = "d41d8cd98f00b204e9800998ecf8427e"; /* well-known empty-file MD5 */

/* =========================================================================
 * save -> load round-trip
 * ========================================================================= */

void test_resume_nvs_save_load_roundtrip_descriptor(void)
{
    nvs_test_begin();

    esp_rmaker_ota_resume_desc_t saved;
    make_desc(&saved, TEST_MD5, 1048576, ESP_RMAKER_OTA_TRANSPORT_MQTT, 256);

    uint8_t tracker_in[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_ota_nvs_resume_save(&saved, tracker_in, sizeof(tracker_in)));

    esp_rmaker_ota_resume_desc_t loaded;
    void *tracker_out = NULL;
    size_t tracker_len = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_ota_nvs_resume_load(&loaded, &tracker_out, &tracker_len));

    TEST_ASSERT_EQUAL_STRING(saved.md5_hex, loaded.md5_hex);
    TEST_ASSERT_EQUAL_UINT32(saved.filesize, loaded.filesize);
    TEST_ASSERT_EQUAL_UINT8(saved.transport, loaded.transport);
    TEST_ASSERT_EQUAL_UINT32(saved.block_size, loaded.block_size);

    free(tracker_out);
    nvs_test_end();
}

void test_resume_nvs_save_load_roundtrip_tracker_blob(void)
{
    nvs_test_begin();

    esp_rmaker_ota_resume_desc_t desc;
    make_desc(&desc, TEST_MD5, 512000, ESP_RMAKER_OTA_TRANSPORT_MQTT, 512);

    uint8_t tracker_in[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_ota_nvs_resume_save(&desc, tracker_in, sizeof(tracker_in)));

    void *tracker_out = NULL;
    size_t tracker_len = 0;
    esp_rmaker_ota_resume_desc_t unused;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_ota_nvs_resume_load(&unused, &tracker_out, &tracker_len));

    TEST_ASSERT_EQUAL_size_t(sizeof(tracker_in), tracker_len);
    TEST_ASSERT_EQUAL_MEMORY(tracker_in, tracker_out, sizeof(tracker_in));

    free(tracker_out);
    nvs_test_end();
}

/* =========================================================================
 * load when nothing saved -> NOT_FOUND
 * ========================================================================= */

void test_resume_nvs_load_absent_returns_not_found(void)
{
    nvs_test_begin();

    esp_rmaker_ota_resume_desc_t desc;
    void *tracker = NULL;
    size_t tracker_len = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND,
                      esp_rmaker_ota_nvs_resume_load(&desc, &tracker, &tracker_len));
    TEST_ASSERT_NULL(tracker);
    TEST_ASSERT_EQUAL_size_t(0, tracker_len);

    nvs_test_end();
}

/* =========================================================================
 * clear erases both keys -> subsequent load returns NOT_FOUND
 * ========================================================================= */

void test_resume_nvs_clear_after_save_makes_load_not_found(void)
{
    nvs_test_begin();

    esp_rmaker_ota_resume_desc_t desc;
    make_desc(&desc, TEST_MD5, 65536, ESP_RMAKER_OTA_TRANSPORT_MQTT, 256);
    uint8_t tracker = 0xFF;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_ota_nvs_resume_save(&desc, &tracker, sizeof(tracker)));

    /* Verify it's there before clearing. */
    esp_rmaker_ota_resume_desc_t tmp;
    void *tp = NULL;
    size_t tl = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_ota_nvs_resume_load(&tmp, &tp, &tl));
    free(tp);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_ota_nvs_resume_clear());

    tp = NULL; tl = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND,
                      esp_rmaker_ota_nvs_resume_load(&tmp, &tp, &tl));
    TEST_ASSERT_NULL(tp);

    nvs_test_end();
}

/* clear when nothing stored must succeed (no-op). */
void test_resume_nvs_clear_when_empty_is_noop(void)
{
    nvs_test_begin();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_ota_nvs_resume_clear());
    nvs_test_end();
}

/* =========================================================================
 * esp_rmaker_ota_nvs_resume_matches - pure in-memory, no NVS needed
 * ========================================================================= */

void test_resume_matches_identical_descriptors_returns_true(void)
{
    esp_rmaker_ota_resume_desc_t a, b;
    make_desc(&a, TEST_MD5, 1048576, ESP_RMAKER_OTA_TRANSPORT_MQTT, 256);
    make_desc(&b, TEST_MD5, 1048576, ESP_RMAKER_OTA_TRANSPORT_MQTT, 256);
    TEST_ASSERT_TRUE(esp_rmaker_ota_nvs_resume_matches(&a, &b));
}

void test_resume_matches_different_md5_returns_false(void)
{
    esp_rmaker_ota_resume_desc_t a, b;
    make_desc(&a, TEST_MD5,               1048576, ESP_RMAKER_OTA_TRANSPORT_MQTT, 256);
    make_desc(&b, "00000000000000000000000000000000", 1048576, ESP_RMAKER_OTA_TRANSPORT_MQTT, 256);
    TEST_ASSERT_FALSE(esp_rmaker_ota_nvs_resume_matches(&a, &b));
}

void test_resume_matches_different_filesize_returns_false(void)
{
    esp_rmaker_ota_resume_desc_t a, b;
    make_desc(&a, TEST_MD5, 1048576, ESP_RMAKER_OTA_TRANSPORT_MQTT, 256);
    make_desc(&b, TEST_MD5,  524288, ESP_RMAKER_OTA_TRANSPORT_MQTT, 256);
    TEST_ASSERT_FALSE(esp_rmaker_ota_nvs_resume_matches(&a, &b));
}

void test_resume_matches_different_transport_returns_false(void)
{
    esp_rmaker_ota_resume_desc_t a, b;
    make_desc(&a, TEST_MD5, 1048576, ESP_RMAKER_OTA_TRANSPORT_MQTT,  256);
    make_desc(&b, TEST_MD5, 1048576, ESP_RMAKER_OTA_TRANSPORT_NONE,    0);
    TEST_ASSERT_FALSE(esp_rmaker_ota_nvs_resume_matches(&a, &b));
}

/* MQTT: block_size mismatch must reject (bitmap granularity changed). */
void test_resume_matches_mqtt_different_block_size_returns_false(void)
{
    esp_rmaker_ota_resume_desc_t a, b;
    make_desc(&a, TEST_MD5, 1048576, ESP_RMAKER_OTA_TRANSPORT_MQTT, 256);
    make_desc(&b, TEST_MD5, 1048576, ESP_RMAKER_OTA_TRANSPORT_MQTT, 512);
    TEST_ASSERT_FALSE(esp_rmaker_ota_nvs_resume_matches(&a, &b));
}

/* transport NONE must never match (no resume without a known transport). */
void test_resume_matches_transport_none_returns_false(void)
{
    esp_rmaker_ota_resume_desc_t a, b;
    make_desc(&a, TEST_MD5, 1048576, ESP_RMAKER_OTA_TRANSPORT_NONE, 0);
    make_desc(&b, TEST_MD5, 1048576, ESP_RMAKER_OTA_TRANSPORT_NONE, 0);
    TEST_ASSERT_FALSE(esp_rmaker_ota_nvs_resume_matches(&a, &b));
}

void test_resume_matches_null_args_return_false(void)
{
    esp_rmaker_ota_resume_desc_t d;
    make_desc(&d, TEST_MD5, 1048576, ESP_RMAKER_OTA_TRANSPORT_MQTT, 256);
    TEST_ASSERT_FALSE(esp_rmaker_ota_nvs_resume_matches(NULL, &d));
    TEST_ASSERT_FALSE(esp_rmaker_ota_nvs_resume_matches(&d, NULL));
    TEST_ASSERT_FALSE(esp_rmaker_ota_nvs_resume_matches(NULL, NULL));
}
