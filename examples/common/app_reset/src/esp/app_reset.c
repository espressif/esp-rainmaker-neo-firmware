/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_reset.c
 * @brief ESP-IDF hold-to-reset: two long-press thresholds on one button.
 */

/* Declarations */
#include "app_reset.h"

/* Config includes */
#include "sdkconfig.h"

#if CONFIG_APP_RESET_ENABLED
/* Button includes */
#include "iot_button.h"

/* RMNG includes */
#include "esp_rmaker_system_ctrl.h"

/* App network includes */
#include "app_network_neo.h"

/* Platform includes */
#include "osal_log.h"

/* Preprocessor definitions ****************************************************/

#define RESET_REBOOT_S (uint8_t)(CONFIG_APP_RESET_REBOOT_S)

/* Variables *******************************************************************/

static const char *TAG = "app_reset";

/** Set once the hold passes the network-reset threshold. */
static bool __entered_network_reset_indicate = false;

/** Set once the hold passes the factory-reset threshold. */
static bool __entered_factory_reset_indicate = false;

/* Private function definitions ************************************************/

static esp_rmaker_error_t __network_reset_fn(void)
{
    return app_network_reset_credentials();
}

static void __callback_reset_network_indicate(void *handle, void *arg)
{
    OSAL_LOGI(TAG, "Release button now for network reset. Keep pressed for factory reset.");
    __entered_network_reset_indicate = true;
}

static void __callback_reset_network_execute(void *handle, void *arg)
{
    /* The hold went on into factory-reset territory, so this release is not a network reset. */
    if (__entered_factory_reset_indicate) {
        return;
    }

    OSAL_LOGI(TAG, "Executing network reset...");
    /* NULL -> use the network reset function registered below. */
    esp_rmaker_system_ctrl_network_reset(0, RESET_REBOOT_S, NULL);
}

static void __callback_reset_factory_indicate(void *handle, void *arg)
{
    OSAL_LOGI(TAG, "Release button to trigger factory reset.");
    __entered_factory_reset_indicate = true;
}

static void __callback_reset_factory_execute(void *handle, void *arg)
{
    OSAL_LOGI(TAG, "Executing factory reset...");
    /* NULL -> use the network reset function registered below. */
    esp_rmaker_system_ctrl_factory_reset(0, RESET_REBOOT_S, NULL);
}
#endif /* CONFIG_APP_RESET_ENABLED */

/* Public function definitions *************************************************/

osal_err_t app_reset_button_register(button_handle_t btn_handle)
{
#if CONFIG_APP_RESET_ENABLED
    if (btn_handle == NULL) {
        OSAL_LOGE(TAG, "Button handle is NULL");
        return OSAL_ERR_INVALID_ARG;
    }

    button_event_args_t network_reset_args = {
        .long_press.press_time = (uint16_t)CONFIG_APP_RESET_NETWORK_TIME_MS,
    };
    button_event_args_t factory_reset_args = {
        .long_press.press_time = (uint16_t)CONFIG_APP_RESET_FACTORY_TIME_MS,
    };

    /* Register the network reset function so the SDK (and console reset-network command) can
     * trigger it without knowing the application-specific routine. */
    esp_rmaker_system_ctrl_register_network_reset_fn(__network_reset_fn);

    iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_START, &network_reset_args,
                           __callback_reset_network_indicate, NULL);
    iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_UP, &network_reset_args,
                           __callback_reset_network_execute, NULL);
    iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_START, &factory_reset_args,
                           __callback_reset_factory_indicate, NULL);
    iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_UP, &factory_reset_args,
                           __callback_reset_factory_execute, NULL);

    return OSAL_ERR_OK;
#else
    (void) btn_handle;
    return OSAL_ERR_OK;
#endif /* CONFIG_APP_RESET_ENABLED */
}

bool app_reset_hold_in_progress(void)
{
#if CONFIG_APP_RESET_ENABLED
    return __entered_network_reset_indicate;
#else
    return false;
#endif /* CONFIG_APP_RESET_ENABLED */
}
