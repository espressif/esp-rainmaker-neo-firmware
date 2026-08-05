/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/* Light driver: on-board RGB LED + boot button, via the app_led/app_button
 * common components (so the example is testable on a bare devkit, and simulated on
 * POSIX where those components are no-op stubs). */

#include <stdlib.h>
#include <time.h>

#include "osal_err.h"
#include "osal_log.h"

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>

#include "app_led.h"
#include "app_button.h"
#include "app_priv.h"

static const char *TAG = "app_driver";

/* Live light state, tracked so the button callbacks and the light-mode
 * auto-switch can consult it. Seeded to the defaults. */
static bool g_power_state = DEFAULT_POWER;
static int g_hue = DEFAULT_HUE;
static int g_saturation = DEFAULT_SATURATION;
static int g_brightness = DEFAULT_BRIGHTNESS;
static int g_cct = DEFAULT_CCT;
static esp_rmaker_light_mode_t g_light_mode = DEFAULT_LIGHT_MODE;

/* LED indicator default: green at 50% brightness (HSV). */
static const app_led_state_t s_default_led_state = {
    .power = DEFAULT_POWER,
    .brightness = DEFAULT_BRIGHTNESS,
    .color_hs = {.hue = DEFAULT_HUE, .saturation = DEFAULT_SATURATION},
    .cct = DEFAULT_CCT,
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

bool app_driver_get_power(void)
{
    return g_power_state;
}

int app_driver_get_hue(void)
{
    return g_hue;
}

int app_driver_get_saturation(void)
{
    return g_saturation;
}

int app_driver_get_brightness(void)
{
    return g_brightness;
}

int app_driver_get_cct(void)
{
    return g_cct;
}

esp_rmaker_light_mode_t app_driver_get_light_mode(void)
{
    return g_light_mode;
}

/* Button short press toggles power and reports the new state to the cloud. */
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

/* Button long press picks a random hue + saturation and reports them. */
static void push_btn_long_cb(void *handle, void *arg)
{
    (void)handle;
    (void)arg;
    int hue = rand() % 361;
    int saturation = rand() % 101;
    if (app_driver_set_hue(hue) == OSAL_ERR_OK && light_device) {
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(light_device, ESP_RMAKER_PARAM_HUE),
            esp_rmaker_int(hue));
    }
    if (app_driver_set_saturation(saturation) == OSAL_ERR_OK && light_device) {
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(light_device, ESP_RMAKER_PARAM_SATURATION),
            esp_rmaker_int(saturation));
    }
}

void app_driver_init(void)
{
    /* Seed the RNG used by the long-press random-color feature. */
    srand(time(NULL));

    if (app_led_init(&s_default_led_state) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to initialise the LED; check the configuration");
    }
    /* Boot button: short press toggles power, long press sets a random color. */
    app_button_config_t btn_cfg = {
        .callbacks = {
            .on_short_press = push_btn_short_cb,
            .on_long_press = push_btn_long_cb,
        },
    };
    app_button_init(&btn_cfg);
}
