/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_led_internal.h
 * @brief Internal app_led variables and functions.
 */

#ifndef __APP_LED_INTERNAL_H__
#define __APP_LED_INTERNAL_H__

/* Includes *******************************************************/

/* App includes */
#include "app_led.h"

/* Types *******************************************************/

/* RGB color */
typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} app_led_color_rgb_t;

/* Public function declarations ****************************************************/

/**
 * @brief Initialize the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_internal_init(void);

/**
 * @brief Set the color of the LED in RGB format.
 * Implemented by LEDC or WS2812 implementation.
 *
 * @param[in] color_rgb The color of the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_internal_set_color_rgb(app_led_color_rgb_t color_rgb);

#endif /* __APP_LED_INTERNAL_H__ */
