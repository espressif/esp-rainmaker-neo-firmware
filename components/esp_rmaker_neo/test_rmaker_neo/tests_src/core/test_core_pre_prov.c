/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_core_pre_prov.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "esp_rmaker_core.h"

void test_core_pre_prov_init_success(void)
{
    /* Init should succeed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_pre_prov_init());

    /* Double init should be OK (idempotent) */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_pre_prov_init());

    /* Cleanup: deinit what was init'd */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_pre_prov_deinit());
}

void test_core_pre_prov_deinit(void)
{
    /* Init first */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_pre_prov_init());

    /* Deinit should succeed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_pre_prov_deinit());

    /* Deinit without init should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, esp_rmaker_pre_prov_deinit());
}
