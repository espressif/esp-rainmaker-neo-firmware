/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_led_posix.c
 * @brief POSIX stub for the app_led interface. There is no physical LED on the
 *        host, so every call is a successful no-op. This lets the portable
 *        example call the LED API unconditionally.
 */

/* Declarations */
#include "app_led.h"

osal_err_t app_led_init(const app_led_state_t *p_state)
{
    (void) p_state;
    return OSAL_ERR_OK;
}

osal_err_t app_led_apply(const app_led_state_t *p_state)
{
    (void) p_state;
    return OSAL_ERR_OK;
}

osal_err_t app_led_set_power(bool power)
{
    (void) power;
    return OSAL_ERR_OK;
}

osal_err_t app_led_set_hue(uint16_t hue)
{
    (void) hue;
    return OSAL_ERR_OK;
}

osal_err_t app_led_set_saturation(uint8_t saturation)
{
    (void) saturation;
    return OSAL_ERR_OK;
}

osal_err_t app_led_set_brightness(uint8_t brightness)
{
    (void) brightness;
    return OSAL_ERR_OK;
}

osal_err_t app_led_set_cct(uint16_t cct)
{
    (void) cct;
    return OSAL_ERR_OK;
}

osal_err_t app_led_set_mode(app_led_mode_t mode)
{
    (void) mode;
    return OSAL_ERR_OK;
}
