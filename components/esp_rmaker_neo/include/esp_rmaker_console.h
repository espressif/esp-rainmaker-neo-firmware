/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_console.h
 * @brief RainMaker Neo SDK serial console interface.
 */

#ifndef __ESP_RMAKER_CONSOLE_H__
#define __ESP_RMAKER_CONSOLE_H__

#include "esp_rmaker_error_types.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the RainMaker Neo serial console.
 *
 * Sets up the serial console (REPL) and registers both the common commands (from the
 * `rmaker_console` component) and the RainMaker Neo SDK built-in commands (e.g. `get-node-id`,
 * `node-info`). This is the single entry point an application needs; call it early in
 * app_main()/main().
 *
 * Works identically on ESP-IDF and POSIX: on ESP-IDF the console is backed by the IDF esp_console
 * component, on POSIX by the esp_console-posix shim (a stdin REPL).
 *
 * This entry point is always linkable. When the console is disabled
 * (CONFIG_RMNG_CONSOLE_ENABLED=n) it logs a warning and returns ESP_RMAKER_OK without starting
 * anything, so callers need not guard the call.
 *
 * @return ESP_RMAKER_OK on success, or an error code on failure.
 *
 * @note If the application initializes the console itself, call esp_rmaker_register_commands()
 *       instead to register only the RainMaker Neo built-in commands. Unlike this function,
 *       esp_rmaker_register_commands() is only available when CONFIG_RMNG_CONSOLE_ENABLED=y.
 */
esp_rmaker_error_t esp_rmaker_console_init(void);

#if CONFIG_RMNG_CONSOLE_ENABLED
/**
 * @brief Register the RainMaker Neo SDK built-in console commands.
 *
 * Registers commands such as `get-node-id` and `node-info`. Must be called after the console has
 * been initialized. esp_rmaker_console_init() calls this implicitly.
 */
void esp_rmaker_register_commands(void);
#endif /* CONFIG_RMNG_CONSOLE_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_CONSOLE_H__ */
