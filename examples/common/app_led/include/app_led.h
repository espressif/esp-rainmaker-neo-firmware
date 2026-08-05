/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_led.h
 * @brief Control the LED present on most ESP development boards.
 */

#ifndef __APP_LED_H__
#define __APP_LED_H__

/* Standard includes */
#include <stdbool.h>
#include <stdint.h>

/* ESP-IDF includes */
#include "osal_err.h"

/* Types ****************************************************************/

/* HSV color */
typedef struct {
    uint16_t hue; // 0-360
    uint8_t saturation; // 0-100
} app_led_color_hs_t;

/* Light mode */
typedef enum {
    APP_LED_MODE_INVALID = 0, // Invalid mode
    APP_LED_MODE_HSV = 1,     // HSV mode
    APP_LED_MODE_CCT = 2,     // CCT mode
    APP_LED_MODE_MAX,         // Used for bounds calculation
} app_led_mode_t;

/* LED state */
typedef struct {
    bool power;
    uint8_t brightness; // 0-100
    app_led_color_hs_t color_hs;
    uint16_t cct; // 2700-6500
    app_led_mode_t mode;
} app_led_state_t;

/* Public function declarations ****************************************************/

/**
 * @brief Initialize the LED.
 *
 * @param[in] p_state Pointer to the initial state of the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_init(const app_led_state_t *p_state);

/**
 * @brief Apply the LED state.
 *
 * @param[in] p_state Pointer to the state of the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_apply(const app_led_state_t *p_state);

/**
 * @brief Set the power of the LED.
 *
 * @param[in] power True to turn on the LED, false to turn off the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_set_power(bool power);

/**
 * @brief Set the hue of the LED.
 *
 * @param[in] hue The hue of the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_set_hue(uint16_t hue);

/**
 * @brief Set the saturation of the LED.
 *
 * @param[in] saturation The saturation of the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_set_saturation(uint8_t saturation);

/**
 * @brief Set the brightness of the LED.
 *
 * @param[in] brightness The brightness of the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_set_brightness(uint8_t brightness);

/**
 * @brief Set the color temperature of the LED.
 *
 * @param[in] cct The color temperature of the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_set_cct(uint16_t cct);

/**
 * @brief Set the light mode of the LED.
 *
 * @param[in] mode The light mode of the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_set_mode(app_led_mode_t mode);

#endif /* __APP_LED_H__ */
