/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file random_common.c
 * @brief Common random number generator implementation.
 */

/* Includes *************************************************************/

/* Declarations */
#include "osal_random.h"

/* Public function definitions *************************************************************/

uint32_t osal_random_generate_range(uint32_t min, uint32_t max)
{
    return min + osal_random_generate() % (max - min + 1);
}
