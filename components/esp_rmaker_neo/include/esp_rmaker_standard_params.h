/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_standard_params.h
 * @brief Standard parameters for RainMaker Neo.
 */

#ifndef __ESP_RMAKER_STANDARD_PARAMS_H__
#define __ESP_RMAKER_STANDARD_PARAMS_H__

#include <stdbool.h>
#include "esp_rmaker_data_model.h"

/**
 * @name Suggested default parameter names
 *
 * These will also be used by default if you use any standard device helper APIs.
 *
 * @note These names are not mandatory. You can use the ESP RainMaker Core APIs
 * to create your own parameters with custom names, if required.
 * @{
 */

#define ESP_RMAKER_DEF_NAME_PARAM_ID      "Name"
#define ESP_RMAKER_DEF_POWER_ID           "Power"
#define ESP_RMAKER_DEF_BRIGHTNESS_ID      "Brightness"
#define ESP_RMAKER_DEF_HUE_ID             "Hue"
#define ESP_RMAKER_DEF_SATURATION_ID      "Saturation"
#define ESP_RMAKER_DEF_INTENSITY_ID       "Intensity"
#define ESP_RMAKER_DEF_CCT_ID             "CCT"
#define ESP_RMAKER_DEF_LIGHT_MODE_ID      "Light Mode"
#define ESP_RMAKER_DEF_DIRECTION_ID       "Direction"
#define ESP_RMAKER_DEF_SPEED_ID           "Speed"
#define ESP_RMAKER_DEF_TEMPERATURE_ID     "Temperature"

/** @} */

/**
 * @brief Light mode enumeration
 *
 * @note These values are used to set the default light mode.
 */
typedef enum {
    ESP_RMAKER_LIGHT_MODE_INVALID = 0, //!< Invalid mode
    ESP_RMAKER_LIGHT_MODE_HSV = 1,     //!< HSV mode
    ESP_RMAKER_LIGHT_MODE_CCT = 2,     //!< CCT mode
    ESP_RMAKER_LIGHT_MODE_MAX,         //!< Used for bounds calculation
} esp_rmaker_light_mode_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create standard name param
 *
 * This will create the standard name parameter.
 * This should be added to all devices for which you want a user customisable name.
 * The value should be same as the device id.
 *
 * All standard device creation APIs will add this internally.
 * No application registered callback will be called for this parameter,
 * and changes will be managed internally, unless a custom bulk write callback is registered.
 *
 * @param[in] param_id Id of the parameter
 * @param[in] val The device name
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_name_param_create(const char *param_id, const char *val);

/**
 * @brief Create standard Power param
 *
 * This will create the standard power parameter.
 *
 * @param[in] param_id Id of the parameter
 * @param[in] val Default Value of the parameter
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_power_param_create(const char *param_id, bool val);

/**
 * @brief Create standard Brightness param
 *
 * This will create the standard brightness parameter.
 *
 * @param[in] param_id Id of the parameter
 * @param[in] val Default Value of the parameter
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_brightness_param_create(const char *param_id, int val);

/**
 * @brief Create standard Hue param
 *
 * This will create the standard hue parameter.
 *
 * @param[in] param_id Id of the parameter
 * @param[in] val Default Value of the parameter
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_hue_param_create(const char *param_id, int val);

/**
 * @brief Create standard Saturation param
 *
 * This will create the standard saturation parameter.
 *
 * @param[in] param_id Id of the parameter
 * @param[in] val Default Value of the parameter
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_saturation_param_create(const char *param_id, int val);

/**
 * @brief Create standard Intensity param
 *
 * This will create the standard intensity parameter.
 *
 * @param[in] param_id Id of the parameter
 * @param[in] val Default Value of the parameter
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_intensity_param_create(const char *param_id, int val);

/**
 * @brief Create standard CCT param
 *
 * This will create the standard cct parameter.
 *
 * @param[in] param_id Id of the parameter
 * @param[in] val Default Value of the parameter
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_cct_param_create(const char *param_id, int val);

/**
 * @brief Create standard Light Mode param
 *
 * This will create the standard light mode parameter. The parameter is bounded to the
 * currently supported light modes:
 * - 1: ::ESP_RMAKER_LIGHT_MODE_HSV
 * - 2: ::ESP_RMAKER_LIGHT_MODE_CCT
 *
 * @param[in] param_id Id of the parameter
 * @param[in] val Default Light Mode of the parameter
 * @param[in] ui_hidden Whether to hide the UI for this parameter
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_light_mode_param_create(const char *param_id, esp_rmaker_light_mode_t val, bool ui_hidden);

/**
 * @brief Create standard Direction param
 *
 * This will create the standard direction parameter.
 *
 * @param[in] param_id Id of the parameter
 * @param[in] val Default Value of the parameter
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_direction_param_create(const char *param_id, int val);

/**
 * @brief Create standard Speed param
 *
 * This will create the standard speed parameter.
 *
 * @param[in] param_id Id of the parameter
 * @param[in] val Default Value of the parameter
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_speed_param_create(const char *param_id, int val);

/**
 * @brief Create standard Temperature param
 *
 * This will create the standard temperature parameter.
 *
 * @param[in] param_id Id of the parameter
 * @param[in] val Default Value of the parameter
 *
 * @return Parameter handle on success.
 * @return NULL in case of failures.
 */
esp_rmaker_param_t *esp_rmaker_temperature_param_create(const char *param_id, float val);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_STANDARD_PARAMS_H__ */
