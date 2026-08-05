/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_entry_esp.c
 * @brief ESP-IDF entry point. Runs the example once; the device then keeps
 *        running on FreeRTOS tasks, so there is no teardown path here.
 */

#include "app_entry.h"

void app_main(void)
{
    (void) app_run();
}
