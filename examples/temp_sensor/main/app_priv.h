/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_priv.h
 * @brief Private interface for the temperature sensor example.
 */

#pragma once

#include "esp_rmaker_core.h"

/* The initial reading, the reported range and the step are owned by temp_sim.h
 * (TEMP_SIM_DEFAULT_TEMP_C / _MIN_TEMP_C / _MAX_TEMP_C / _UI_STEP_C); the
 * parameter bounds below are declared from those. */

/* Device + its single read-only, time-series temperature parameter. */
extern esp_rmaker_device_t *temp_sensor_device;
extern esp_rmaker_param_t *temp_sensor_temperature_param;

/* Initialise the LED hardware and set the initial indication. */
void app_driver_init(void);

/* Start the background temperature simulation. Call once the node's
 * temperature parameter has been created, so readings can be reported. */
void app_driver_start_simulation(void);

/* Current (simulated) temperature, in degrees Celsius. */
float app_driver_get_temperature(void);
