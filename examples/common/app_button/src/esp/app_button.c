/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_button.c
 * @brief App button implementation.
 */

/* Includes ****************************************************************/

/* Declarations */
#include "app_button.h"

/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* espressif/button includes */
#include "button_gpio.h"
#include "iot_button.h"

/* ESP-IDF includes */
#include "osal_err.h"
#include "osal_log.h"

/* Config includes */
#include "sdkconfig.h"

/* Hold-to-reset lives in its own component; see app_reset.h. */
#include "app_reset.h"

/* Preprocessor definitions ****************************************************/

#define GPIO_NUM CONFIG_APP_BUTTON_GPIO_NUM
#ifdef CONFIG_APP_BUTTON_IS_ACTIVE_HIGH
#define IS_ACTIVE_HIGH 1
#else
#define IS_ACTIVE_HIGH 0
#endif

/* Constants ****************************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "app_button";

/* Variables ****************************************************************/

/**
 * @brief Callback to execute the long press.
 */
static app_button_callback_t __button_callback_long_press_action = NULL;

/* Private function declarations ****************************************************/

/**
 * @brief Callback to indicate the long press.
 *
 * @param[in] handle Button handle (unused).
 * @param[in] arg Argument (unused).
 */
static void __button_callback_long_press_indicate(void *handle, void *arg);

/**
 * @brief Callback to execute the long press.
 *
 * @param[in] handle Button handle (unused).
 * @param[in] arg Argument (unused).
 */
static void __button_callback_long_press_execute(void *handle, void *arg);


/* Private function definitions ****************************************************/

static void __button_callback_long_press_indicate(void *handle, void *arg)
{
    OSAL_LOGI(TAG, "Release button now to perform long press action. Keep pressed for network/factory reset.");
}

static void __button_callback_long_press_execute(void *handle, void *arg)
{
    /* The user is holding for a reset, not asking for the long-press action. */
    if (app_reset_hold_in_progress()) {
        return;
    }
    if (__button_callback_long_press_action != NULL) {
        OSAL_LOGI(TAG, "Executing long press action...");
        __button_callback_long_press_action(handle, arg);
    } else {
        OSAL_LOGW(TAG, "No long press action to execute.");
    }
}


/* Public function definitions ****************************************************/

osal_err_t app_button_init(app_button_config_t *config)
{
    /* Validate the configuration. */
    if (config == NULL) {
        OSAL_LOGE(TAG, "Configuration is NULL");
        return OSAL_ERR_INVALID_ARG;
    }

    /* Create GPIO button */
    button_config_t btn_cfg = {
        .short_press_time = (uint16_t)CONFIG_APP_BUTTON_SHORT_PRESS_TIME_MS,
        .long_press_time = (uint16_t)CONFIG_APP_BUTTON_LONG_PRESS_TIME_MS,
    };
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = GPIO_NUM,
        .active_level = IS_ACTIVE_HIGH,
    };
    button_handle_t gpio_btn = NULL;
    esp_err_t err = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &gpio_btn);
    if (gpio_btn == NULL) {
        OSAL_LOGE(TAG, "Failed to create the GPIO button: %s", esp_err_to_name(err));
        return OSAL_ERR_FAIL;
    }

    /* Set the callbacks. */
    if (config->callbacks.on_short_press != NULL) {
        iot_button_register_cb(gpio_btn, BUTTON_SINGLE_CLICK, NULL, config->callbacks.on_short_press, NULL);
    }
    if (config->callbacks.on_long_press != NULL) {
        __button_callback_long_press_action = config->callbacks.on_long_press;
        iot_button_register_cb(gpio_btn, BUTTON_LONG_PRESS_START, NULL, __button_callback_long_press_indicate, NULL);
        iot_button_register_cb(gpio_btn, BUTTON_LONG_PRESS_UP, NULL, __button_callback_long_press_execute, NULL);
    }
    app_reset_button_register(gpio_btn);

    return OSAL_ERR_OK;
}
