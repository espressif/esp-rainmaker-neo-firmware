/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_network_common.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "network/common.h"

void test_network_common_init_deinit(void)
{
    /* Init should succeed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_network_init());

    /* Deinit should succeed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_network_deinit());

    /* Double deinit should be OK (idempotent) */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_network_deinit());
}

void test_network_common_payload(void)
{
    uint8_t test_data[] = {1, 2, 3, 4, 5};
    size_t data_len = sizeof(test_data);

    /* Create payload */
    esp_rmaker_network_payload_t *payload = esp_rmaker_network_make_payload(test_data, data_len);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL(data_len, payload->payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(test_data, payload->payload, data_len);

    /* Free payload */
    esp_rmaker_network_free_payload(payload);

    /* NULL payload should be OK */
    esp_rmaker_network_free_payload(NULL);
}

void test_network_common_payload_zero_length(void)
{
    esp_rmaker_network_payload_t *payload = esp_rmaker_network_make_payload(NULL, 0);
    TEST_ASSERT_NULL(payload);
    payload = esp_rmaker_network_make_payload(NULL, 1);
    TEST_ASSERT_NULL(payload);
    payload = esp_rmaker_network_make_payload("non-null", 0);
    TEST_ASSERT_NULL(payload);
}
