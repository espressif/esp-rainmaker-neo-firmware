/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file local_ctrl/prov_local_ctrl.c
 * @brief Local control endpoints on the provisioning service.
 */

/* Includes *******************************************************/

/* Declaration includes. */
#include "local_ctrl/prov.h"

/* Standard includes. */
#include <stddef.h>

/* Platform common includes. */
#include "osal_log.h"

/* Local control handler includes. */
#include "local_ctrl/endpoints.h"

/* Prov registry includes. */
#include "prov_helpers.h"

/* Variables *******************************************************/

static const char *TAG = "rmng_prov_local_ctrl";

/* Private function declarations *******************************************************/

static void __prov_local_ctrl_on_prov_end(void *priv_data);

/* Variables, continued *******************************************************/

static const prov_endpoint_t get_params_endpoint = {
    .name = RMAKER_LOCAL_CTRL_GET_PARAMS_ENDPOINT,
    .handler = esp_rmaker_local_ctrl_get_params_ep_handler,
    .priv_data = NULL,
    .on_prov_end = __prov_local_ctrl_on_prov_end,
};

static const prov_endpoint_t set_params_endpoint = {
    .name = RMAKER_LOCAL_CTRL_SET_PARAMS_ENDPOINT,
    .handler = esp_rmaker_local_ctrl_set_params_ep_handler,
    .priv_data = NULL,
};

static const prov_endpoint_t get_config_endpoint = {
    .name = RMAKER_LOCAL_CTRL_GET_CONFIG_ENDPOINT,
    .handler = esp_rmaker_local_ctrl_get_config_ep_handler,
    .priv_data = NULL,
};

static const prov_endpoint_t *const endpoints[] = {
    &get_params_endpoint,
    &set_params_endpoint,
    &get_config_endpoint,
};

static const prov_capability_t local_ctrl_capability = {
    .label = PROV_APP_LABEL_RMAKER_EXTRA,
    .capability = RMAKER_PROV_LOCAL_CTRL_CAPABILITY,
};

/* Private function definitions *******************************************************/

/** A session that ended mid-transfer leaves the get-data cache behind. */
static void __prov_local_ctrl_on_prov_end(void *priv_data)
{
    (void)priv_data;
    esp_rmaker_local_ctrl_endpoints_free_data();
}

/* Public function definitions *******************************************************/

esp_rmaker_error_t esp_rmaker_prov_local_ctrl_init(void)
{
    esp_rmaker_error_t err = esp_rmaker_local_ctrl_endpoints_init();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (prov_add_capability(&local_ctrl_capability) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to add the %s capability.", local_ctrl_capability.capability);
        return ESP_RMAKER_FAIL;
    }
    for (size_t i = 0; i < sizeof(endpoints) / sizeof(endpoints[0]); i++) {
        if (prov_add_endpoint(endpoints[i]) != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to add the %s endpoint.", endpoints[i]->name);
            while (i-- > 0) {
                prov_del_endpoint(endpoints[i]);
            }
            prov_del_capability(&local_ctrl_capability);
            return ESP_RMAKER_FAIL;
        }
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_prov_local_ctrl_deinit(void)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    for (size_t i = 0; i < sizeof(endpoints) / sizeof(endpoints[0]); i++) {
        if (prov_del_endpoint(endpoints[i]) != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to remove the %s endpoint.", endpoints[i]->name);
            err = ESP_RMAKER_FAIL;
        }
    }
    if (prov_del_capability(&local_ctrl_capability) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to remove the %s capability.", local_ctrl_capability.capability);
        err = ESP_RMAKER_FAIL;
    }
    esp_rmaker_local_ctrl_endpoints_free_data();
    return err;
}
