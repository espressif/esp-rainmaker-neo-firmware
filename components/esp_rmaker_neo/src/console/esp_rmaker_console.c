/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_console.c
 * @brief RainMaker Neo SDK serial console entry point.
 *
 * Thin, platform-unified wrapper: initializes the common console (REPL + common commands, provided by
 * the espressif/rmaker_console component) and then registers the RainMaker Neo built-in commands. Identical on
 * ESP-IDF and POSIX thanks to the esp_console-posix shim.
 */


#include "esp_rmaker_console.h"
#include "osal_log.h"

#if CONFIG_ESP_RMAKER_NEO_CONSOLE_ENABLE
#include <esp_rmaker_common_console.h>
#endif

static const char *TAG = "rmng_console";

esp_rmaker_error_t esp_rmaker_console_init(void)
{
#if CONFIG_ESP_RMAKER_NEO_CONSOLE_ENABLE
    esp_err_t err = esp_rmaker_common_console_init();
    if (err != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to initialize common console: 0x%x", err);
        return (esp_rmaker_error_t)err;
    }
    esp_rmaker_register_commands();
    return ESP_RMAKER_OK;
#else /* !CONFIG_ESP_RMAKER_NEO_CONSOLE_ENABLE */
    OSAL_LOGW(TAG, "RMNG console is not enabled; enable via CONFIG_ESP_RMAKER_NEO_CONSOLE_ENABLE=y");
    return ESP_RMAKER_OK;
#endif /* !CONFIG_ESP_RMAKER_NEO_CONSOLE_ENABLE */
}
