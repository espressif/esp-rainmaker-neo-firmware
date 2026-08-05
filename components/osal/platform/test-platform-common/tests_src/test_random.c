/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "osal_random.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/** Pearson chi^2 vs uniform over 16 bins; `total` must be divisible by 16. */
static uint64_t chi_square_uniform16(const uint32_t bins[16], uint32_t total)
{
    const uint32_t expected = total / 16;
    uint64_t chi2 = 0;
    for (unsigned i = 0; i < 16; i++) {
        int64_t diff = (int64_t)bins[i] - (int64_t)expected;
        chi2 += (uint64_t)(diff * diff) / (uint64_t)expected;
    }
    return chi2;
}

static unsigned popcount_u32(uint32_t v)
{
    unsigned c = 0;
    for (unsigned s = 0; s < 32u; s++) {
        c += (unsigned)((v >> s) & 1u);
    }
    return c;
}

void test_random_basic(void)
{
    /* --- Consecutive repeats: fail if the same uint32_t appears `threshold` times in a row --- */
    enum { k_consecutive_same_threshold = 5 };
    enum { k_random_streak_samples = 1000 };

    uint32_t prev = osal_random_generate();
    int run = 1;
    for (int i = 1; i < k_random_streak_samples; i++) {
        uint32_t v = osal_random_generate();
        if (v == prev) {
            run++;
            TEST_ASSERT_LESS_THAN_INT(k_consecutive_same_threshold, run);
        } else {
            prev = v;
            run = 1;
        }
    }

    /* --- chi^2 on low nibble: catches strong bias / stuck patterns (df=15) --- */
    enum { k_chi_bins = 16 };
    enum { k_chi_samples = 2048 };
    uint32_t nibble_bins[k_chi_bins];
    memset(nibble_bins, 0, sizeof(nibble_bins));
    for (int i = 0; i < k_chi_samples; i++) {
        uint32_t v = osal_random_generate();
        nibble_bins[v & 0x0Fu]++;
    }
    /* Upper tail ~99.9% for chi^2(15) ~ 37.7; use 45 as loose smoke bound. */
    uint64_t chi_nibble = chi_square_uniform16(nibble_bins, k_chi_samples);
    if (chi_nibble > 45) {
        TEST_FAIL_MESSAGE("Chi-square test failed (nibble); exceeded 45");
    }

    /* --- Bit balance across many draws: mean ones per bit ~ 0.5 --- */
    enum { k_bit_balance_words = 512 };
    unsigned total_ones = 0;
    for (int i = 0; i < k_bit_balance_words; i++) {
        total_ones += popcount_u32(osal_random_generate());
    }
    const unsigned total_bits = k_bit_balance_words * 32;
    const unsigned expected_ones = total_bits / 2;
    /* Binomial stddev ~ sqrt(n/4); allow a wide band to avoid flaky CI. */
    TEST_ASSERT_UINT_WITHIN(600, expected_ones, total_ones);

    /* --- Value spread: not stuck on one value or a narrow band --- */
    enum { k_spread_samples = 256 };
    uint32_t vmin = UINT32_MAX;
    uint32_t vmax = 0;
    for (int i = 0; i < k_spread_samples; i++) {
        uint32_t v = osal_random_generate();
        if (v < vmin) {
            vmin = v;
        }
        if (v > vmax) {
            vmax = v;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(vmin, vmax);
    TEST_ASSERT_GREATER_THAN_UINT32(0xFFFFu, vmax - vmin);

    /* --- chi^2 on bytes from osal_random_fill (high nibble per byte) --- */
    enum { k_fill_len = 512 };
    unsigned char fill_buf[k_fill_len];
    memset(fill_buf, 0, sizeof(fill_buf));
    osal_random_fill(fill_buf, sizeof(fill_buf));
    uint32_t fill_bins[k_chi_bins];
    memset(fill_bins, 0, sizeof(fill_bins));
    for (size_t i = 0; i < sizeof(fill_buf); i++) {
        fill_bins[(fill_buf[i] >> 4) & 0x0Fu]++;
    }
    uint64_t chi_fill = chi_square_uniform16(fill_bins, k_fill_len);
    if (chi_fill > 45) {
        TEST_FAIL_MESSAGE("Chi-square test failed (fill); exceeded 45");
    }

    /* --- Generate a random number in a range, and ensure it is always within the range (inclusive) --- */
    enum { k_range_samples = 1000 };
    uint32_t min = 10;
    uint32_t max = 15;
    for (int i = 0; i < k_range_samples; i++) {
        uint32_t random_number = osal_random_generate_range(min, max);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(max, random_number);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(min, random_number);
    }

    /* --- Buffer not all zeros; two fills differ --- */
    unsigned char buffer[64];
    memset(buffer, 0, sizeof(buffer));

    osal_random_fill(buffer, sizeof(buffer));

    /* Check that buffer is no longer all zeros */
    bool all_zeros = true;
    for (size_t i = 0; i < sizeof(buffer); i++) {
        if (buffer[i] != 0) {
            all_zeros = false;
            break;
        }
    }
    TEST_ASSERT_FALSE(all_zeros);

    /* Test consecutive buffer fills produce different results */
    unsigned char buffer2[64];
    memset(buffer2, 0, sizeof(buffer2));

    osal_random_fill(buffer2, sizeof(buffer2));

    /* Check that buffers are different */
    bool buffers_identical = true;
    for (size_t i = 0; i < sizeof(buffer); i++) {
        if (buffer[i] != buffer2[i]) {
            buffers_identical = false;
            break;
        }
    }
    TEST_ASSERT_FALSE(buffers_identical);
}
