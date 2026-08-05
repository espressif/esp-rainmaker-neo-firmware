/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file fan_sim.h
 * @brief Fan LED status-animation task.
 *
 * Drives an RGB-LED "status" animation from the current fan state: while the
 * fan is powered the hue advances at a rate set by the speed, and while it is
 * also swinging the brightness "breathes" up and down. The animation owns the
 * fan power/swing/speed state; each step it invokes an @ref
 * fan_sim_frame_cb_t so the caller can push the computed hue/brightness to
 * hardware however it likes (drive the LED directly, or stash it and apply it
 * only while the fan is the focused device).
 */

#ifndef __FAN_SIM_H__
#define __FAN_SIM_H__

/* Includes ****************************************************************/

/* Standard includes */
#include <stdbool.h>
#include <stdint.h>

/* Platform includes */
#include "osal_err.h"

/* Constants ****************************************************************/

/** Default initial hue/brightness (HSV). Used to seed the animation and can
 *  be used to seed the caller's LED indicator state. */
#define FAN_SIM_DEFAULT_HUE         240
#define FAN_SIM_DEFAULT_BRIGHTNESS  10

/* Types ****************************************************************/

/**
 * @brief Callback invoked from the animation task on every step while the fan
 *        is powered, carrying the freshly computed HSV hue and brightness.
 *        Use it to drive a hardware indicator (e.g. an LED). Pass NULL if not
 *        needed.
 *
 * @param[in] hue        The new hue, in degrees [0, 360).
 * @param[in] brightness The new brightness, in percent [0, 100].
 */
typedef void (*fan_sim_frame_cb_t)(uint16_t hue, uint8_t brightness);

/**
 * @brief Configuration for the fan LED animation.
 */
typedef struct {
    fan_sim_frame_cb_t on_frame;   /**< Fired on every animation step while powered. */
    bool     initial_power;        /**< Initial fan power state. */
    bool     initial_swing;        /**< Initial fan swing state. */
    int      initial_speed;        /**< Initial fan speed. */
    uint16_t initial_hue;          /**< Initial hue in degrees. */
    uint8_t  initial_brightness;   /**< Initial brightness in percent. */
    uint32_t task_stack_depth;     /**< Stack depth (in words on FreeRTOS, ignored on POSIX). */
    uint32_t task_priority;        /**< Task priority. */
} fan_sim_config_t;

/* Public function declarations ****************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the fan LED animation task. The configuration is copied
 *        internally; the caller's struct does not need to outlive this call.
 *
 * @param[in] config Pointer to a configuration struct.
 *
 * @return OSAL_ERR_OK on success, error code otherwise.
 */
osal_err_t fan_sim_start(const fan_sim_config_t *config);

/**
 * @brief Stop and delete the fan LED animation task.
 */
void fan_sim_stop(void);

/**
 * @brief Set the fan power state consulted by the animation.
 */
void fan_sim_set_power(bool power);

/**
 * @brief Set the fan swing state consulted by the animation.
 */
void fan_sim_set_swing(bool swing);

/**
 * @brief Set the fan speed consulted by the animation (drives the hue rate).
 */
void fan_sim_set_speed(int speed);

/**
 * @brief Get the current fan power state.
 */
bool fan_sim_get_power(void);

/**
 * @brief Get the current fan swing state.
 */
bool fan_sim_get_swing(void);

/**
 * @brief Get the current fan speed.
 */
int fan_sim_get_speed(void);

#ifdef __cplusplus
}
#endif

#endif /* __FAN_SIM_H__ */
