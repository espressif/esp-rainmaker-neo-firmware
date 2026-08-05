/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file wifi_internal.c
 * @brief Internal functions for the Wi-Fi network.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "app_network_neo.h"

/* ESP-IDF includes */
#include "esp_wifi.h"

/* Public function definitions **********************************************************/

/* Reset Wi-Fi credentials */
osal_err_t app_network_reset_credentials(void)
{
    esp_err_t err = esp_wifi_restore();
    /* If the Wi-Fi is not initialized, consider it a success. */
    if (err == ESP_ERR_WIFI_NOT_INIT || err == ESP_OK) {
        return OSAL_ERR_OK;
    }
    return OSAL_ERR_FAIL;
}
