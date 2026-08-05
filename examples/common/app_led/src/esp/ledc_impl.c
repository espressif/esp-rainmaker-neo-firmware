/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file ledc_impl.c
 * @brief implementation of the app_led.h interface for LEDC.
 */

/* Includes ****************************************************************/

/* Declarations */
#include "app_led_internal.h"

/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ESP-IDF includes */
#include "driver/ledc.h"

/* Preprocessor definitions ****************************************************/

#define IS_ACTIVE_HIGH          0
#define LEDC_LS_TIMER           LEDC_TIMER_0
#define LEDC_LS_MODE            LEDC_LOW_SPEED_MODE

#define LEDC_LS_CH0_GPIO        CONFIG_APP_LED_LEDC_GPIO_NUM_R
#define LEDC_LS_CH1_GPIO        CONFIG_APP_LED_LEDC_GPIO_NUM_G
#define LEDC_LS_CH2_GPIO        CONFIG_APP_LED_LEDC_GPIO_NUM_B

#define LEDC_NUM_CHANNELS       (3)
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY_MAX           (8192 - 1) // (2 ** 13) - 1
#define LEDC_FREQUENCY          (5000) // Frequency in Hertz. Set frequency at 5 kHz

/* Global variables ****************************************************************/

/**
 * Prepare individual configuration
 * for each channel of LED Controller
 * by selecting:
 * - controller's channel number
 * - output duty cycle, set initially to 0
 * - GPIO number where LED is connected to
 * - speed mode, either high or low
 * - timer servicing selected channel
 *   Note: if different channels use one timer,
 *         then frequency and bit_num of these channels
 *         will be the same
 */
static ledc_channel_config_t ledc_channel[LEDC_NUM_CHANNELS] = {
    {
        .channel    = LEDC_CHANNEL_0,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH0_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
    },
    {
        .channel    = LEDC_CHANNEL_1,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH1_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
    },
    {
        .channel    = LEDC_CHANNEL_2,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH2_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
    },
};

/* Private function declarations ****************************************************/

/**
 * @brief Set the value of a channel.
 *
 * @param[in] channel_num Channel number.
 * @param[in] value Value to set (0-255).
 */
static void __ledc_set_channel(int channel_num, uint8_t value);

/* Private function definitions ****************************************************/

static void __ledc_set_channel(int channel_num, uint8_t value)
{
    uint32_t duty = value * LEDC_DUTY_MAX / 255;
    if (!IS_ACTIVE_HIGH) {
        duty = LEDC_DUTY_MAX - duty;
    }
    ledc_set_duty(ledc_channel[channel_num].speed_mode, ledc_channel[channel_num].channel, duty);
    ledc_update_duty(ledc_channel[channel_num].speed_mode, ledc_channel[channel_num].channel);
}

/* Public function definitions ****************************************************/

osal_err_t app_led_internal_init(void)
{
    /*
     * Prepare and set configuration of timers
     * that will be used by LED Controller
     */
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_DUTY_RES,   // resolution of PWM duty
        .freq_hz = LEDC_FREQUENCY,          // frequency of PWM signal
        .speed_mode = LEDC_LS_MODE,         // timer mode
        .timer_num = LEDC_LS_TIMER,         // timer index
        .clk_cfg = LEDC_AUTO_CLK,           // Auto select the source clock
    };
    // Set configuration of timer0 for high speed channels
    ledc_timer_config(&ledc_timer);

    // Set LED Controller with previously prepared configuration
    for (int ch = 0; ch < LEDC_NUM_CHANNELS; ch++) {
        ledc_channel_config(&ledc_channel[ch]);
    }
    return OSAL_ERR_OK;
}

osal_err_t app_led_internal_set_color_rgb(app_led_color_rgb_t color_rgb)
{
    __ledc_set_channel(0, color_rgb.red);
    __ledc_set_channel(1, color_rgb.green);
    __ledc_set_channel(2, color_rgb.blue);
    return OSAL_ERR_OK;
}
