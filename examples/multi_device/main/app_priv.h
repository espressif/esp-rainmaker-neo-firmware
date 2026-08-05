/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_priv.h
 * @brief Private interface for the multi-device example.
 */

#pragma once

#include <stdbool.h>
#include "osal_err.h"
#include "esp_rmaker_core.h"

/* Default values. All devices start ON so the focus LED shows something. */
#define DEFAULT_LIGHT_POWER       true
#define DEFAULT_LIGHT_BRIGHTNESS  50

#define DEFAULT_FAN_POWER         true
#define DEFAULT_FAN_SWING         false
#define DEFAULT_FAN_SPEED         1

#define DEFAULT_SWITCH_POWER      true

/* Speed range exposed by the fan speed parameter. */
#define FAN_SPEED_MIN   1
#define FAN_SPEED_MAX   5

/* Device handles, created in app_main and consulted by the driver callbacks. */
extern esp_rmaker_device_t *light_device;
extern esp_rmaker_device_t *fan_device;
extern esp_rmaker_device_t *switch_device;
extern esp_rmaker_device_t *temp_sensor_device;

/* The temperature sensor's only parameter is read-only, so the driver reports
 * it by handle instead of looking it up by type on every simulated reading. */
extern esp_rmaker_param_t *temp_sensor_temperature_param;

/* Which device the shared LED + button currently control. */
typedef enum {
    DEVICE_FOCUS_LIGHT = 0,
    DEVICE_FOCUS_FAN,
    DEVICE_FOCUS_SWITCH,
    DEVICE_FOCUS_TEMP_SENSOR,
    DEVICE_FOCUS_COUNT,
} device_focus_t;

void app_driver_init(void);

/* Light (power + brightness only). */
osal_err_t app_light_set_power(bool power);
osal_err_t app_light_set_brightness(int brightness);
bool app_light_get_power(void);
int  app_light_get_brightness(void);

/* Fan (power + swing + speed). */
osal_err_t app_fan_set_power(bool power);
osal_err_t app_fan_set_swing(bool swing);
osal_err_t app_fan_set_speed(int speed);
bool app_fan_get_power(void);
bool app_fan_get_swing(void);
int  app_fan_get_speed(void);

/* Switch (power). */
osal_err_t app_switch_set_power(bool power);
bool app_switch_get_power(void);

/* Temperature sensor (read-only, simulated). */
float app_temp_sensor_get_temperature(void);
