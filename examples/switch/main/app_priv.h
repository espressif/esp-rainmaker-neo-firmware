/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_priv.h
 * @brief Private interface for the switch example.
 */

#pragma once

#include <stdbool.h>
#include "osal_err.h"
#include "esp_rmaker_core.h"

#define DEFAULT_POWER  false

extern esp_rmaker_device_t *switch_device;

void app_driver_init(void);
osal_err_t app_driver_set_state(bool state);
bool app_driver_get_state(void);
