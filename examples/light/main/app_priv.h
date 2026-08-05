/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_priv.h
 * @brief Private interface for the light example.
 */

#pragma once

#include <stdbool.h>
#include "osal_err.h"
#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_params.h"

/* Default values: green light on at 50% brightness, HSV mode. */
#define DEFAULT_POWER       true
#define DEFAULT_HUE         120
#define DEFAULT_SATURATION  100
#define DEFAULT_BRIGHTNESS  50
#define DEFAULT_CCT         0
#define DEFAULT_LIGHT_MODE  ESP_RMAKER_LIGHT_MODE_HSV

extern esp_rmaker_device_t *light_device;

void app_driver_init(void);

osal_err_t app_driver_set_power(bool power);
osal_err_t app_driver_set_hue(int hue);
osal_err_t app_driver_set_saturation(int saturation);
osal_err_t app_driver_set_brightness(int brightness);
osal_err_t app_driver_set_cct(int cct);
osal_err_t app_driver_set_light_mode(esp_rmaker_light_mode_t light_mode);

bool app_driver_get_power(void);
int app_driver_get_hue(void);
int app_driver_get_saturation(void);
int app_driver_get_brightness(void);
int app_driver_get_cct(void);
esp_rmaker_light_mode_t app_driver_get_light_mode(void);
