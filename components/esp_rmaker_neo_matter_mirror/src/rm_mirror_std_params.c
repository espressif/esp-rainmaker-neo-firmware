/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_std_params.c
 * @brief Standard RainMaker Neo params a Matter device type mandates.
 */

#include "esp_rmaker_matter_standard_params.h"

esp_rmaker_param_t *esp_rmaker_identify_param_create(const char *param_id)
{
    /* Write-only: a duration to act on, not state. Nothing to read back, nothing to report. */
    return esp_rmaker_param_create(param_id, ESP_RMAKER_PARAM_IDENTIFY, esp_rmaker_int(0), PROP_FLAG_WRITE);
}
