/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_priv.h
 * @brief Private interface for the fan example.
 */

#pragma once

#include <stdbool.h>
#include "osal_err.h"
#include "esp_rmaker_core.h"

/* Default values: fan off, no swing, lowest speed. */
#define DEFAULT_POWER   false
#define DEFAULT_SWING   false
#define DEFAULT_SPEED   1

/* Speed range exposed by the speed parameter. */
#define FAN_SPEED_MIN   1
#define FAN_SPEED_MAX   5

extern esp_rmaker_device_t *fan_device;

void app_driver_init(void);

osal_err_t app_driver_set_power(bool power);
osal_err_t app_driver_set_swing(bool swing);
osal_err_t app_driver_set_speed(int speed);

bool app_driver_get_power(void);
bool app_driver_get_swing(void);
int  app_driver_get_speed(void);
