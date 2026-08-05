/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_network_neo.c
 * @brief Implementation of the ESP RainMaker Neo network provisioning.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "app_network_neo.h"

/* Configuration includes */
#include "sdkconfig.h"

/* ESP-IDF includes */
#include "esp_log.h"

/* Constants **********************************************************************/

/* Tag for logging */
static const char *TAG = "app_network_neo";

/* Set the PoP type */
#if CONFIG_APP_NETWORK_POP_TYPE_RANDOM
#define APP_NETWORK_POP_TYPE ((app_network_pop_type_t) POP_TYPE_RANDOM)
#elif CONFIG_APP_NETWORK_POP_TYPE_MAC
#define APP_NETWORK_POP_TYPE ((app_network_pop_type_t) POP_TYPE_MAC)
#elif CONFIG_APP_NETWORK_POP_TYPE_NONE
#define APP_NETWORK_POP_TYPE ((app_network_pop_type_t) POP_TYPE_NONE)
#else
#error "Invalid PoP type configured"
#endif

/**
 * @brief RainMaker Neo manufacturer data prefix.
 */
static const app_network_mfg_data_prefix_t __neo_mfg_data_prefix = {
    .header = {MFG_DATA_HEADER},
    .app_id = {'R', 'M', 'N'},
    .version = 'G',
    .customer_id = {MFG_DATA_CUSTOMER_ID},
};

/* Public function definitions ******************************************************/

osal_err_t app_network_set_custom_mfg_data_neo(uint16_t device_type, uint8_t device_subtype)
{
    /* Set the RMNG manufacturer data prefix */
    esp_err_t err = app_network_set_custom_mfg_data_prefix(&__neo_mfg_data_prefix);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set the RMNG manufacturer data prefix: %s", esp_err_to_name(err));
        return OSAL_ERR_FAIL;
    }

    /* Set the custom manufacturer data */
    err = app_network_set_custom_mfg_data(device_type, device_subtype);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set the custom manufacturer data: %s", esp_err_to_name(err));
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}

osal_err_t app_network_start_neo(void)
{
    /* Start the network */
    esp_err_t err = app_network_start(APP_NETWORK_POP_TYPE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the network: %s", esp_err_to_name(err));
        return OSAL_ERR_FAIL;
    }

#if !CONFIG_APP_NETWORK_ASYNCHRONOUS_CONNECTION
    /* Wait for network provisioning completion */
    if (app_network_wait_for_network_prov_ended() != ESP_OK) {
        return OSAL_ERR_FAIL;
    }
#endif
    return OSAL_ERR_OK;
}
