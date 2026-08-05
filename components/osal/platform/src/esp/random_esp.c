/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file random_esp.c
 * @brief ESP-IDF RNG implementation.
 */

/* Includes *************************************************************/

/* Declarations includes. */
#include "osal_random.h"

/* ESP-IDF includes. */
#include "esp_random.h"

/* Public function definitions *************************************************************/

uint32_t osal_random_generate(void)
{
    return esp_random();
}

void osal_random_fill(void *buffer, size_t size)
{
    esp_fill_random(buffer, size);
}
