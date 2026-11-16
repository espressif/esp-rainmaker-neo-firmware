/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_matter_effects.c
 * @brief The Matter Identify effect examples, as app_led patterns.
 */

/* Includes ****************************************************************/

#include "app_matter_effects.h"

/* Standard includes */
#include <stdint.h>

/* Definitions ****************************************************************/

/* Cycles and colours of the examples in the Identify cluster, spec section 1.2.4.2 */
#define EFFECT_BLINK_PERIOD_MS 500
#define EFFECT_BLINK_ON_MS 250
#define EFFECT_BREATHE_PERIOD_MS 1000
#define EFFECT_BREATHE_CYCLES 15
#define EFFECT_OKAY_PERIOD_MS 1000
#define EFFECT_OKAY_FLASH_PERIOD_MS 300
#define EFFECT_OKAY_FLASH_ON_MS 150
#define EFFECT_OKAY_FLASHES 2
#define EFFECT_CHANNEL_CHANGE_PERIOD_MS 8000
#define EFFECT_CHANNEL_CHANGE_ON_MS 500

#define EFFECT_LEVEL_MINIMUM 1
#define EFFECT_HUE_GREEN 120
#define EFFECT_HUE_ORANGE 30
#define EFFECT_SATURATION_FULL 100
#define EFFECT_MS_PER_SECOND 1000

/* Variables ****************************************************************/

/* Blink: on and off once. A plain Identify repeats it for IdentifyTime. */
static const app_led_effect_t __blink = {
    .waveform = APP_LED_WAVEFORM_PULSE,
    .period_ms = EFFECT_BLINK_PERIOD_MS,
    .on_ms = EFFECT_BLINK_ON_MS,
    .cycles = 1,
};

/* Breathe: on and off over 1 s, 15 times. */
static const app_led_effect_t __breathe = {
    .waveform = APP_LED_WAVEFORM_RAMP,
    .period_ms = EFFECT_BREATHE_PERIOD_MS,
    .cycles = EFFECT_BREATHE_CYCLES,
};

/* Okay on a non-colored light: two flashes. */
static const app_led_effect_t __okay_non_colored = {
    .waveform = APP_LED_WAVEFORM_PULSE,
    .period_ms = EFFECT_OKAY_FLASH_PERIOD_MS,
    .on_ms = EFFECT_OKAY_FLASH_ON_MS,
    .cycles = EFFECT_OKAY_FLASHES,
};

/* Okay on a colored light: green for 1 s. */
static const app_led_effect_t __okay_colored = {
    .waveform = APP_LED_WAVEFORM_STEADY,
    .period_ms = EFFECT_OKAY_PERIOD_MS,
    .cycles = 1,
    .color_override = true,
    .color_hs = {.hue = EFFECT_HUE_GREEN, .saturation = EFFECT_SATURATION_FULL},
};

/* ChannelChange on a non-colored light: maximum brightness for 0.5 s, then minimum for 7.5 s. */
static const app_led_effect_t __channel_change_non_colored = {
    .waveform = APP_LED_WAVEFORM_PULSE,
    .period_ms = EFFECT_CHANNEL_CHANGE_PERIOD_MS,
    .on_ms = EFFECT_CHANNEL_CHANGE_ON_MS,
    .cycles = 1,
    .level_low = EFFECT_LEVEL_MINIMUM,
};

/* ChannelChange on a colored light: orange for 8 s. */
static const app_led_effect_t __channel_change_colored = {
    .waveform = APP_LED_WAVEFORM_STEADY,
    .period_ms = EFFECT_CHANNEL_CHANGE_PERIOD_MS,
    .cycles = 1,
    .color_override = true,
    .color_hs = {.hue = EFFECT_HUE_ORANGE, .saturation = EFFECT_SATURATION_FULL},
};

/* Public function definitions ****************************************************/

osal_err_t app_matter_effects_get(esp_rmaker_matter_mirror_identify_effect_t effect_id,
                                  app_matter_effect_light_t light, int32_t seconds,
                                  app_led_effect_t *p_effect)
{
    if (p_effect == NULL || light >= APP_MATTER_EFFECT_LIGHT_MAX) {
        return OSAL_ERR_INVALID_ARG;
    }
    const bool colored = light == APP_MATTER_EFFECT_LIGHT_COLORED;

    switch (effect_id) {
    case ESP_RMAKER_MATTER_MIRROR_IDENTIFY_PLAIN:
        /* An Identify command names no effect: blink for IdentifyTime, which is a uint16 */
        if (seconds <= 0 || seconds > UINT16_MAX) {
            return OSAL_ERR_INVALID_ARG;
        }
        *p_effect = __blink;
        p_effect->cycles = (uint32_t)seconds * EFFECT_MS_PER_SECOND / p_effect->period_ms;
        break;
    case ESP_RMAKER_MATTER_MIRROR_EFFECT_BLINK:
        *p_effect = __blink;
        break;
    case ESP_RMAKER_MATTER_MIRROR_EFFECT_BREATHE:
        *p_effect = __breathe;
        break;
    case ESP_RMAKER_MATTER_MIRROR_EFFECT_OKAY:
        *p_effect = colored ? __okay_colored : __okay_non_colored;
        break;
    case ESP_RMAKER_MATTER_MIRROR_EFFECT_CHANNEL_CHANGE:
        *p_effect = colored ? __channel_change_colored : __channel_change_non_colored;
        break;
    default:
        /* FINISH and STOP end the running effect, and an unknown effect names no pattern */
        return OSAL_ERR_NOT_SUPPORTED;
    }

    return OSAL_ERR_OK;
}
