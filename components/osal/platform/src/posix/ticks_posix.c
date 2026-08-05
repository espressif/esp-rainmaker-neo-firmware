/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_ticks.h"

#include <time.h>

osal_tick_type_t osal_get_max_delay(void)
{
    return UINT32_MAX;
}

osal_tick_type_t osal_ticks_from_ms(uint32_t ms)
{
    return (osal_tick_type_t) ms;
}

uint32_t osal_ms_from_ticks(osal_tick_type_t ticks)
{
    return (uint32_t) ticks;
}
