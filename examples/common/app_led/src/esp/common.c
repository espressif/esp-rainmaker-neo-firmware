/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file common.c
 * @brief Common functions for the app_led.
 */

/* Includes ****************************************************************/

#include "app_led_internal.h"

/* Standard includes */
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* Platform includes */
#include "osal_semaphore.h"
#include "osal_ticks.h"

/* Variables ****************************************************************/

static app_led_state_t __led_state = {
    .power = false,
    .color_hs = {
        .hue = 0,
        .saturation = 0,
    },
    .brightness = 0,
    .cct = 0,
    .mode = APP_LED_MODE_HSV,
};

/**
 * @brief Serialises access to __led_state and to the underlying LED hardware.
 *
 * The setters below may be called from several tasks at once (e.g. an animation
 * task pushing frames while a cloud/local write callback toggles power). The
 * hardware backends are not re-entrant: e.g., a WS2812 refresh enables the RMT
 * channel, transmits and disables it again, so a concurrent refresh fails with
 * "channel not in init state". Every setter therefore takes this mutex for the
 * whole state-change + hardware-update sequence.
 */
static osal_semaphore_handle_t __led_lock = NULL;

/* Types ****************************************************************/

/**
 * @brief Identifies the single field a setter wants to change.
 *
 * Lets every app_led_set_*() share one lock / update / revert sequence
 * (__internal_set_field()) instead of open-coding it per setter.
 */
typedef enum {
    LED_FIELD_POWER,
    LED_FIELD_HUE,
    LED_FIELD_SATURATION,
    LED_FIELD_BRIGHTNESS,
    LED_FIELD_CCT,
    LED_FIELD_MODE,
} led_field_t;

/* Private function declarations ****************************************************/
/**
 * @brief Convert HS to RGB, with V at 100.
 *
 * @param[in] color_hs The color in HS format.
 * @param[out] color_rgb Pointer to the color in RGB format.
 */
static void __internal_hsv_to_rgb(app_led_color_hs_t color_hs, app_led_color_rgb_t *color_rgb);

/**
 * @brief Convert CCT to RGB.
 *
 * @param[in] cct The color temperature in Kelvin.
 * @param[out] color_rgb Pointer to the color in RGB format.
 */
static void __internal_cct_to_rgb(uint16_t cct, app_led_color_rgb_t *color_rgb);

/**
 * @brief Scale the brightness of the color.
 *
 * @param[in] color_rgb Pointer to the color in RGB format.
 * @param[in] brightness The brightness to scale the color to.
 */
static void __internal_scale_rgb_brightness(app_led_color_rgb_t *color_rgb, uint8_t brightness);

/**
 * @brief Update the hardware LED based on the current state.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
static osal_err_t __internal_update_hardware_led(void);

/* Private function definitions ****************************************************/

/**
 * @brief Take the LED lock, failing if the component has not been initialised.
 *
 * @return OSAL_ERR_OK on success, OSAL_ERR_INVALID_STATE if app_led_init() has not run.
 */
static osal_err_t __internal_lock(void)
{
    if (__led_lock == NULL) {
        return OSAL_ERR_INVALID_STATE;
    }
    if (osal_semaphore_take(__led_lock, OSAL_MAX_DELAY) != OSAL_ERR_OK) {
        return OSAL_ERR_TIMEOUT;
    }
    return OSAL_ERR_OK;
}

/**
 * @brief Release the LED lock.
 */
static void __internal_unlock(void)
{
    osal_semaphore_give(__led_lock);
}

static void __internal_hsv_to_rgb(app_led_color_hs_t color_hs, app_led_color_rgb_t *color_rgb)
{
    uint16_t h = color_hs.hue % 360;
    uint8_t s = color_hs.saturation;
    uint8_t v = 100;

    uint16_t rgb_max = v * 2.55f;
    uint16_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint16_t i = h / 60;
    uint16_t diff = h % 60;

    // RGB adjustment amount by hue
    uint16_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
    case 0:
        color_rgb->red = rgb_max;
        color_rgb->green = rgb_min + rgb_adj;
        color_rgb->blue = rgb_min;
        break;
    case 1:
        color_rgb->red = rgb_max - rgb_adj;
        color_rgb->green = rgb_max;
        color_rgb->blue = rgb_min;
        break;
    case 2:
        color_rgb->red = rgb_min;
        color_rgb->green = rgb_max;
        color_rgb->blue = rgb_min + rgb_adj;
        break;
    case 3:
        color_rgb->red = rgb_min;
        color_rgb->green = rgb_max - rgb_adj;
        color_rgb->blue = rgb_max;
        break;
    case 4:
        color_rgb->red = rgb_min + rgb_adj;
        color_rgb->green = rgb_min;
        color_rgb->blue = rgb_max;
        break;
    default:
        color_rgb->red = rgb_max;
        color_rgb->green = rgb_min;
        color_rgb->blue = rgb_max - rgb_adj;
        break;
    }
}

// Helper to clamp float to uint8_t
static inline uint8_t clamp_u8(float value)
{
    if (value < 0.0f) {
        return 0;
    }
    if (value > 255.0f) {
        return 255;
    }
    return (uint8_t)value;
}

// Applies gamma correction to make colors appear natural to human eyes
static inline uint8_t apply_gamma(uint8_t color)
{
    float normalized = color / 255.0f;
    // Gamma of 2.8 is standard for LEDs to create visible color separation
    return clamp_u8(powf(normalized, 2.8f) * 255.0f);
}

static void __internal_cct_to_rgb(uint16_t cct, app_led_color_rgb_t *color_rgb)
{
    if (cct < 1000) {
        cct = 1000;
    }
    if (cct > 40000) {
        cct = 40000;
    }

    float temp = cct / 100.0f;
    float r, g, b;

    /* 1. Calculate Red */
    if (temp <= 66.0f) {
        r = 255.0f;
    } else {
        r = temp - 60.0f;
        r = 329.698727446f * powf(r, -0.1332047592f);
    }

    /* 2. Calculate Green */
    if (temp <= 66.0f) {
        g = temp;
        g = 99.4708025861f * logf(g) - 161.1195681661f;
    } else {
        g = temp - 60.0f;
        g = 288.1221695283f * powf(g, -0.0755148492f);
    }

    /* 3. Calculate Blue */
    if (temp >= 66.0f) {
        b = 255.0f;
    } else if (temp <= 19.0f) {
        b = 0.0f;
    } else {
        b = temp - 10.0f;
        b = 138.5177312231f * logf(b) - 305.0447927307f;
    }

    /* 4. Apply Gamma Correction for physical LEDs */
    color_rgb->red = apply_gamma(clamp_u8(r));
    color_rgb->green = apply_gamma(clamp_u8(g));
    color_rgb->blue = apply_gamma(clamp_u8(b));
}

static void __internal_scale_rgb_brightness(app_led_color_rgb_t *color_rgb, uint8_t brightness)
{
    uint32_t red_scaled = (uint32_t)color_rgb->red * (uint32_t)brightness / 100;
    uint32_t green_scaled = (uint32_t)color_rgb->green * (uint32_t)brightness / 100;
    uint32_t blue_scaled = (uint32_t)color_rgb->blue * (uint32_t)brightness / 100;
    color_rgb->red = (uint8_t)red_scaled;
    color_rgb->green = (uint8_t)green_scaled;
    color_rgb->blue = (uint8_t)blue_scaled;
}

static osal_err_t __internal_update_hardware_led(void)
{
    app_led_color_rgb_t color_rgb = {0};

    if (__led_state.power) {
        /* Decide RGB color based on the mode */
        switch (__led_state.mode) {
        case APP_LED_MODE_HSV:
            __internal_hsv_to_rgb(__led_state.color_hs, &color_rgb);
            break;
        case APP_LED_MODE_CCT:
            __internal_cct_to_rgb(__led_state.cct, &color_rgb);
            break;
        default:
            return OSAL_ERR_NOT_SUPPORTED;
        }

        /* Scale brightness */
        __internal_scale_rgb_brightness(&color_rgb, __led_state.brightness);
    } else {
        /* Turn off the LED */
        color_rgb.red = 0;
        color_rgb.green = 0;
        color_rgb.blue = 0;
    }

    return app_led_internal_set_color_rgb(color_rgb);
}

/**
 * @brief Apply a single already-validated field change to the LED.
 *
 * Takes the lock, writes the field, pushes the result to the hardware and
 * reverts the field if the hardware rejects it, so that a non-OK return always
 * means the LED (and the cached state) is unchanged.
 *
 * @param[in] field The field to change.
 * @param[in] value The new value, widened to uint32_t.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
static osal_err_t __internal_set_field(led_field_t field, uint32_t value)
{
    osal_err_t ret = __internal_lock();
    if (ret != OSAL_ERR_OK) {
        return ret;
    }

    app_led_state_t previous = __led_state;
    switch (field) {
    case LED_FIELD_POWER:
        __led_state.power = (bool)value;
        break;
    case LED_FIELD_HUE:
        __led_state.color_hs.hue = (uint16_t)value;
        break;
    case LED_FIELD_SATURATION:
        __led_state.color_hs.saturation = (uint8_t)value;
        break;
    case LED_FIELD_BRIGHTNESS:
        __led_state.brightness = (uint8_t)value;
        break;
    case LED_FIELD_CCT:
        __led_state.cct = (uint16_t)value;
        break;
    case LED_FIELD_MODE:
        __led_state.mode = (app_led_mode_t)value;
        break;
    default:
        __internal_unlock();
        return OSAL_ERR_INVALID_ARG;
    }

    /* Push to the hardware, reverting the field if the hardware refuses it */
    ret = __internal_update_hardware_led();
    if (ret != OSAL_ERR_OK) {
        __led_state = previous;
    }

    __internal_unlock();
    return ret;
}

/* Public function definitions ****************************************************/

osal_err_t app_led_init(const app_led_state_t *p_state)
{
    /* Create the lock that serialises the setters against each other */
    if (__led_lock == NULL) {
        __led_lock = osal_semaphore_create_mutex();
        if (__led_lock == NULL) {
            return OSAL_ERR_NO_MEM;
        }
    }

    /* Initialize the LED driver */
    osal_err_t ret = OSAL_ERR_OK;
    ret = app_led_internal_init();
    if (ret != OSAL_ERR_OK) {
        return ret;
    }

    /* Apply the initial state */
    return app_led_apply(p_state);
}

osal_err_t app_led_apply(const app_led_state_t *p_state)
{
    /* Validate the pointer */
    if (p_state == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    /* Validate the LED state */
    switch (p_state->mode) {
    case APP_LED_MODE_HSV:
        if (p_state->color_hs.hue > 360) {
            return OSAL_ERR_INVALID_ARG;
        }
        if (p_state->color_hs.saturation > 100) {
            return OSAL_ERR_INVALID_ARG;
        }
        break;
    case APP_LED_MODE_CCT:
        if (p_state->cct < 2700 || p_state->cct > 6500) {
            return OSAL_ERR_INVALID_ARG;
        }
        break;
    default:
        return OSAL_ERR_INVALID_ARG;
    }
    if (p_state->brightness > 100) {
        return OSAL_ERR_INVALID_ARG;
    }

    osal_err_t ret = __internal_lock();
    if (ret != OSAL_ERR_OK) {
        return ret;
    }

    /* Set the LED state, reverting it if the hardware refuses the change */
    app_led_state_t previous = __led_state;
    __led_state = *p_state;
    ret = __internal_update_hardware_led();
    if (ret != OSAL_ERR_OK) {
        __led_state = previous;
    }

    __internal_unlock();
    return ret;
}

osal_err_t app_led_set_power(bool power)
{
    return __internal_set_field(LED_FIELD_POWER, power);
}

osal_err_t app_led_set_hue(uint16_t hue)
{
    if (hue > 360) {
        return OSAL_ERR_INVALID_ARG;
    }
    return __internal_set_field(LED_FIELD_HUE, hue);
}

osal_err_t app_led_set_saturation(uint8_t saturation)
{
    if (saturation > 100) {
        return OSAL_ERR_INVALID_ARG;
    }
    return __internal_set_field(LED_FIELD_SATURATION, saturation);
}

osal_err_t app_led_set_brightness(uint8_t brightness)
{
    if (brightness > 100) {
        return OSAL_ERR_INVALID_ARG;
    }
    return __internal_set_field(LED_FIELD_BRIGHTNESS, brightness);
}

osal_err_t app_led_set_cct(uint16_t cct)
{
    if (cct < 2700 || cct > 6500) {
        return OSAL_ERR_INVALID_ARG;
    }
    return __internal_set_field(LED_FIELD_CCT, cct);
}

osal_err_t app_led_set_mode(app_led_mode_t mode)
{
    if (mode <= APP_LED_MODE_INVALID || mode >= APP_LED_MODE_MAX) {
        return OSAL_ERR_INVALID_ARG;
    }
    return __internal_set_field(LED_FIELD_MODE, mode);
}
