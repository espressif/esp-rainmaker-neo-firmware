/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/* Temperature sensor driver: on-board RGB LED used as a temperature indicator,
 * via the app_led common component. The sensor "hardware" - a reading that
 * random-walks once a second - is simulated by temp_sim.c;
 * this driver reports each reading to RMNG and drives the LED from
 * it. On POSIX the LED component is a no-op stub, so the whole thing runs
 * simulated on the host. */

#include <stddef.h>

#include "osal_err.h"
#include "osal_log.h"

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>

#include "temp_sim.h"

#include "app_led.h"
#include "app_priv.h"

static const char *TAG = "app_driver";

/* LED indicator default: full-saturation at 30% brightness (HSV). The hue is
 * driven from the reading (blue = cold, red = hot). */
static const app_led_state_t s_default_led_state = {
    .power = true,
    .brightness = 30,
    .color_hs = {.hue = 0, .saturation = 100},
    .cct = 0,
    .mode = APP_LED_MODE_HSV,
};

/* Map a temperature onto the LED hue: 240 deg (blue) when cold, 0 deg (red) when hot. */
static void update_temperature_led(float temperature)
{
    float normalized = (temperature - TEMP_SIM_MIN_TEMP_C) /
                       (TEMP_SIM_MAX_TEMP_C - TEMP_SIM_MIN_TEMP_C);
    if (normalized < 0.0f) {
        normalized = 0.0f;
    }
    if (normalized > 1.0f) {
        normalized = 1.0f;
    }
    uint16_t hue = (uint16_t)(240 - (normalized * 240));
    app_led_set_hue(hue);
    OSAL_LOGD(TAG, "Temperature LED: %.1f degC -> hue %d deg", temperature, hue);
}

/* Registered as the temp_sim reading callback: report the new reading to RMNG
 * and reflect it on the LED. Invoked from the simulation task. */
static void on_temperature_reading(float temperature)
{
    if (temp_sensor_temperature_param) {
        esp_rmaker_param_update(temp_sensor_temperature_param, esp_rmaker_float(temperature));
    }
    update_temperature_led(temperature);
}

float app_driver_get_temperature(void)
{
    return temp_sim_get_temperature();
}

void app_driver_init(void)
{
    if (app_led_init(&s_default_led_state) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to initialise the LED; check the configuration");
    }
    update_temperature_led(temp_sim_get_temperature());
}

void app_driver_start_simulation(void)
{
    temp_sim_config_t sim_config = {
        .on_reading       = on_temperature_reading,
        .initial_temp     = TEMP_SIM_DEFAULT_TEMP_C,
        .min_temp         = TEMP_SIM_MIN_TEMP_C,
        .max_temp         = TEMP_SIM_MAX_TEMP_C,
        .max_delta        = TEMP_SIM_MAX_DELTA_C,
        .period_ms        = TEMP_SIM_PERIOD_MS,
    };
    if (temp_sim_start(&sim_config) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to start temperature simulation");
    }
}
