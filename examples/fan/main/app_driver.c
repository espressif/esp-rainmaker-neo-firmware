/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/* Fan driver: on-board RGB LED (status animation) + boot button, via the
 * app_led/app_button common components (so the example is testable on a bare
 * devkit, and simulated on POSIX where those components are no-op stubs).
 * The LED status animation is provided by fan_sim.c, which runs the animation
 * task (on the osal task API, so it runs unchanged on both targets) and hands
 * each frame back via on_frame. */

#include "osal_err.h"
#include "osal_log.h"

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>

#include "app_led.h"
#include "app_button.h"
#include "fan_sim.h"
#include "app_priv.h"

static const char *TAG = "app_driver";

/* LED indicator default: blue, dim (HSV). The animation shifts hue and
 * brightness from here while the fan is running. */
static const app_led_state_t s_default_led_state = {
    .power = DEFAULT_POWER,
    .brightness = FAN_SIM_DEFAULT_BRIGHTNESS,
    .color_hs = {.hue = FAN_SIM_DEFAULT_HUE, .saturation = 100},
    .cct = 0,
    .mode = APP_LED_MODE_HSV,
};

osal_err_t app_driver_set_power(bool power)
{
    osal_err_t err = app_led_set_power(power);
    if (err != OSAL_ERR_OK) {
        return err;
    }
    fan_sim_set_power(power);
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Power changed to %s", power ? "ON" : "OFF");
    return OSAL_ERR_OK;
}

osal_err_t app_driver_set_swing(bool swing)
{
    fan_sim_set_swing(swing);
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Swing changed to %s", swing ? "ON" : "OFF");
    return OSAL_ERR_OK;
}

osal_err_t app_driver_set_speed(int speed)
{
    if (speed < FAN_SPEED_MIN || speed > FAN_SPEED_MAX) {
        OSAL_LOGE(TAG, "Invalid speed value: %d (must be %d-%d)", speed, FAN_SPEED_MIN, FAN_SPEED_MAX);
        return OSAL_ERR_INVALID_ARG;
    }
    fan_sim_set_speed(speed);
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Speed changed to %d", speed);
    return OSAL_ERR_OK;
}

bool app_driver_get_power(void)
{
    return fan_sim_get_power();
}

bool app_driver_get_swing(void)
{
    return fan_sim_get_swing();
}

int app_driver_get_speed(void)
{
    return fan_sim_get_speed();
}

/* Button short press toggles power and reports the new state to the cloud. */
static void push_btn_short_cb(void *handle, void *arg)
{
    (void)handle;
    (void)arg;
    bool new_power = !fan_sim_get_power();
    /* Before the node is up the device does not exist yet: the hardware still
     * changes, only the report is skipped. */
    if (app_driver_set_power(new_power) == OSAL_ERR_OK && fan_device) {
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(fan_device, ESP_RMAKER_PARAM_POWER),
            esp_rmaker_bool(new_power));
    }
}

/* Button long press toggles swing and cycles the speed (1-5), reporting both. */
static void push_btn_long_cb(void *handle, void *arg)
{
    (void)handle;
    (void)arg;
    bool new_swing = !fan_sim_get_swing();
    int new_speed = (fan_sim_get_speed() % FAN_SPEED_MAX) + 1;
    if (app_driver_set_swing(new_swing) == OSAL_ERR_OK && fan_device) {
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(fan_device, ESP_RMAKER_PARAM_DIRECTION),
            esp_rmaker_bool(new_swing));
    }
    if (app_driver_set_speed(new_speed) == OSAL_ERR_OK && fan_device) {
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(fan_device, ESP_RMAKER_PARAM_SPEED),
            esp_rmaker_int(new_speed));
    }
}

/* fan_sim frame callback: push each animated frame straight to the LED. */
static void on_led_frame(uint16_t hue, uint8_t brightness)
{
    app_led_set_hue(hue);
    app_led_set_brightness(brightness);
}

void app_driver_init(void)
{
    if (app_led_init(&s_default_led_state) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to initialise the LED; check the configuration");
    }
    /* Boot button: short press toggles power, long press toggles swing + speed. */
    app_button_config_t btn_cfg = {
        .callbacks = {
            .on_short_press = push_btn_short_cb,
            .on_long_press = push_btn_long_cb,
        },
    };
    app_button_init(&btn_cfg);

    /* Drive the LED status animation from the fan_sim task. */
    fan_sim_config_t sim_cfg = {
        .on_frame = on_led_frame,
        .initial_power = DEFAULT_POWER,
        .initial_swing = DEFAULT_SWING,
        .initial_speed = DEFAULT_SPEED,
        .initial_hue = FAN_SIM_DEFAULT_HUE,
        .initial_brightness = FAN_SIM_DEFAULT_BRIGHTNESS,
        .task_stack_depth = 2048,
        .task_priority = 5,
    };
    if (fan_sim_start(&sim_cfg) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to start the fan LED animation");
    }
}
