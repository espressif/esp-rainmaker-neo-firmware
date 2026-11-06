/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "osal_ticks.h"
#include "freertos/FreeRTOS.h"

/** @brief An osal tick count as a FreeRTOS one; OSAL_MAX_DELAY stays "wait forever" where TickType_t is 64-bit. */
static inline TickType_t osal_ticks_to_rtos(osal_tick_type_t ticks)
{
    return ticks == OSAL_MAX_DELAY ? portMAX_DELAY : (TickType_t) ticks;
}
