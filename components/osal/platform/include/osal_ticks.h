/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ticks.h
 * @brief Tick type and conversions between ticks and milliseconds.
 */

#ifndef __OSAL_TICKS_H__
#define __OSAL_TICKS_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t osal_tick_type_t;

#define OSAL_MAX_DELAY (osal_get_max_delay())

/**
 * @brief Get the maximum delay.
 *
 * @return The maximum delay.
 */
osal_tick_type_t osal_get_max_delay( void );

/**
 * @brief Convert milliseconds to ticks.
 *
 * @param[in] ms The number of milliseconds to convert.
 *
 * @return The number of ticks.
 */
osal_tick_type_t osal_ticks_from_ms(uint32_t ms);

/**
 * @brief Convert ticks to milliseconds.
 *
 * @param[in] ticks The number of ticks to convert.
 *
 * @return The number of milliseconds.
 */
uint32_t osal_ms_from_ticks(osal_tick_type_t ticks);

#ifdef __cplusplus
}
#endif

#endif
