/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_tick_math.h
 * @brief Overflow-safe millisecond/tick arithmetic.
 *
 * ``pdMS_TO_TICKS`` casts to the 32-bit ``TickType_t`` *before* multiplying by
 * ``configTICK_RATE_HZ``, so the product wraps above 2^32 / configTICK_RATE_HZ
 * ms (71.6 min at 1 kHz, 11.9 h at 100 Hz) and the timer fires early rather
 * than failing loudly. These do the same conversion in 64-bit, and stay free of
 * FreeRTOS types so they can be unit-tested on any build.
 */

#ifndef __OSAL_TICK_MATH_H__
#define __OSAL_TICK_MATH_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** ticks = ms * (ticks/s) / (ms/s). */
#define OSAL_MS_PER_SEC 1000ULL

/**
 * @brief Convert milliseconds to ticks without overflowing.
 *
 * @param[in] ms           Delay in milliseconds.
 * @param[in] tick_rate_hz Tick rate in Hz (``configTICK_RATE_HZ``).
 *
 * @return The delay in ticks, truncated toward zero, and possibly wider than
 *         the platform's tick type. Pass it through ::osal_tick_period_clamp
 *         before handing it to a timer API.
 */
static inline uint64_t osal_ticks_from_ms_64(uint64_t ms, uint32_t tick_rate_hz)
{
    return (ms * (uint64_t) tick_rate_hz) / OSAL_MS_PER_SEC;
}

/**
 * @brief Clamp a tick count to one valid timer period.
 *
 * @param[in] total_ticks Full delay in ticks.
 * @param[in] max_ticks   Largest period a single arm may use; 0 is treated as 1.
 *
 * @return A period in ``[1, max_ticks]``. Never 0: FreeRTOS rejects that.
 */
static inline uint64_t osal_tick_period_clamp(uint64_t total_ticks, uint64_t max_ticks)
{
    if (max_ticks == 0) {
        max_ticks = 1;
    }
    if (total_ticks == 0) {
        return 1;
    }
    if (total_ticks > max_ticks) {
        return max_ticks;
    }
    return total_ticks;
}

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_TICK_MATH_H__ */
