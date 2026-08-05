/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file fan_sim.c
 * @brief Implementation of the fan LED status animation.
 */

/* Includes ****************************************************************/

/* Declarations */
#include "fan_sim.h"

/* Standard includes */
#include <stddef.h>

/* Platform includes */
#include "osal_log.h"
#include "osal_task.h"
#include "osal_ticks.h"

/* Constants ****************************************************************/

#define FAN_SIM_TASK_NAME         "fan_sim"
/** Hue degrees advanced per animation step, per unit of speed. */
#define FAN_SIM_HUE_STEP_PER_SPEED  5
/** Brightness delta per step while "breathing" (swing on). */
#define FAN_SIM_BRIGHTNESS_STEP     5
/** Brightness bounds (percent) for the breathing envelope. */
#define FAN_SIM_MIN_BRIGHTNESS      10
#define FAN_SIM_MAX_BRIGHTNESS      100
/** Period between animation steps. */
#define FAN_SIM_PERIOD_MS           50

/* Tag for logging */
static const char *TAG = "fan_sim";

/* Variables ****************************************************************/

/* Configuration */
static fan_sim_config_t s_config;

/* Task handle */
static osal_task_handle_t s_task = NULL;

/* Live fan state consulted by the animation. */
static bool s_power = false;
static bool s_swing = false;
static int  s_speed = 1;

/* Private function definitions ****************************************************/

/**
 * @brief Fan LED animation task.
 *
 * @param[in] unused Unused parameter.
 */
static void __fan_sim_task(void *unused)
{
    (void)unused;
    uint16_t hue = s_config.initial_hue;
    uint8_t brightness = s_config.initial_brightness;
    bool inc_brightness = true;

    while (1) {
        if (s_power) {
            hue = (hue + s_speed * FAN_SIM_HUE_STEP_PER_SPEED) % 360;
            if (s_swing) {
                if (inc_brightness) {
                    brightness += FAN_SIM_BRIGHTNESS_STEP;
                    if (brightness >= FAN_SIM_MAX_BRIGHTNESS) {
                        brightness = FAN_SIM_MAX_BRIGHTNESS;
                        inc_brightness = false;
                    }
                } else {
                    brightness -= FAN_SIM_BRIGHTNESS_STEP;
                    if (brightness <= FAN_SIM_MIN_BRIGHTNESS) {
                        brightness = FAN_SIM_MIN_BRIGHTNESS;
                        inc_brightness = true;
                    }
                }
            }
            if (s_config.on_frame != NULL) {
                s_config.on_frame(hue, brightness);
            }
            OSAL_LOGD(TAG, "LED animation: hue -> %d, brightness -> %d", hue, brightness);
        }
        osal_task_delay(osal_ticks_from_ms(FAN_SIM_PERIOD_MS));
    }
}

/* Public function definitions ****************************************************/

osal_err_t fan_sim_start(const fan_sim_config_t *config)
{
    if (config == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (s_task != NULL) {
        OSAL_LOGW(TAG, "Animation already running");
        return OSAL_ERR_INVALID_STATE;
    }

    s_config = *config;
    s_power = s_config.initial_power;
    s_swing = s_config.initial_swing;
    s_speed = s_config.initial_speed;

    osal_err_t err = osal_task_create(
                         __fan_sim_task,
                         FAN_SIM_TASK_NAME,
                         s_config.task_stack_depth,
                         NULL,
                         s_config.task_priority,
                         &s_task);
    if (err != OSAL_ERR_OK) {
        s_task = NULL;
        OSAL_LOGE(TAG, "Failed to create animation task: %d", (int)err);
        return err;
    }

    OSAL_LOGI(TAG, "Animation started (power=%s, swing=%s, speed=%d)",
              s_power ? "ON" : "OFF", s_swing ? "ON" : "OFF", s_speed);
    return OSAL_ERR_OK;
}

void fan_sim_stop(void)
{
    if (s_task == NULL) {
        return;
    }
    osal_task_delete(s_task);
    s_task = NULL;
    OSAL_LOGI(TAG, "Animation stopped");
}

void fan_sim_set_power(bool power)
{
    s_power = power;
}

void fan_sim_set_swing(bool swing)
{
    s_swing = swing;
}

void fan_sim_set_speed(int speed)
{
    s_speed = speed;
}

bool fan_sim_get_power(void)
{
    return s_power;
}

bool fan_sim_get_swing(void)
{
    return s_swing;
}

int fan_sim_get_speed(void)
{
    return s_speed;
}
