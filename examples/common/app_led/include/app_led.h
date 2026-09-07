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

/* Brightness shape of one effect cycle, between level_low and full brightness */
typedef enum {
    APP_LED_WAVEFORM_STEADY = 0, // Full brightness for the whole cycle
    APP_LED_WAVEFORM_PULSE,      // Full brightness for on_ms, then level_low for the rest
    APP_LED_WAVEFORM_RAMP,       // Up over the first half of the cycle, down over the second
    APP_LED_WAVEFORM_MAX,        // Used for bounds calculation
} app_led_waveform_t;

/* Attention-drawing pattern played over the tracked state, e.g. to identify the device */
typedef struct {
    app_led_waveform_t waveform;
    uint32_t period_ms;         // One cycle of the waveform
    uint32_t on_ms;             // PULSE: how much of the cycle stays at full brightness
    uint32_t cycles;            // Cycles to play, 0 to play until a stop request
    uint8_t level_low;          // Brightness of the low phase, 0-100
    bool color_override;        // Play color_hs in place of the tracked color
    app_led_color_hs_t color_hs;
} app_led_effect_t;

/* Public function declarations ****************************************************/

/**
 * @brief Initialize the LED and light it with the given state.
 *
 * @param[in] p_state Pointer to the initial state of the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_init(const app_led_state_t *p_state);

/**
 * @brief Initialize the LED, but leave it dark.
 *
 * The state is tracked and the setters keep working (and validating) as usual, the LED simply
 * stays unlit until ::app_led_mark_live. For an application whose real state is only known some
 * way into the boot - this avoids lighting the defaults first and jumping to the
 * real state a moment later.
 *
 * @param[in] p_state Pointer to the initial state of the LED.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_init_dark(const app_led_state_t *p_state);

/**
 * @brief Light the LED with the state tracked so far, ending an ::app_led_init_dark.
 *
 * Safe to call on an LED that is already live: it re-applies the tracked state.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_mark_live(void);

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

/**
 * @brief Play an effect over the tracked state.
 *
 * The effect drives the LED whatever the tracked power/brightness say, so it stays visible on a
 * light the user turned off. The setters keep working while it runs; the tracked state is shown
 * again once it ends. Starting an effect while one is running replaces it.
 *
 * @note A frame lasts 50 ms: @c period_ms must be 100 ms or more, and a multiple of 50 ms
 *       renders exactly.
 *
 * @param[in] p_effect Pointer to the effect to play.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_effect_start(const app_led_effect_t *p_effect);

/**
 * @brief Stop the running effect at once and show the tracked state again.
 *
 * @note One effect plays at a time: a start replaces the running effect, and one stop ends it.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_effect_stop(void);

/**
 * @brief Let the running effect play to the end of its current cycle, then stop it.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_led_effect_stop_at_cycle_end(void);

/**
 * @brief Whether an effect is playing, false if the LED shows the tracked state.
 */
bool app_led_effect_active(void);

#endif /* __APP_LED_H__ */
