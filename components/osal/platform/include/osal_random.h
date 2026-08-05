/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_random.h
 * @brief Platform common RNG (Random Number Generator) header file.
 */

#ifndef __OSAL_RANDOM_H__
#define __OSAL_RANDOM_H__

/* Includes *************************************************************/

/* Standard includes. */
#include <stdint.h>
#include <stddef.h>

/* Public function declarations *************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate a 32-bit random number.
 * @return The random number.
 */
uint32_t osal_random_generate(void);

/**
 * @brief Generate a 32-bit random number in a range, inclusive of both min and max.
 * @param[in] min The minimum value.
 * @param[in] max The maximum value.
 * @return The random number.
 */
uint32_t osal_random_generate_range(uint32_t min, uint32_t max);

/**
 * @brief Fill a buffer with random data.
 * @param[out] buffer The buffer to fill.
 * @param[in] size The size of the buffer.
 */
void osal_random_fill(void *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_RANDOM_H__ */
