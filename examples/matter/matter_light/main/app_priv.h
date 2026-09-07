/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_priv.h
 * @brief Private interface for the Matter-mirror light example.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "osal_err.h"
#include "esp_rmaker_core.h"
#include "esp_rmaker_matter_mirror.h"
#include "esp_rmaker_standard_params.h"

/* Default values: warm white on at 50% brightness (CCT mode). */
#define DEFAULT_POWER       true
#define DEFAULT_BRIGHTNESS  50
#define DEFAULT_CCT         2700
#define DEFAULT_HUE         180
#define DEFAULT_SATURATION  100
#define DEFAULT_LIGHT_MODE  ESP_RMAKER_LIGHT_MODE_CCT

extern esp_rmaker_device_t *light_device;

void app_driver_init(void);
void app_driver_mark_ready(void);

osal_err_t app_driver_set_power(bool power);
osal_err_t app_driver_set_brightness(int brightness);
osal_err_t app_driver_set_cct(int cct);
osal_err_t app_driver_set_hue(int hue);
osal_err_t app_driver_set_saturation(int saturation);
osal_err_t app_driver_set_light_mode(esp_rmaker_light_mode_t light_mode);

/**
 * @brief Identify the light: play @p effect_id for @p seconds, or stop when @p seconds is 0.
 *
 * @param[in] effect_id What to render, ESP_RMAKER_MATTER_MIRROR_IDENTIFY_PLAIN for a plain
 *            Identify (and for the identify param, which names no effect).
 * @param[in] seconds How long to identify for, 0 to stop.
 *
 * @return OSAL_ERR_OK when the light rendered it, OSAL_ERR_NOT_SUPPORTED for an unknown effect.
 */
osal_err_t app_driver_identify(esp_rmaker_matter_mirror_identify_effect_t effect_id, int32_t seconds);

bool app_driver_get_power(void);
int app_driver_get_brightness(void);
int app_driver_get_cct(void);
int app_driver_get_hue(void);
int app_driver_get_saturation(void);
esp_rmaker_light_mode_t app_driver_get_light_mode(void);
