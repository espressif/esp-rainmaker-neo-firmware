/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_ota_partition.c
 * @brief Unit tests for the partition helpers (util/ota_partition.c).
 *
 * Covers the rollback detection that decides the verdict of a job which is pending
 * verification, when the new firmware crashed before it could report one itself.
 * POSIX only: the fixtures drive the partition states of the POSIX OTA config.
 */

#include "unity.h"
#include "test_rmaker_neo_ota_prototypes.h"

#include <stdint.h>

#include "osal_ota.h"
#include "osal_ota_posix_config.h"
#include "osal_ota_posix_shared.h"
#include "util/ota_partition.h"

static uint8_t __boot_slot(void)
{
    uint8_t boot_idx = 0;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_get_boot_partition(&boot_idx));
    return boot_idx;
}

static uint8_t __other_slot(void)
{
    return (uint8_t)((__boot_slot() + 1) % OSAL_OTA_POSIX_PART_COUNT);
}

static void __set_all_slots_valid(void)
{
    for (uint8_t i = 0; i < OSAL_OTA_POSIX_PART_COUNT; i++) {
        TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_partition_state(i, OSAL_OTA_IMG_VALID));
    }
}

void test_partition_rollback_not_detected_when_all_slots_valid(void)
{
    __set_all_slots_valid();
    TEST_ASSERT_FALSE(esp_rmaker_ota_partition_rollback_detected());
}

void test_partition_rollback_detected_when_other_slot_aborted(void)
{
    __set_all_slots_valid();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_partition_state(__other_slot(), OSAL_OTA_IMG_ABORTED));
    TEST_ASSERT_TRUE(esp_rmaker_ota_partition_rollback_detected());
    __set_all_slots_valid();
}

void test_partition_rollback_detected_when_other_slot_invalid(void)
{
    __set_all_slots_valid();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_partition_state(__other_slot(), OSAL_OTA_IMG_INVALID));
    TEST_ASSERT_TRUE(esp_rmaker_ota_partition_rollback_detected());
    __set_all_slots_valid();
}

void test_partition_rollback_not_detected_when_running_slot_aborted(void)
{
    __set_all_slots_valid();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_partition_state(__boot_slot(), OSAL_OTA_IMG_ABORTED));
    TEST_ASSERT_FALSE(esp_rmaker_ota_partition_rollback_detected());
    __set_all_slots_valid();
}

void test_partition_rollback_not_detected_while_pending_verify(void)
{
    __set_all_slots_valid();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_partition_state(__other_slot(), OSAL_OTA_IMG_PENDING_VERIFY));
    TEST_ASSERT_FALSE(esp_rmaker_ota_partition_rollback_detected());
    __set_all_slots_valid();
}

void test_partition_running_is_pending_verify_follows_boot_slot_state(void)
{
    __set_all_slots_valid();
    TEST_ASSERT_FALSE(esp_rmaker_ota_partition_running_is_pending_verify());
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_partition_state(__boot_slot(), OSAL_OTA_IMG_PENDING_VERIFY));
    TEST_ASSERT_TRUE(esp_rmaker_ota_partition_running_is_pending_verify());
    __set_all_slots_valid();
}
