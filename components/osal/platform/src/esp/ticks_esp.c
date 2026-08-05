/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_ticks.h"
#include "freertos/FreeRTOS.h"

osal_tick_type_t osal_get_max_delay(void)
{
    return portMAX_DELAY;
}

osal_tick_type_t osal_ticks_from_ms(uint32_t ms)
{
    return pdMS_TO_TICKS(ms);
}

uint32_t osal_ms_from_ticks(osal_tick_type_t ticks)
{
    return pdTICKS_TO_MS(ticks);
}
