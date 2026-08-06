/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "osal_ticks.h"

void test_ticks_basic(void)
{
    /* Ensure fair comparison*/
    uint32_t inc = osal_ms_from_ticks(1);
    for (uint32_t ms = 0; ms < 10 * inc; ms += inc) {
        osal_tick_type_t t = osal_ticks_from_ms(ms);
        uint32_t back = osal_ms_from_ticks(t);
        TEST_ASSERT_EQUAL_UINT32(ms, back);
    }
}
