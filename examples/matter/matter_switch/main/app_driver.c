/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/* Switch driver: on-board LED as the load indicator + boot button, via the
 * app_led/app_button common components (so the example is testable on a bare
 * devkit). */

#include "osal_err.h"
#include "osal_log.h"

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>

#include <esp_rmaker_matter_mirror.h>

#include "app_led.h"
#include "app_button.h"
#include "app_matter_effects.h"
#include "app_priv.h"

static const char *TAG = "app_driver";

/* Live switch state, tracked so the button callbacks can consult it. */
static bool g_power_state = DEFAULT_POWER;
static int g_dim = DEFAULT_DIM;

/* LED indicator default: red, at the default dim level, starts off. */
static const app_led_state_t s_default_led_state = {
    .power = DEFAULT_POWER,
    .brightness = DEFAULT_DIM,
    .color_hs = {.hue = 0, .saturation = 100},
    .cct = 0,
    .mode = APP_LED_MODE_HSV,
};

osal_err_t app_driver_set_power(bool power)
{
    osal_err_t err = app_led_set_power(power);
    if (err != OSAL_ERR_OK) {
        return err;
    }
    g_power_state = power;
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Power changed to %s", power ? "true" : "false");
    return OSAL_ERR_OK;
}

/* The dim level is the load's level: the indicator LED renders it as brightness. */
osal_err_t app_driver_set_dim(int dim)
{
    osal_err_t err = app_led_set_brightness(dim);
    if (err != OSAL_ERR_OK) {
        return err;
    }
    g_dim = dim;
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Dim level changed to %d", dim);
    return OSAL_ERR_OK;
}

bool app_driver_get_power(void)
{
    return g_power_state;
}

int app_driver_get_dim(void)
{
    return g_dim;
}

osal_err_t app_driver_identify(esp_rmaker_matter_mirror_identify_effect_t effect_id, int32_t seconds)
{
    switch (effect_id) {
    case ESP_RMAKER_MATTER_MIRROR_EFFECT_STOP:
        return app_led_effect_stop();
    case ESP_RMAKER_MATTER_MIRROR_EFFECT_FINISH:
        return app_led_effect_stop_at_cycle_end();
    case ESP_RMAKER_MATTER_MIRROR_IDENTIFY_PLAIN:
        /* IdentifyTime 0 is the stop that ends a plain Identify */
        if (seconds <= 0) {
            return app_led_effect_stop();
        }
        break;
    default:
        break;
    }

    /* The indicator renders brightness alone, so every effect follows the non-colored variant */
    app_led_effect_t effect;
    osal_err_t err = app_matter_effects_get(effect_id, APP_MATTER_EFFECT_LIGHT_NON_COLORED, seconds,
                                            &effect);
    if (err != OSAL_ERR_OK) {
        /* No pattern for this effect: let the mirror fall back to the identify param */
        return err;
    }

    /* The pattern carries its own length, so the log reports what the LED renders */
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Identify effect 0x%X for %ums", (unsigned)effect_id,
              (unsigned)(effect.period_ms * effect.cycles));
    return app_led_effect_start(&effect);
}

/* Button short press toggles power and reports the new state to the cloud
 * (and, via the Matter mirror, onto the fabric). */
static void push_btn_short_cb(void *handle, void *arg)
{
    (void)handle;
    (void)arg;
    bool new_state = !g_power_state;
    if (app_driver_set_power(new_state) != OSAL_ERR_OK) {
        return;
    }
    /* Before the node is up the device does not exist yet: the hardware still
     * changes, only the report is skipped. */
    if (switch_device) {
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(switch_device, ESP_RMAKER_PARAM_POWER),
            esp_rmaker_bool(new_state));
    }
}

/* Button long press steps the dim level, wrapping, and reports it. */
static void push_btn_long_cb(void *handle, void *arg)
{
    (void)handle;
    (void)arg;
    int dim = g_dim + DIM_STEP;
    if (dim > 100) {
        dim = DIM_STEP;
    }
    if (app_driver_set_dim(dim) == OSAL_ERR_OK && switch_device) {
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(switch_device, ESP_RMAKER_PARAM_DIM),
            esp_rmaker_int(dim));
    }
}

void app_driver_init(void)
{
    /* The LED stays dark until app_driver_mark_ready(): the boot state is only settled once the
     * Matter stack has applied its StartUp attributes, and the defaults must not show first. */
    if (app_led_init_dark(&s_default_led_state) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to initialise the LED; check the configuration");
    }
    /* Boot button: short press toggles power, long press steps the dim level. */
    app_button_config_t btn_cfg = {
        .callbacks = {
            .on_short_press = push_btn_short_cb,
            .on_long_press = push_btn_long_cb,
        },
    };
    app_button_init(&btn_cfg);
}

void app_driver_mark_ready(void)
{
    if (app_led_mark_live() != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to light the LED");
    }
}
