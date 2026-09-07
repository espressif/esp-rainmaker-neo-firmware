/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_matter_effects.h
 * @brief The Matter Identify effect examples, as app_led patterns.
 */

#ifndef __APP_MATTER_EFFECTS_H__
#define __APP_MATTER_EFFECTS_H__

/* Standard includes */
#include <stdint.h>

/* Platform includes */
#include "osal_err.h"

/* SDK includes */
#include <esp_rmaker_matter_mirror.h>

/* App includes */
#include "app_led.h"

/* Types ****************************************************************/

/* What the device can render, which picks the example an effect follows */
typedef enum {
    APP_MATTER_EFFECT_LIGHT_NON_COLORED = 0, // Brightness alone
    APP_MATTER_EFFECT_LIGHT_COLORED,         // Hue and saturation as well
    APP_MATTER_EFFECT_LIGHT_MAX,             // Used for bounds calculation
} app_matter_effect_light_t;

/* Public function declarations ****************************************************/

/**
 * @brief Fill @p p_effect with the pattern the Matter spec gives for @p effect_id.
 *
 * @note @p seconds applies to a plain Identify alone: every TriggerEffect effect carries the
 *       length of its own example.
 *
 * @param[in] effect_id What the controller asked the device to render.
 * @param[in] light What the device can render.
 * @param[in] seconds IdentifyTime of a plain Identify, ignored for the effects.
 * @param[out] p_effect Pointer to the pattern to fill.
 *
 * @return OSAL_ERR_OK on success, OSAL_ERR_NOT_SUPPORTED for a request that names no pattern
 *         (::ESP_RMAKER_MATTER_MIRROR_EFFECT_FINISH and ::ESP_RMAKER_MATTER_MIRROR_EFFECT_STOP
 *         end the running effect instead), OSAL_ERR_INVALID_ARG for a bad argument.
 */
osal_err_t app_matter_effects_get(esp_rmaker_matter_mirror_identify_effect_t effect_id,
                                  app_matter_effect_light_t light, int32_t seconds,
                                  app_led_effect_t *p_effect);

#endif /* __APP_MATTER_EFFECTS_H__ */
