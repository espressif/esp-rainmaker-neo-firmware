/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_tick_math.c
 * @brief Tests for the overflow-safe ms/tick arithmetic in osal_tick_math.h.
 *
 * Regression: pdMS_TO_TICKS() casts to the 32-bit TickType_t before multiplying
 * by configTICK_RATE_HZ. A daily schedule (86400 s) armed through it fired after
 * 500.65 s instead, and again on every re-arm.
 */

#include "unity.h"
#include <stdint.h>
#include "osal_tick_math.h"

/* portMAX_DELAY / 2 for a 32-bit TickType_t, hardcoded so these hold on POSIX
 * builds where portMAX_DELAY is not defined. */
#define TEST_MAX_TICKS ((uint64_t)(0xFFFFFFFFULL / 2))

#define TEST_DAY_MS 86400000ULL

/* Unity's 64-bit assertions need UNITY_SUPPORT_64, which ESP-IDF gates behind
 * CONFIG_UNITY_ENABLE_64BIT (default n); with it off they always fail rather
 * than fail to compile. Compare halves instead. */
#define TEST_ASSERT_EQUAL_TICKS(expected, actual)                                   \
    do {                                                                            \
        uint64_t __exp = (uint64_t)(expected);                                       \
        uint64_t __act = (uint64_t)(actual);                                         \
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(__exp >> 32), (uint32_t)(__act >> 32));  \
        TEST_ASSERT_EQUAL_UINT32((uint32_t)__exp, (uint32_t)__act);                  \
    } while (0)

void test_tick_math_conversion(void)
{
    /* Exact at both tick rates the SDK ships with. */
    TEST_ASSERT_EQUAL_TICKS(0, osal_ticks_from_ms_64(0, 1000));
    TEST_ASSERT_EQUAL_TICKS(1000, osal_ticks_from_ms_64(1000, 1000));
    TEST_ASSERT_EQUAL_TICKS(100, osal_ticks_from_ms_64(1000, 100));

    /* Sub-tick truncates to 0; callers clamp it up to one period. */
    TEST_ASSERT_EQUAL_TICKS(0, osal_ticks_from_ms_64(9, 100));

    /* The regression: one day, where pdMS_TO_TICKS returned 500.65 s worth. */
    TEST_ASSERT_EQUAL_TICKS(86400000ULL, osal_ticks_from_ms_64(TEST_DAY_MS, 1000));
    TEST_ASSERT_EQUAL_TICKS(8640000ULL, osal_ticks_from_ms_64(TEST_DAY_MS, 100));

    /* Straddle ms * tick_rate_hz == 2^32, where pdMS_TO_TICKS starts wrapping. */
    TEST_ASSERT_EQUAL_TICKS(4294967ULL, osal_ticks_from_ms_64(4294967ULL, 1000));
    TEST_ASSERT_EQUAL_TICKS(4294968ULL, osal_ticks_from_ms_64(4294968ULL, 1000));
    TEST_ASSERT_EQUAL_TICKS(4294968ULL, osal_ticks_from_ms_64(42949680ULL, 100));

    /* A year stays monotonic rather than wrapping. */
    const uint64_t year_ms = 365ULL * TEST_DAY_MS;
    TEST_ASSERT_EQUAL_TICKS(year_ms, osal_ticks_from_ms_64(year_ms, 1000));

    /* A zero tick rate is a caller bug; degrade to 0, never trap on the divide. */
    TEST_ASSERT_EQUAL_TICKS(0, osal_ticks_from_ms_64(1000, 0));
}

void test_tick_math_period_clamp(void)
{
    /* Never 0: FreeRTOS rejects a zero period. */
    TEST_ASSERT_EQUAL_TICKS(1, osal_tick_period_clamp(0, TEST_MAX_TICKS));
    TEST_ASSERT_EQUAL_TICKS(1, osal_tick_period_clamp(0, 0));
    TEST_ASSERT_EQUAL_TICKS(1, osal_tick_period_clamp(5, 0));

    /* Below the cap, unchanged - a one-day delay needs no split. */
    TEST_ASSERT_EQUAL_TICKS(1, osal_tick_period_clamp(1, TEST_MAX_TICKS));
    TEST_ASSERT_EQUAL_TICKS(86400000ULL, osal_tick_period_clamp(86400000ULL, TEST_MAX_TICKS));
    TEST_ASSERT_EQUAL_TICKS(TEST_MAX_TICKS, osal_tick_period_clamp(TEST_MAX_TICKS, TEST_MAX_TICKS));

    /* Above it, saturates rather than wrapping into a short period. */
    TEST_ASSERT_EQUAL_TICKS(TEST_MAX_TICKS, osal_tick_period_clamp(TEST_MAX_TICKS + 1, TEST_MAX_TICKS));
    TEST_ASSERT_EQUAL_TICKS(TEST_MAX_TICKS, osal_tick_period_clamp(UINT64_MAX, TEST_MAX_TICKS));

    /* A year at 1 kHz is over the cap, so it must split. The chain that walks
     * those segments is covered in test_timer_chain.c. */
    TEST_ASSERT_TRUE(osal_ticks_from_ms_64(365ULL * TEST_DAY_MS, 1000) > TEST_MAX_TICKS);
}
