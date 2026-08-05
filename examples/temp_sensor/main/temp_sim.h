/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file temp_sim.h
 * @brief Ambient-temperature sensor simulation.
 *
 * Emulates a free-running temperature sensor: on every period the standing
 * temperature is nudged by a random delta in [-max_delta, +max_delta] and
 * clamped to [min, max], then the caller's callback is invoked with the new
 * reading. The simulation owns the temperature; the caller decides what to do
 * with each reading (report it to RainMaker Neo, drive an LED, ...).
 */

#ifndef __TEMP_SIM_H__
#define __TEMP_SIM_H__

/* Includes ****************************************************************/

/* Standard includes */
#include <stdint.h>

/* Platform includes */
#include "osal_err.h"

/* Constants ****************************************************************/

/** Default initial reading, in degrees Celsius. Used to seed the RainMaker Neo
 *  parameter before the simulation task is started. */
#define TEMP_SIM_DEFAULT_TEMP_C   25.0f

/** Default reported range, in degrees Celsius. The simulation clamps every
 *  reading to [MIN, MAX]. */
#define TEMP_SIM_MIN_TEMP_C       0.0f
#define TEMP_SIM_MAX_TEMP_C       50.0f

/** Default maximum absolute change per step, in degrees Celsius. */
#define TEMP_SIM_MAX_DELTA_C      2.0f

/** Default period between readings, in milliseconds. */
#define TEMP_SIM_PERIOD_MS        60000

/** Reported UI step, in degrees Celsius. Readings are rounded to this. */
#define TEMP_SIM_UI_STEP_C        0.1f

/* Types ****************************************************************/

/**
 * @brief Callback invoked from the scheduler (timer) context on every new
 *        reading, so it must not block.
 *        Use it to report the value (e.g. esp_rmaker_param_update) and/or to
 *        drive a hardware indicator. Pass NULL if not needed.
 *
 * @param[in] temperature The new temperature, in degrees Celsius.
 */
typedef void (*temp_sim_reading_cb_t)(float temperature);

/**
 * @brief Configuration for the temperature simulation.
 */
typedef struct {
    temp_sim_reading_cb_t on_reading; /**< Fired on every simulated reading. */
    float initial_temp;               /**< Initial temperature in degC. */
    float min_temp;                   /**< Lower clamp in degC. */
    float max_temp;                   /**< Upper clamp in degC. */
    float max_delta;                  /**< Maximum absolute change per step in degC. */
    uint32_t period_ms;               /**< Period between readings, in milliseconds. */
} temp_sim_config_t;

/* Public function declarations ****************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the periodic temperature simulation. The configuration is copied
 *        internally; the caller's struct does not need to outlive this call.
 *
 * @param[in] config Pointer to a configuration struct.
 *
 * @return OSAL_ERR_OK on success, error code otherwise.
 */
osal_err_t temp_sim_start(const temp_sim_config_t *config);

/**
 * @brief Stop and cancel the temperature simulation.
 */
void temp_sim_stop(void);

/**
 * @brief Get the current (simulated) temperature, in degrees Celsius.
 *
 * Before @ref temp_sim_start it returns @ref TEMP_SIM_DEFAULT_TEMP_C, so it
 * can be used to seed the RainMaker Neo parameter.
 */
float temp_sim_get_temperature(void);

#ifdef __cplusplus
}
#endif

#endif /* __TEMP_SIM_H__ */
