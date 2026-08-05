/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>

#include "app_entry.h"

#include <osal_storage.h>
#include <app_network_neo.h>
#include <app_event_loop.h>

#include <esp_rmaker_core.h>
#include <esp_rmaker_ota.h>

#include "app_priv.h"

/* Configuration includes */
#include "sdkconfig.h"

static const char *TAG = "app_main";

esp_rmaker_device_t *light_device;

/* Bulk write callback to handle commands received from the RMNG cloud.
 * All writable parameters of one request are delivered together, so the
 * light-mode auto-switch and force-power-on logic can weigh every change at once. */
static esp_rmaker_error_t bulk_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
                                        uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        OSAL_LOGI(TAG, "Received write request via : %s", esp_rmaker_req_src_to_string(ctx->src));
    }

    bool hsv_change = false;
    bool cct_change = false;
    bool force_power_on = false;

    for (uint8_t i = 0; i < count; i++) {
        const esp_rmaker_param_t *param = write_req[i].param;
        const esp_rmaker_param_val_t val = write_req[i].val;
        const char *type = esp_rmaker_param_get_type(param);
        osal_err_t err = OSAL_ERR_FAIL;

        /* Name parameter should be handled here, if using the bulk write callback. */
        if (strcmp(type, ESP_RMAKER_PARAM_NAME) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for %s", val.val.s, esp_rmaker_param_get_id(param));
            err = OSAL_ERR_OK;
        } else if (strcmp(type, ESP_RMAKER_PARAM_POWER) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for Power", val.val.b ? "true" : "false");
            err = app_driver_set_power(val.val.b);
        } else if (strcmp(type, ESP_RMAKER_PARAM_HUE) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for Hue", val.val.i);
            err = app_driver_set_hue(val.val.i);
            hsv_change = true;
        } else if (strcmp(type, ESP_RMAKER_PARAM_SATURATION) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for Saturation", val.val.i);
            err = app_driver_set_saturation(val.val.i);
            hsv_change = true;
        } else if (strcmp(type, ESP_RMAKER_PARAM_BRIGHTNESS) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for Brightness", val.val.i);
            err = app_driver_set_brightness(val.val.i);
            /* A non-zero brightness implies the light should be on. */
            force_power_on = (val.val.i > 0);
        } else if (strcmp(type, ESP_RMAKER_PARAM_CCT) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for CCT", val.val.i);
            err = app_driver_set_cct(val.val.i);
            cct_change = true;
        } else if (strcmp(type, ESP_RMAKER_PARAM_LIGHT_MODE) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for Light Mode", val.val.i);
            err = app_driver_set_light_mode((esp_rmaker_light_mode_t)val.val.i);
        }

        if (err == OSAL_ERR_OK) {
            esp_rmaker_param_update(param, val);
        }
    }

    /* Auto-switch light mode when only one color space changed. If both HSV and
     * CCT changed in the same request, keep the current mode. */
    if (hsv_change ^ cct_change) {
        esp_rmaker_light_mode_t mode = app_driver_get_light_mode();
        esp_rmaker_light_mode_t new_mode = ESP_RMAKER_LIGHT_MODE_INVALID;
        if (hsv_change && mode != ESP_RMAKER_LIGHT_MODE_HSV) {
            new_mode = ESP_RMAKER_LIGHT_MODE_HSV;
        } else if (cct_change && mode != ESP_RMAKER_LIGHT_MODE_CCT) {
            new_mode = ESP_RMAKER_LIGHT_MODE_CCT;
        }
        if (new_mode != ESP_RMAKER_LIGHT_MODE_INVALID && app_driver_set_light_mode(new_mode) == OSAL_ERR_OK) {
            esp_rmaker_param_update(
                esp_rmaker_device_get_param_by_type(light_device, ESP_RMAKER_PARAM_LIGHT_MODE),
                esp_rmaker_int(new_mode));
        }
    }

    /* Any color change in the active mode implies the light should be on. */
    esp_rmaker_light_mode_t mode = app_driver_get_light_mode();
    force_power_on |= (hsv_change && mode == ESP_RMAKER_LIGHT_MODE_HSV) ||
                      (cct_change && mode == ESP_RMAKER_LIGHT_MODE_CCT);
    if (!app_driver_get_power() && force_power_on && app_driver_set_power(true) == OSAL_ERR_OK) {
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(light_device, ESP_RMAKER_PARAM_POWER),
            esp_rmaker_bool(true));
    }

    return ESP_RMAKER_OK;
}

/* OTA diagnostics: invoked during OTA enable, and again after MQTT connects when
 * CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is set. Returning failure triggers a rollback. */
static esp_rmaker_ota_diag_status_t ota_diag_fn(esp_rmaker_ota_diag_priv_t *ota_diag_priv, void *priv)
{
    switch (ota_diag_priv->state) {
    case OTA_DIAG_STATE_INIT:
        OSAL_LOGI(TAG, "OTA diagnostics during OTA enable");
        return OTA_DIAG_STATUS_SUCCESS;
    case OTA_DIAG_STATE_POST_MQTT:
        OSAL_LOGI(TAG, "OTA diagnostics after MQTT connected");
        return OTA_DIAG_STATUS_SUCCESS;
    default:
        OSAL_LOGE(TAG, "Unknown OTA diagnostic state: %d", ota_diag_priv->state);
        return OTA_DIAG_STATUS_FAIL;
    }
}

/* Abstracted main function.
 * For how ESP-IDF and POSIX entry points use this, see the examples/common/app_entry component. */
osal_err_t app_run(void)
{
    /* Initialise the RMNG serial console. */
    esp_rmaker_console_init();

    /* Initialize application-specific hardware drivers and set the initial state. */
    app_driver_init();

    /* Initialize NVS and the network stack. */
    osal_storage_init(NULL);
    app_network_init();

    /* RMNG provisioning runs before node init.
     * See the examples/common/app_network component for more details. */
    app_network_provision(NEO_MFG_DATA_DEVICE_TYPE_LIGHT, NEO_MFG_DATA_DEVICE_SUBTYPE_LIGHT);

    /* Register the default RMNG event handler (logs RMNG/OTA/network events).
     * See the examples/common/app_event_loop component for more details. */
    app_event_loop_register_default_handler();

    /* Initialize the RMNG agent. */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Light", "light");
    if (!node) {
        OSAL_LOGE(TAG, "Could not initialise node. Aborting!!!");
        return OSAL_ERR_FAIL;
    }

    /* Create a Lightbulb device and add the standard name + color parameters.
     * Parameters are seeded from the current driver state, so any change made
     * before the node came up (e.g. a boot-button press) is carried into the
     * data model instead of being reset to the compile-time defaults. */
    light_device = esp_rmaker_lightbulb_device_create("Light", NULL, app_driver_get_power());
    esp_rmaker_device_add_bulk_cb(light_device, bulk_write_cb, NULL);
    esp_rmaker_device_add_param(light_device, esp_rmaker_brightness_param_create(ESP_RMAKER_DEF_BRIGHTNESS_ID, app_driver_get_brightness()));
    esp_rmaker_device_add_param(light_device, esp_rmaker_hue_param_create(ESP_RMAKER_DEF_HUE_ID, app_driver_get_hue()));
    esp_rmaker_device_add_param(light_device, esp_rmaker_saturation_param_create(ESP_RMAKER_DEF_SATURATION_ID, app_driver_get_saturation()));
    esp_rmaker_device_add_param(light_device, esp_rmaker_cct_param_create(ESP_RMAKER_DEF_CCT_ID, app_driver_get_cct()));
    esp_rmaker_device_add_param(light_device, esp_rmaker_light_mode_param_create(ESP_RMAKER_DEF_LIGHT_MODE_ID, app_driver_get_light_mode(), false));
    esp_rmaker_node_add_device(node, light_device);

    /* Enable optional services. */
#if CONFIG_ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE
    /* On-Network Challenge Response: node-group association via an HTTP server on the node. */
    esp_rmaker_chal_resp_service_enable();
#else
    /* Local Control: control the device without internet via an HTTP server on the node. */
    esp_rmaker_local_ctrl_service_enable();
#endif
    /* Timezone service. */
    esp_rmaker_timezone_service_enable();

    /* System service: remote reboot / network-reset / factory-reset. */
    esp_rmaker_system_serv_config_t system_serv_config = {
        .flags = SYSTEM_SERV_FLAGS_ALL,
        .reboot_seconds = 2,
        .reset_seconds = 2,
        .reset_reboot_seconds = 2,
        .network_reset_fn = app_network_reset_credentials,
    };
    esp_rmaker_system_service_enable(&system_serv_config);

    /* Enable OTA before starting the agent. */
    esp_rmaker_ota_config_t ota_config = {
        .ota_cb = NULL,             /* Use the default OTA callback. */
        .ota_diag = ota_diag_fn,    /* OTA rollback diagnostics. */
        .priv = NULL,
    };
    esp_rmaker_ota_enable(&ota_config);

    /* Start the RMNG agent. */
    esp_rmaker_start();

    OSAL_LOGI(TAG, "Light ready.");
    return OSAL_ERR_OK;
}
