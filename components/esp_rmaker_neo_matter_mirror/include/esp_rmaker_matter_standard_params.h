/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_matter_standard_params.h
 * @brief Standard RainMaker Neo params a Matter device type mandates.
 *
 * Vocabulary the common data model gained because Matter requires it of every device type that
 * carries the cluster.
 */

#ifndef __ESP_RMAKER_MATTER_STANDARD_PARAMS_H__
#define __ESP_RMAKER_MATTER_STANDARD_PARAMS_H__

#include "esp_rmaker_data_model.h"

/** Identify: seconds to make the device visibly findable for. Maps to the Identify cluster. */
#define ESP_RMAKER_PARAM_IDENTIFY "esp.param.identify"

/** Suggested default id for the Identify param */
#define ESP_RMAKER_DEF_IDENTIFY_ID "Identify"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create standard Identify param
 *
 * Required on devices whose Matter device type mandates the Identify cluster (every light and
 * plug). Write-only, in seconds: a write of N asks the device to identify itself for N seconds,
 * a write of 0 asks it to stop. There is no state to read back, so the param is never reported.
 *
 * The application renders the request in its write callback. A device that can render the
 * Identify effects (blink vs breathe, which this param cannot express) should also register
 * esp_rmaker_matter_mirror_register_identify_handler().
 *
 * @param[in] param_id Id of the parameter
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_identify_param_create(const char *param_id);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_MATTER_STANDARD_PARAMS_H__ */
