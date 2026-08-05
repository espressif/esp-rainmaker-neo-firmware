/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_reset.h
 * @brief Hold-to-reset on a physical button.
 *
 * Holding the button past two configurable thresholds triggers a network reset
 * (`CONFIG_APP_RESET_NETWORK_TIME_MS`, default 5 s) and then a factory reset
 * (`CONFIG_APP_RESET_FACTORY_TIME_MS`, default 10 s). Both are **enabled by default** --
 * see `CONFIG_APP_RESET_ENABLED` and the component README.
 *
 * This is the RMNG counterpart of esp-rainmaker's `espressif/rmaker_app_reset`. It cannot
 * be replaced by that component: upstream calls esp_rainmaker's `esp_rmaker_wifi_reset()`
 * / `esp_rmaker_factory_reset()`, which do not exist here; the resets below go through
 * RMNG's own `esp_rmaker_system_ctrl_*` API.
 *
 * The reset thresholds sit on the same button as the application's own long-press action,
 * so a caller that has its own long-press handler must skip it while a reset hold is in
 * progress -- see ::app_reset_hold_in_progress.
 */

#pragma once

/* Standard includes */
#include <stdbool.h>

/* Platform includes */
#include "osal_err.h"

#ifdef ESP_PLATFORM
/* espressif/button: the handle type the reset callbacks are registered against. */
#include "iot_button.h"
#endif /* ESP_PLATFORM */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ESP_PLATFORM
/**
 * @brief Register the hold-to-reset callbacks on an existing button.
 *
 * Registers the network- and factory-reset thresholds on @p btn_handle, and registers the
 * network-reset routine with RMNG so the SDK (and the `reset-network` console command) can
 * trigger it without knowing the application's specifics.
 *
 * Does nothing and returns OSAL_ERR_OK when `CONFIG_APP_RESET_ENABLED` is disabled, so a
 * caller need not guard the call.
 *
 * ESP-IDF only: there is no physical button on the host.
 *
 * @param[in] btn_handle Button to attach the reset thresholds to.
 * @return OSAL_ERR_OK on success, OSAL_ERR_INVALID_ARG if @p btn_handle is NULL,
 *         otherwise an error code.
 */
osal_err_t app_reset_button_register(button_handle_t btn_handle);
#endif /* ESP_PLATFORM */

/**
 * @brief Whether the current button hold has already crossed a reset threshold.
 *
 * A caller with its own long-press action should consult this and skip that action when it
 * returns true: the user is holding for a reset, not asking for the long-press behaviour.
 * Always false on POSIX, and false when `CONFIG_APP_RESET_ENABLED` is disabled.
 *
 * @return true if a network or factory reset hold is in progress.
 */
bool app_reset_hold_in_progress(void);

#ifdef __cplusplus
}
#endif
