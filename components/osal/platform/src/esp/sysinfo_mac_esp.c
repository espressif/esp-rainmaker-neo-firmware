/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sysinfo_mac_esp.c
 * @brief ESP-IDF MAC address implementation.
 */

/* Includes **************************************************************/

/* Declarations */
#include "osal_sysinfo.h"

/* ESP-IDF */
#include "esp_mac.h"

/* Public function definitions **********************************************************/

osal_err_t osal_sysinfo_get_base_mac(uint8_t *mac, size_t mac_len)
{
    if (!mac || mac_len < OSAL_MAC_ADDR_LEN) {
        return OSAL_ERR_INVALID_ARG;
    }
    /* ESP_MAC_BASE is the base MAC for all chips, so this covers Wi-Fi and Thread nodes
     * alike, rather than assuming an interface the target may not have. */
    if (esp_read_mac(mac, ESP_MAC_BASE) != ESP_OK) {
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}
