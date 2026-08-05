/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_button.h
 * @brief App button interface (OS-agnostic).
 *
 * The callback shape mirrors espressif/button's @c button_cb_t
 * (@c void(void *button_handle, void *usr_data)) so the ESP backend can pass
 * these callbacks straight to @c iot_button_register_cb, while the header itself
 * stays free of any ESP-only include. On POSIX there is no physical button, so
 * the stub backend accepts the same config and does nothing.
 */

#ifndef __APP_BUTTON_H__
#define __APP_BUTTON_H__

/* Includes ****************************************************************/

/* Platform includes */
#include "osal_err.h"

/* Types ****************************************************************/

/**
 * @brief Callback for any button event. Matches espressif/button's button_cb_t.
 */
typedef void (*app_button_callback_t)(void *button_handle, void *usr_data);

/**
 * @brief Callbacks for the button.
 */
typedef struct {
    app_button_callback_t on_short_press; // Callback for short press. NULL if not needed.
    app_button_callback_t on_long_press; // Callback for long press. NULL if not needed.
} app_button_callbacks_t;

/**
 * @brief Configuration for the button.
 */
typedef struct {
    /** Callbacks for the button. */
    app_button_callbacks_t callbacks;
} app_button_config_t;

/* Public function declarations ****************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the button.
 *
 * @param[in] config Configuration for the button.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t app_button_init(app_button_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* __APP_BUTTON_H__ */
