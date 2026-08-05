/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/* Multi-device driver: one shared on-board RGB LED + boot button drive four
 * devices (light, fan, switch, temperature sensor) behind the app_led/app_button
 * common components (so the example is testable on a bare devkit, and simulated
 * on POSIX where those components are no-op stubs). The button cycles which
 * device is "focused": short press moves the focus (and points the LED at that
 * device), long press actuates the focused device. The temperature sensor is
 * read-only, so it has nothing to actuate. */

#include "osal_err.h"
#include "osal_log.h"

#include <esp_rmaker_core.h>

#include "app_led.h"
#include "app_button.h"
#include "fan_sim.h"
#include "temp_sim.h"
#include "app_priv.h"

static const char *TAG = "app_driver";

/* Live per-device state, tracked so the button callbacks can consult it.
 * Seeded to the defaults. The fan's power/swing/speed live inside fan_sim
 * (which drives the fan LED animation) and are reached via its accessors. */
static bool g_light_power = DEFAULT_LIGHT_POWER;
static int  g_light_brightness = DEFAULT_LIGHT_BRIGHTNESS;
static bool g_switch_power = DEFAULT_SWITCH_POWER;

/* Per-device LED indicator state. Only the focused device's state is pushed to
 * the (single) hardware LED via app_led_apply. Colors: light green, fan blue
 * (animated), switch red, temperature sensor blue-to-red by reading. */
static app_led_state_t s_led_light = {
    .power = DEFAULT_LIGHT_POWER,
    .brightness = DEFAULT_LIGHT_BRIGHTNESS,
    .color_hs = {.hue = 120, .saturation = 100},
    .cct = 0,
    .mode = APP_LED_MODE_HSV,
};
static app_led_state_t s_led_fan = {
    .power = DEFAULT_FAN_POWER,
    .brightness = FAN_SIM_DEFAULT_BRIGHTNESS,
    .color_hs = {.hue = FAN_SIM_DEFAULT_HUE, .saturation = 100},
    .cct = 0,
    .mode = APP_LED_MODE_HSV,
};
static app_led_state_t s_led_switch = {
    .power = DEFAULT_SWITCH_POWER,
    .brightness = 30,
    .color_hs = {.hue = 0, .saturation = 100},
    .cct = 0,
    .mode = APP_LED_MODE_HSV,
};
/* The sensor has no power state of its own, so its indicator is always on; the
 * hue is seeded from the initial reading in app_driver_init and then tracks
 * every simulated reading. */
static app_led_state_t s_led_temp_sensor = {
    .power = true,
    .brightness = 30,
    .color_hs = {.hue = 0, .saturation = 100},
    .cct = 0,
    .mode = APP_LED_MODE_HSV,
};

/* Currently focused device (cycled by short button press). */
static device_focus_t g_focus = DEVICE_FOCUS_LIGHT;

/* Return the LED state for the focused device. */
static app_led_state_t *focused_led_state(void)
{
    switch (g_focus) {
    case DEVICE_FOCUS_FAN:
        return &s_led_fan;
    case DEVICE_FOCUS_SWITCH:
        return &s_led_switch;
    case DEVICE_FOCUS_TEMP_SENSOR:
        return &s_led_temp_sensor;
    case DEVICE_FOCUS_LIGHT:
    default:
        return &s_led_light;
    }
}

/* Push the focused device's LED state to the hardware LED. */
static void apply_focused_led(void)
{
    (void)app_led_apply(focused_led_state());
}

/* ---- Light: power + brightness ---- */

osal_err_t app_light_set_power(bool power)
{
    g_light_power = power;
    s_led_light.power = power;
    if (g_focus == DEVICE_FOCUS_LIGHT) {
        apply_focused_led();
    }
    OSAL_LOGI(TAG, "!!!LIGHT HARDWARE!!! Power -> %s", power ? "ON" : "OFF");
    return OSAL_ERR_OK;
}

osal_err_t app_light_set_brightness(int brightness)
{
    g_light_brightness = brightness;
    s_led_light.brightness = (uint8_t)brightness;
    if (g_focus == DEVICE_FOCUS_LIGHT) {
        apply_focused_led();
    }
    OSAL_LOGI(TAG, "!!!LIGHT HARDWARE!!! Brightness -> %d", brightness);
    return OSAL_ERR_OK;
}

bool app_light_get_power(void)
{
    return g_light_power;
}

int app_light_get_brightness(void)
{
    return g_light_brightness;
}

/* ---- Fan: power + swing + speed ---- */

osal_err_t app_fan_set_power(bool power)
{
    fan_sim_set_power(power);
    s_led_fan.power = power;
    if (g_focus == DEVICE_FOCUS_FAN) {
        apply_focused_led();
    }
    OSAL_LOGI(TAG, "!!!FAN HARDWARE!!! Power -> %s", power ? "ON" : "OFF");
    return OSAL_ERR_OK;
}

osal_err_t app_fan_set_swing(bool swing)
{
    fan_sim_set_swing(swing);
    OSAL_LOGI(TAG, "!!!FAN HARDWARE!!! Swing -> %s", swing ? "ON" : "OFF");
    return OSAL_ERR_OK;
}

osal_err_t app_fan_set_speed(int speed)
{
    if (speed < FAN_SPEED_MIN || speed > FAN_SPEED_MAX) {
        OSAL_LOGE(TAG, "Invalid speed value: %d (must be %d-%d)", speed, FAN_SPEED_MIN, FAN_SPEED_MAX);
        return OSAL_ERR_INVALID_ARG;
    }
    fan_sim_set_speed(speed);
    OSAL_LOGI(TAG, "!!!FAN HARDWARE!!! Speed -> %d", speed);
    return OSAL_ERR_OK;
}

bool app_fan_get_power(void)
{
    return fan_sim_get_power();
}

bool app_fan_get_swing(void)
{
    return fan_sim_get_swing();
}

int app_fan_get_speed(void)
{
    return fan_sim_get_speed();
}

/* ---- Switch: power ---- */

osal_err_t app_switch_set_power(bool power)
{
    g_switch_power = power;
    s_led_switch.power = power;
    if (g_focus == DEVICE_FOCUS_SWITCH) {
        apply_focused_led();
    }
    OSAL_LOGI(TAG, "!!!SWITCH HARDWARE!!! Power -> %s", power ? "ON" : "OFF");
    return OSAL_ERR_OK;
}

bool app_switch_get_power(void)
{
    return g_switch_power;
}

/* ---- Temperature sensor: read-only, simulated ---- */

float app_temp_sensor_get_temperature(void)
{
    return temp_sim_get_temperature();
}

/* Map a reading onto the sensor's LED hue: 240 deg (blue) when cold, 0 deg (red) when
 * hot. Stores it in the sensor's LED state without touching the hardware. */
static void set_temp_sensor_led_hue(float temperature)
{
    float normalized = (temperature - TEMP_SIM_MIN_TEMP_C) /
                       (TEMP_SIM_MAX_TEMP_C - TEMP_SIM_MIN_TEMP_C);
    if (normalized < 0.0f) {
        normalized = 0.0f;
    }
    if (normalized > 1.0f) {
        normalized = 1.0f;
    }
    s_led_temp_sensor.color_hs.hue = (uint16_t)(240 - (normalized * 240));
}

/* temp_sim reading callback: report the new reading to the cloud and reflect it
 * on the sensor's LED, pushing it to the hardware LED only while the sensor is
 * the focused device. */
static void on_temperature_reading(float temperature)
{
    if (temp_sensor_temperature_param) {
        esp_rmaker_param_update(temp_sensor_temperature_param, esp_rmaker_float(temperature));
    }

    set_temp_sensor_led_hue(temperature);
    if (g_focus == DEVICE_FOCUS_TEMP_SENSOR) {
        apply_focused_led();
    }
}

/* fan_sim frame callback: the shared animation computes the fan's hue and
 * brightness; stash them in the fan's LED state and push to the hardware LED
 * only while the fan is the focused device. */
static void on_fan_led_frame(uint16_t hue, uint8_t brightness)
{
    s_led_fan.color_hs.hue = hue;
    s_led_fan.brightness = brightness;
    if (g_focus == DEVICE_FOCUS_FAN) {
        apply_focused_led();
    }
}

/* Short press cycles the focused device and points the LED at it; it issues no
 * param updates. */
static void push_btn_short_cb(void *handle, void *arg)
{
    (void)handle;
    (void)arg;
    static const char *names[] = {"Light", "Fan", "Switch", "Temp Sensor"};
    g_focus = (g_focus + 1) % DEVICE_FOCUS_COUNT;
    OSAL_LOGI(TAG, "Button short press - now controlling: %s", names[g_focus]);
    apply_focused_led();
}

/* Long press actuates the focused device and reports the change to the cloud.
 * The hardware is always actuated; before the node is up the device handles do
 * not exist yet, so only the param update is skipped. */
static void push_btn_long_cb(void *handle, void *arg)
{
    (void)handle;
    (void)arg;
    OSAL_LOGI(TAG, "Button long press - actuating device %d", g_focus);

    switch (g_focus) {
    case DEVICE_FOCUS_LIGHT: {
        /* Toggle power; when turning on, step brightness by 25% (wraps). */
        bool new_power = !g_light_power;
        if (app_light_set_power(new_power) == OSAL_ERR_OK && light_device) {
            esp_rmaker_param_update(
                esp_rmaker_device_get_param_by_type(light_device, ESP_RMAKER_PARAM_POWER),
                esp_rmaker_bool(new_power));
        }
        if (new_power) {
            int new_brightness = (g_light_brightness + 25) % 101;
            if (app_light_set_brightness(new_brightness) == OSAL_ERR_OK && light_device) {
                esp_rmaker_param_update(
                    esp_rmaker_device_get_param_by_type(light_device, ESP_RMAKER_PARAM_BRIGHTNESS),
                    esp_rmaker_int(new_brightness));
            }
        }
        break;
    }
    case DEVICE_FOCUS_FAN: {
        /* Toggle power; when turning on, cycle the speed (1-5). */
        bool new_power = !fan_sim_get_power();
        if (app_fan_set_power(new_power) == OSAL_ERR_OK && fan_device) {
            esp_rmaker_param_update(
                esp_rmaker_device_get_param_by_type(fan_device, ESP_RMAKER_PARAM_POWER),
                esp_rmaker_bool(new_power));
        }
        if (new_power) {
            int new_speed = (fan_sim_get_speed() % FAN_SPEED_MAX) + 1;
            if (app_fan_set_speed(new_speed) == OSAL_ERR_OK && fan_device) {
                esp_rmaker_param_update(
                    esp_rmaker_device_get_param_by_type(fan_device, ESP_RMAKER_PARAM_SPEED),
                    esp_rmaker_int(new_speed));
            }
        }
        break;
    }
    case DEVICE_FOCUS_SWITCH: {
        bool new_power = !g_switch_power;
        if (app_switch_set_power(new_power) == OSAL_ERR_OK && switch_device) {
            esp_rmaker_param_update(
                esp_rmaker_device_get_param_by_type(switch_device, ESP_RMAKER_PARAM_POWER),
                esp_rmaker_bool(new_power));
        }
        break;
    }
    case DEVICE_FOCUS_TEMP_SENSOR:
        /* Read-only device: the simulation owns the value, nothing to actuate. */
        OSAL_LOGI(TAG, "Temperature sensor is read-only; current reading %.1f degC",
                  temp_sim_get_temperature());
        break;
    default:
        break;
    }
}

void app_driver_init(void)
{
    /* Seed the sensor's indicator from the simulation's initial reading, so it
     * is already correct the first time the focus lands on it. */
    set_temp_sensor_led_hue(TEMP_SIM_DEFAULT_TEMP_C);

    /* Initialise the shared LED, pointed at the first focused device (light). */
    if (app_led_init(focused_led_state()) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to initialise the LED; check the configuration");
    }
    /* Boot button: short press cycles focus, long press actuates. */
    app_button_config_t btn_cfg = {
        .callbacks = {
            .on_short_press = push_btn_short_cb,
            .on_long_press = push_btn_long_cb,
        },
    };
    app_button_init(&btn_cfg);

    /* Drive the fan LED status animation from the fan_sim task. */
    fan_sim_config_t sim_cfg = {
        .on_frame = on_fan_led_frame,
        .initial_power = DEFAULT_FAN_POWER,
        .initial_swing = DEFAULT_FAN_SWING,
        .initial_speed = DEFAULT_FAN_SPEED,
        .initial_hue = FAN_SIM_DEFAULT_HUE,
        .initial_brightness = FAN_SIM_DEFAULT_BRIGHTNESS,
        .task_stack_depth = 2048,
        .task_priority = 5,
    };
    if (fan_sim_start(&sim_cfg) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to start the fan LED animation");
    }

    /* Drive the temperature sensor from the temp_sim task. Readings that
     * land before the node is up are logged and shown on the LED; reporting
     * starts as soon as app_main has created the parameter. */
    temp_sim_config_t temp_cfg = {
        .on_reading = on_temperature_reading,
        .initial_temp = TEMP_SIM_DEFAULT_TEMP_C,
        .min_temp = TEMP_SIM_MIN_TEMP_C,
        .max_temp = TEMP_SIM_MAX_TEMP_C,
        .max_delta = TEMP_SIM_MAX_DELTA_C,
        .period_ms = TEMP_SIM_PERIOD_MS,
    };
    if (temp_sim_start(&temp_cfg) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to start the temperature simulation");
    }
}
