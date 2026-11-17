/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_priv.h
 * @brief Private interface for the Matter-mirror switch example.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "osal_err.h"
#include "esp_rmaker_core.h"
#include "esp_rmaker_matter_mirror.h"
#include "esp_rmaker_standard_params.h"

/* Defaults: off, at half dim level. */
#define DEFAULT_POWER  false
#define DEFAULT_DIM    50

/* What a long button press adds to the dim level, wrapping at 100. */
#define DIM_STEP       25

extern esp_rmaker_device_t *switch_device;

void app_driver_init(void);
void app_driver_mark_ready(void);

osal_err_t app_driver_set_power(bool power);
osal_err_t app_driver_set_dim(int dim);
bool app_driver_get_power(void);
int app_driver_get_dim(void);

/**
 * @brief Identify the switch: play @p effect_id for @p seconds, or stop when @p seconds is 0.
 *
 * @param[in] effect_id What to render, ESP_RMAKER_MATTER_MIRROR_IDENTIFY_PLAIN for a plain
 *            Identify (and for the identify param, which names no effect).
 * @param[in] seconds How long to identify for, 0 to stop.
 *
 * @return OSAL_ERR_OK when the switch rendered it, OSAL_ERR_NOT_SUPPORTED for an unknown effect.
 */
osal_err_t app_driver_identify(esp_rmaker_matter_mirror_identify_effect_t effect_id, int32_t seconds);
