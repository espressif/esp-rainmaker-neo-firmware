/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file temp_sim.c
 * @brief Implementation of the ambient-temperature simulation.
 */

/* Includes ****************************************************************/

/* Declarations */
#include "temp_sim.h"

/* Standard includes */
#include <math.h>
#include <stddef.h>

/* Platform includes */
#include "osal_log.h"
#include "osal_random.h"
#include "osal_scheduler.h"

/* Constants ****************************************************************/

/** Resolution of the random delta, in steps per degree Celsius. */
#define TEMP_SIM_DELTA_STEPS_PER_C  100
/** Round factor for the reported reading; matches the RainMaker Neo param UI step. */
#define TEMP_SIM_ROUND_FACTOR    (1.0f / TEMP_SIM_UI_STEP_C)

/* Tag for logging */
static const char *TAG = "temp_sim";

/* Variables ****************************************************************/

/* Configuration */
static temp_sim_config_t s_config;

/* Handle of the periodic scheduled reading */
static osal_scheduler_task_handle_t s_sched = NULL;

/* Current temperature */
static float s_temperature = TEMP_SIM_DEFAULT_TEMP_C;

/* Private function definitions ****************************************************/

/**
 * @brief Generate a random delta in [-max_delta, +max_delta], in degC.
 */
static float __temp_sim_random_delta(void)
{
    uint32_t span = (uint32_t)(s_config.max_delta * 2.0f * TEMP_SIM_DELTA_STEPS_PER_C);
    if (span == 0) {
        return 0.0f;
    }
    uint32_t drawn = osal_random_generate_range(0, span);
    return ((float)drawn / TEMP_SIM_DELTA_STEPS_PER_C) - s_config.max_delta;
}

/**
 * @brief Produce one simulated reading. Runs on the scheduler (timer) context,
 *        so it must stay short and non-blocking.
 *
 * @param[in] unused Unused parameter.
 */
static void __temp_sim_step(void *unused)
{
    (void)unused;

    s_temperature += __temp_sim_random_delta();
    if (s_temperature < s_config.min_temp) {
        s_temperature = s_config.min_temp;
    } else if (s_temperature > s_config.max_temp) {
        s_temperature = s_config.max_temp;
    }

    float reported = roundf(s_temperature * TEMP_SIM_ROUND_FACTOR) / TEMP_SIM_ROUND_FACTOR;
    OSAL_LOGI(TAG, "!!!HARDWARE!!! Temperature simulated to %.1f degC", reported);

    if (s_config.on_reading != NULL) {
        s_config.on_reading(reported);
    }
}

/* Public function definitions ****************************************************/

osal_err_t temp_sim_start(const temp_sim_config_t *config)
{
    if (config == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (config->min_temp > config->max_temp ||
            config->initial_temp < config->min_temp ||
            config->initial_temp > config->max_temp ||
            config->max_delta < 0.0f ||
            config->period_ms == 0) {
        OSAL_LOGE(TAG, "Invalid simulation configuration");
        return OSAL_ERR_INVALID_ARG;
    }
    if (s_sched != NULL) {
        OSAL_LOGW(TAG, "Simulation already running");
        return OSAL_ERR_INVALID_STATE;
    }

    s_config = *config;
    s_temperature = s_config.initial_temp;

    /* Idempotent; the SDK core also initializes the scheduler. */
    osal_scheduler_init();

    osal_err_t err = osal_scheduler_schedule_task_periodic(&s_sched, s_config.period_ms, __temp_sim_step, NULL);
    if (err != OSAL_ERR_OK) {
        s_sched = NULL;
        OSAL_LOGE(TAG, "Failed to schedule simulation: %d", (int)err);
        return err;
    }

    OSAL_LOGI(TAG, "Simulation started (temperature=%.1f degC, range=%.1f-%.1f degC, delta=+/-%.1f degC, period=%ums)",
              s_temperature, s_config.min_temp, s_config.max_temp,
              s_config.max_delta, (unsigned)s_config.period_ms);
    return OSAL_ERR_OK;
}

void temp_sim_stop(void)
{
    if (s_sched == NULL) {
        return;
    }
    osal_scheduler_cancel_task(&s_sched);
    s_sched = NULL;
    OSAL_LOGI(TAG, "Simulation stopped");
}

float temp_sim_get_temperature(void)
{
    return s_temperature;
}
