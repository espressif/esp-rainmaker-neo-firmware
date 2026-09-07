/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/* Color light driver (CCT + HSV): on-board LED + boot button, via the
 * app_led/app_button common components (so the example is testable on a bare
 * devkit). */

#include <stdlib.h>
#include <time.h>

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

/* Live light state, tracked so the button callbacks can consult it. */
static bool g_power_state = DEFAULT_POWER;
static int g_brightness = DEFAULT_BRIGHTNESS;
static int g_cct = DEFAULT_CCT;
static int g_hue = DEFAULT_HUE;
static int g_saturation = DEFAULT_SATURATION;
static esp_rmaker_light_mode_t g_light_mode = DEFAULT_LIGHT_MODE;

/* LED indicator default: warm white at 50% brightness (CCT mode). */
static const app_led_state_t s_default_led_state = {
    .power = DEFAULT_POWER,
    .brightness = DEFAULT_BRIGHTNESS,
    .cct = DEFAULT_CCT,
    .color_hs = {.hue = DEFAULT_HUE, .saturation = DEFAULT_SATURATION},
    .mode = APP_LED_MODE_CCT,
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

osal_err_t app_driver_set_brightness(int brightness)
{
    osal_err_t err = app_led_set_brightness(brightness);
    if (err != OSAL_ERR_OK) {
        return err;
    }
    g_brightness = brightness;
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Brightness changed to %d", brightness);
    return OSAL_ERR_OK;
}

osal_err_t app_driver_set_cct(int cct)
{
    osal_err_t err = app_led_set_cct(cct);
    if (err != OSAL_ERR_OK) {
        return err;
    }
    g_cct = cct;
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Color temperature changed to %d", cct);
    return OSAL_ERR_OK;
}

osal_err_t app_driver_set_hue(int hue)
{
    osal_err_t err = app_led_set_hue(hue);
    if (err != OSAL_ERR_OK) {
        return err;
    }
    g_hue = hue;
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Hue changed to %d", hue);
    return OSAL_ERR_OK;
}

osal_err_t app_driver_set_saturation(int saturation)
{
    osal_err_t err = app_led_set_saturation(saturation);
    if (err != OSAL_ERR_OK) {
        return err;
    }
    g_saturation = saturation;
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Saturation changed to %d", saturation);
    return OSAL_ERR_OK;
}

osal_err_t app_driver_set_light_mode(esp_rmaker_light_mode_t light_mode)
{
    app_led_mode_t led_mode;
    const char *light_mode_str;
    switch (light_mode) {
    case ESP_RMAKER_LIGHT_MODE_HSV:
        led_mode = APP_LED_MODE_HSV;
        light_mode_str = "HSV";
        break;
    case ESP_RMAKER_LIGHT_MODE_CCT:
        led_mode = APP_LED_MODE_CCT;
        light_mode_str = "CCT";
        break;
    default:
        return OSAL_ERR_INVALID_ARG;
    }
    osal_err_t err = app_led_set_mode(led_mode);
    if (err != OSAL_ERR_OK) {
        return err;
    }
    g_light_mode = light_mode;
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Light mode changed to %s", light_mode_str);
    return OSAL_ERR_OK;
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

    /* This example is a colored light, so it renders the colored variant of every effect */
    app_led_effect_t effect;
    osal_err_t err = app_matter_effects_get(effect_id, APP_MATTER_EFFECT_LIGHT_COLORED, seconds,
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

bool app_driver_get_power(void)
{
    return g_power_state;
}

int app_driver_get_brightness(void)
{
    return g_brightness;
}

int app_driver_get_cct(void)
{
    return g_cct;
}

int app_driver_get_hue(void)
{
    return g_hue;
}

int app_driver_get_saturation(void)
{
    return g_saturation;
}

esp_rmaker_light_mode_t app_driver_get_light_mode(void)
{
    return g_light_mode;
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
    if (light_device) {
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(light_device, ESP_RMAKER_PARAM_POWER),
            esp_rmaker_bool(new_state));
    }
}

/* Button long press picks a random color temperature and reports it. */
static void push_btn_long_cb(void *handle, void *arg)
{
    (void)handle;
    (void)arg;
    int cct = 2700 + rand() % (6500 - 2700 + 1);
    if (app_driver_set_cct(cct) == OSAL_ERR_OK && light_device) {
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(light_device, ESP_RMAKER_PARAM_CCT),
            esp_rmaker_int(cct));
    }
}

void app_driver_init(void)
{
    /* Seed the RNG used by the long-press random-CCT feature. */
    srand(time(NULL));

    /* The LED stays dark until app_driver_mark_ready(): the boot state is only settled once the
     * Matter stack has applied its StartUp attributes, and the defaults must not show first. */
    if (app_led_init_dark(&s_default_led_state) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to initialise the LED; check the configuration");
    }
    /* Boot button: short press toggles power, long press sets a random CCT. */
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
