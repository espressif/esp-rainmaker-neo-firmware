/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_standard_params.c
 * @brief Standard parameters for RainMaker Neo.
 */

/* Includes *******************************************************/

/* Standard includes */
#include <stdint.h>

/* RMNG includes */
#include "esp_rmaker_standard_types.h"
#include "esp_rmaker_standard_params.h"
#include "esp_rmaker_val.h"

/* Public function declarations *******************************************************/

esp_rmaker_param_t *esp_rmaker_name_param_create(const char *param_id, const char *val)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_NAME,
                                esp_rmaker_str(val), PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
    return param;
}

esp_rmaker_param_t *esp_rmaker_power_param_create(const char *param_id, bool val)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_POWER,
                                esp_rmaker_bool(val), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (param) {
        esp_rmaker_param_add_ui_type(param, ESP_RMAKER_UI_TOGGLE);
    }
    return param;
}

esp_rmaker_param_t *esp_rmaker_brightness_param_create(const char *param_id, int val)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_BRIGHTNESS,
                                esp_rmaker_int(val), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (param) {
        esp_rmaker_param_add_ui_type(param, ESP_RMAKER_UI_SLIDER);
        esp_rmaker_param_add_bounds(param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1));
    }
    return param;
}

esp_rmaker_param_t *esp_rmaker_hue_param_create(const char *param_id, int val)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_HUE,
                                esp_rmaker_int(val), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (param) {
        esp_rmaker_param_add_ui_type(param, ESP_RMAKER_UI_HUE_SLIDER);
        esp_rmaker_param_add_bounds(param, esp_rmaker_int(0), esp_rmaker_int(360), esp_rmaker_int(1));
    }
    return param;
}

esp_rmaker_param_t *esp_rmaker_saturation_param_create(const char *param_id, int val)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_SATURATION,
                                esp_rmaker_int(val), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (param) {
        esp_rmaker_param_add_ui_type(param, ESP_RMAKER_UI_SLIDER);
        esp_rmaker_param_add_bounds(param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1));
    }
    return param;
}

esp_rmaker_param_t *esp_rmaker_intensity_param_create(const char *param_id, int val)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_INTENSITY,
                                esp_rmaker_int(val), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (param) {
        esp_rmaker_param_add_ui_type(param, ESP_RMAKER_UI_SLIDER);
        esp_rmaker_param_add_bounds(param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1));
    }
    return param;
}

esp_rmaker_param_t *esp_rmaker_cct_param_create(const char *param_id, int val)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_CCT,
                                esp_rmaker_int(val), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (param) {
        esp_rmaker_param_add_ui_type(param, ESP_RMAKER_UI_SLIDER);
        esp_rmaker_param_add_bounds(param, esp_rmaker_int(2700), esp_rmaker_int(6500), esp_rmaker_int(100));
    }
    return param;
}

esp_rmaker_param_t *esp_rmaker_light_mode_param_create(const char *param_id, esp_rmaker_light_mode_t val, bool ui_hidden)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_LIGHT_MODE,
                                esp_rmaker_int((int)val), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (param) {
        esp_rmaker_param_add_ui_type(param, ui_hidden ? ESP_RMAKER_UI_HIDDEN : ESP_RMAKER_UI_DROPDOWN);
        esp_rmaker_param_add_bounds(param, esp_rmaker_int(ESP_RMAKER_LIGHT_MODE_INVALID + 1), esp_rmaker_int(ESP_RMAKER_LIGHT_MODE_MAX - 1), esp_rmaker_int(1));
    }
    return param;
}

esp_rmaker_param_t *esp_rmaker_direction_param_create(const char *param_id, int val)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_DIRECTION,
                                esp_rmaker_int(val), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (param) {
        esp_rmaker_param_add_ui_type(param, ESP_RMAKER_UI_DROPDOWN);
        esp_rmaker_param_add_bounds(param, esp_rmaker_int(0), esp_rmaker_int(1), esp_rmaker_int(1));
    }
    return param;
}

esp_rmaker_param_t *esp_rmaker_speed_param_create(const char *param_id, int val)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_SPEED,
                                esp_rmaker_int(val), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (param) {
        esp_rmaker_param_add_ui_type(param, ESP_RMAKER_UI_SLIDER);
        esp_rmaker_param_add_bounds(param, esp_rmaker_int(0), esp_rmaker_int(5), esp_rmaker_int(1));
    }
    return param;
}

esp_rmaker_param_t *esp_rmaker_temperature_param_create(const char *param_id, float val)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_TEMPERATURE,
                                esp_rmaker_float(val), PROP_FLAG_READ | PROP_FLAG_TIME_SERIES);
    if (param) {
        esp_rmaker_param_add_ui_type(param, ESP_RMAKER_UI_TEXT);
    }
    return param;
}
