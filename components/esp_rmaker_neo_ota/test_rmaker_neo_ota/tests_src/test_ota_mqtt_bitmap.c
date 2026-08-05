/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_ota_mqtt_bitmap.c
 * @brief Unit tests for the MQTT downloader bitmap (mqtt_bitmap.c).
 *
 * Covers:
 *   - bitmap_init: correct cell count, all-bits-set, padding bits for non-multiples of 8
 *   - bitmap_set_processed / bitmap_is_processed: bit clear, unprocessed_block_count decrement
 *   - out-of-range and double-process guards
 *   - Popcount-with-padding-mask formula: standalone verification of the algorithm used in
 *     resume_attempt() to count unprocessed blocks.
 */

#include "unity.h"
#include "test_rmng_ota_prototypes.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_rmaker_error_types.h"
#include "ota_mqtt_downloader_bitmap.h"

/* =========================================================================
 * bitmap_init
 * ========================================================================= */

void test_bitmap_init_exact_multiple_of_8(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(8, &bm));
    TEST_ASSERT_NOT_NULL(bm.bitmap);
    TEST_ASSERT_EQUAL_size_t(8, bm.block_count);
    TEST_ASSERT_EQUAL_size_t(8, bm.unprocessed_block_count);
    /* One cell, all bits set */
    TEST_ASSERT_EQUAL_HEX8(0xFF, bm.bitmap[0]);

    mqtt_downloader_bitmap_deinit(&bm);
}

void test_bitmap_init_non_multiple_has_padding_bits_set(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));

    /* 9 blocks -> 2 cells; cell[1] bit 0 is real (block 8), bits 1-7 are padding */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(9, &bm));
    TEST_ASSERT_NOT_NULL(bm.bitmap);
    TEST_ASSERT_EQUAL_size_t(9, bm.block_count);
    TEST_ASSERT_EQUAL_size_t(9, bm.unprocessed_block_count);
    TEST_ASSERT_EQUAL_HEX8(0xFF, bm.bitmap[0]); /* blocks 0-7, all pending */
    TEST_ASSERT_EQUAL_HEX8(0xFF, bm.bitmap[1]); /* bit 0 = block 8 (real); bits 1-7 = padding (also 1) */

    mqtt_downloader_bitmap_deinit(&bm);
}

void test_bitmap_init_zero_block_count_returns_error(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, mqtt_downloader_bitmap_init(0, &bm));
    TEST_ASSERT_NULL(bm.bitmap);
}

void test_bitmap_init_null_bitmap_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, mqtt_downloader_bitmap_init(8, NULL));
}

/* =========================================================================
 * bitmap_is_processed / bitmap_set_processed
 * ========================================================================= */

void test_bitmap_block_unprocessed_after_init(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(16, &bm));

    for (int32_t i = 0; i < 16; i++) {
        TEST_ASSERT_FALSE_MESSAGE(mqtt_downloader_bitmap_is_processed(&bm, i), "all blocks must be unprocessed after init");
    }

    mqtt_downloader_bitmap_deinit(&bm);
}

void test_bitmap_set_processed_clears_bit_and_decrements_count(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(16, &bm));

    TEST_ASSERT_FALSE(mqtt_downloader_bitmap_is_processed(&bm, 5));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_set_processed(&bm, 5));
    TEST_ASSERT_TRUE(mqtt_downloader_bitmap_is_processed(&bm, 5));
    TEST_ASSERT_EQUAL_size_t(15, bm.unprocessed_block_count);

    mqtt_downloader_bitmap_deinit(&bm);
}

void test_bitmap_set_processed_last_block_in_cell(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));
    /* 9 blocks; block 8 is the only real bit in cell[1] */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(9, &bm));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_set_processed(&bm, 8));
    TEST_ASSERT_TRUE(mqtt_downloader_bitmap_is_processed(&bm, 8));
    TEST_ASSERT_EQUAL_size_t(8, bm.unprocessed_block_count);
    /* Padding bits in cell[1] must be unchanged (still 1) */
    TEST_ASSERT_EQUAL_HEX8(0xFE, bm.bitmap[1]);

    mqtt_downloader_bitmap_deinit(&bm);
}

void test_bitmap_set_processed_double_process_returns_invalid_state(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(4, &bm));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,            mqtt_downloader_bitmap_set_processed(&bm, 2));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, mqtt_downloader_bitmap_set_processed(&bm, 2));
    /* unprocessed_block_count must not go below 3 */
    TEST_ASSERT_EQUAL_size_t(3, bm.unprocessed_block_count);

    mqtt_downloader_bitmap_deinit(&bm);
}

void test_bitmap_out_of_range_block_id_returns_error(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(4, &bm));

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, mqtt_downloader_bitmap_set_processed(&bm, 4));  /* == block_count */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, mqtt_downloader_bitmap_set_processed(&bm, -1));
    TEST_ASSERT_FALSE(mqtt_downloader_bitmap_is_processed(&bm, 4));   /* out-of-range -> false */
    TEST_ASSERT_FALSE(mqtt_downloader_bitmap_is_processed(&bm, -1));
    TEST_ASSERT_EQUAL_size_t(4, bm.unprocessed_block_count); /* unchanged */

    mqtt_downloader_bitmap_deinit(&bm);
}

/* =========================================================================
 * bitmap_deinit
 * ========================================================================= */

void test_bitmap_deinit_zeros_struct(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(8, &bm));
    mqtt_downloader_bitmap_deinit(&bm);

    TEST_ASSERT_NULL(bm.bitmap);
    TEST_ASSERT_EQUAL_size_t(0, bm.block_count);
    TEST_ASSERT_EQUAL_size_t(0, bm.unprocessed_block_count);
}

/* =========================================================================
 * Popcount-with-padding-mask formula
 *
 * Reproduces the algorithm from resume_attempt() so we can verify the two
 * regression-critical cases independently:
 *   1. 9 blocks fresh (0 received) -> popcount must be 9, not 10 (7 padding bits)
 *   2. 9 blocks, 4 received -> popcount must be 5
 * ========================================================================= */

static size_t count_unprocessed(const mqtt_downloader_bitmap_t *bitmap)
{
    size_t cell_count = (bitmap->block_count + 7) / 8;
    size_t unprocessed = 0;
    for (size_t c = 0; c < cell_count; c++) {
        uint8_t cell = bitmap->bitmap[c];
        if (c == cell_count - 1 && (bitmap->block_count % 8) != 0) {
            cell &= (uint8_t)((1u << (bitmap->block_count % 8)) - 1);
        }
        unprocessed += (size_t)__builtin_popcount(cell);
    }
    return unprocessed;
}

void test_popcount_fresh_9_blocks_equals_9_not_10(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(9, &bm));

    TEST_ASSERT_EQUAL_size_t(9, count_unprocessed(&bm));

    mqtt_downloader_bitmap_deinit(&bm);
}

void test_popcount_after_4_of_9_processed_equals_5(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(9, &bm));

    for (int32_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_set_processed(&bm, i));
    }

    TEST_ASSERT_EQUAL_size_t(5, count_unprocessed(&bm));

    mqtt_downloader_bitmap_deinit(&bm);
}

void test_popcount_all_processed_equals_0(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(9, &bm));

    for (int32_t i = 0; i < 9; i++) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_set_processed(&bm, i));
    }

    TEST_ASSERT_EQUAL_size_t(0, count_unprocessed(&bm));

    mqtt_downloader_bitmap_deinit(&bm);
}

void test_popcount_exact_multiple_of_8_no_padding(void)
{
    mqtt_downloader_bitmap_t bm;
    memset(&bm, 0, sizeof(bm));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_init(8, &bm));

    TEST_ASSERT_EQUAL_size_t(8, count_unprocessed(&bm));

    for (int32_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, mqtt_downloader_bitmap_set_processed(&bm, i));
    }
    TEST_ASSERT_EQUAL_size_t(5, count_unprocessed(&bm));

    mqtt_downloader_bitmap_deinit(&bm);
}
