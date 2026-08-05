/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file host_ctrl_esp.c
 * @brief ESP implementation of the host control.
 */

#include "esp_rmaker_host_ctrl.h"
#include "sysinfo.h"
#include <sdkconfig.h>
#include "esp_sleep.h"

char *esp_rmaker_host_ctrl_get_target_type(void)
{
#ifdef CONFIG_IDF_TARGET
    return CONFIG_IDF_TARGET;
#else
    return "esp-unknown";
#endif
}

void esp_rmaker_host_ctrl_kill(void)
{
    // Put the ESP into permanent deep sleep
    esp_deep_sleep_start();
}
