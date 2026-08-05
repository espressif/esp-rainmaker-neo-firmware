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

#include "temp_sim.h"

#include "app_priv.h"

/* Configuration includes */
#include "sdkconfig.h"

static const char *TAG = "app_main";

esp_rmaker_device_t *temp_sensor_device;
esp_rmaker_param_t *temp_sensor_temperature_param;

/* Bulk write callback for commands from the RMNG cloud. The temperature is
 * read-only (driven by the simulation), so only the device name is writable.
 * All writable parameters of one request arrive together. */
static esp_rmaker_error_t bulk_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
                                        uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        OSAL_LOGI(TAG, "Received write request via : %s", esp_rmaker_req_src_to_string(ctx->src));
    }

    for (uint8_t i = 0; i < count; i++) {
        const esp_rmaker_param_t *param = write_req[i].param;
        const esp_rmaker_param_val_t val = write_req[i].val;
        const char *type = esp_rmaker_param_get_type(param);

        /* Name parameter should be handled here, if using the bulk write callback. */
        if (strcmp(type, ESP_RMAKER_PARAM_NAME) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for %s", val.val.s, esp_rmaker_param_get_id(param));
            esp_rmaker_param_update(param, val);
        }
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

    /* RMNG provisioning runs before node init. There is no dedicated temperature
     * sensor manufacturer-data type, so we skip the BLE advertisement filter.
     * See the examples/common/app_network component for more details. */
    app_network_provision(NEO_MFG_DATA_DEVICE_TYPE_NONE, NEO_MFG_DATA_DEVICE_SUBTYPE_NONE);

    /* Register the default RMNG event handler (logs RMNG/OTA/network events).
     * See the examples/common/app_event_loop component for more details. */
    app_event_loop_register_default_handler();

    /* Initialize the RMNG agent. */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Temp Sensor", "temp_sensor");
    if (!node) {
        OSAL_LOGE(TAG, "Could not initialise node. Aborting!!!");
        return OSAL_ERR_FAIL;
    }

    /* Create a Temperature Sensor device with a single read-only, time-series
     * temperature parameter (float degC). It is seeded from the current driver
     * state, so the value reported at node bring-up matches the simulation. */
    temp_sensor_device = esp_rmaker_device_create("Temp Sensor", ESP_RMAKER_DEVICE_TEMP_SENSOR, NULL);
    esp_rmaker_device_add_bulk_cb(temp_sensor_device, bulk_write_cb, NULL);

    temp_sensor_temperature_param = esp_rmaker_param_create("Temperature", ESP_RMAKER_PARAM_TEMPERATURE,
                                    esp_rmaker_float(app_driver_get_temperature()),
                                    PROP_FLAG_READ | PROP_FLAG_TIME_SERIES);
    esp_rmaker_param_add_bounds(temp_sensor_temperature_param, esp_rmaker_float(TEMP_SIM_MIN_TEMP_C),
                                esp_rmaker_float(TEMP_SIM_MAX_TEMP_C), esp_rmaker_float(TEMP_SIM_UI_STEP_C));
    esp_rmaker_device_add_param(temp_sensor_device, temp_sensor_temperature_param);

    esp_rmaker_device_assign_primary_param(temp_sensor_device, temp_sensor_temperature_param);
    esp_rmaker_node_add_device(node, temp_sensor_device);

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

    /* Start the temperature simulation now that the parameter exists. */
    app_driver_start_simulation();

    OSAL_LOGI(TAG, "Temperature sensor ready.");
    return OSAL_ERR_OK;
}
