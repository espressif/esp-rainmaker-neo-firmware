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

/* Four devices on a single node. */
esp_rmaker_device_t *light_device;
esp_rmaker_device_t *fan_device;
esp_rmaker_device_t *switch_device;
esp_rmaker_device_t *temp_sensor_device;

/* The temperature sensor's read-only parameter, reported by the driver. */
esp_rmaker_param_t *temp_sensor_temperature_param;

/* Bulk write callback for the light (power + brightness). Each device gets its
 * own bulk callback; RMNG delivers all parameters of one request together. */
static esp_rmaker_error_t light_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
        uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        OSAL_LOGI(TAG, "Light write via : %s", esp_rmaker_req_src_to_string(ctx->src));
    }

    bool force_power_on = false;

    for (uint8_t i = 0; i < count; i++) {
        const esp_rmaker_param_t *param = write_req[i].param;
        const esp_rmaker_param_val_t val = write_req[i].val;
        const char *type = esp_rmaker_param_get_type(param);
        osal_err_t err = OSAL_ERR_FAIL;

        /* Name parameter should be handled here, if using the bulk write callback. */
        if (strcmp(type, ESP_RMAKER_PARAM_NAME) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for Light Name", val.val.s);
            err = OSAL_ERR_OK;
        } else if (strcmp(type, ESP_RMAKER_PARAM_POWER) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for Light Power", val.val.b ? "true" : "false");
            err = app_light_set_power(val.val.b);
        } else if (strcmp(type, ESP_RMAKER_PARAM_BRIGHTNESS) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for Light Brightness", val.val.i);
            err = app_light_set_brightness(val.val.i);
            /* A non-zero brightness implies the light should be on. */
            force_power_on = (val.val.i > 0);
        }

        if (err == OSAL_ERR_OK) {
            esp_rmaker_param_update(param, val);
        }
    }

    if (!app_light_get_power() && force_power_on && app_light_set_power(true) == OSAL_ERR_OK) {
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(light_device, ESP_RMAKER_PARAM_POWER),
            esp_rmaker_bool(true));
    }

    return ESP_RMAKER_OK;
}

/* Bulk write callback for the fan (power + swing + speed). */
static esp_rmaker_error_t fan_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
                                       uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        OSAL_LOGI(TAG, "Fan write via : %s", esp_rmaker_req_src_to_string(ctx->src));
    }
    for (uint8_t i = 0; i < count; i++) {
        const esp_rmaker_param_t *param = write_req[i].param;
        const esp_rmaker_param_val_t val = write_req[i].val;
        const char *type = esp_rmaker_param_get_type(param);
        osal_err_t err = OSAL_ERR_FAIL;

        if (strcmp(type, ESP_RMAKER_PARAM_NAME) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for Fan Name", val.val.s);
            err = OSAL_ERR_OK;
        } else if (strcmp(type, ESP_RMAKER_PARAM_POWER) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for Fan Power", val.val.b ? "true" : "false");
            err = app_fan_set_power(val.val.b);
        } else if (strcmp(type, ESP_RMAKER_PARAM_DIRECTION) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for Fan Swing", val.val.b ? "true" : "false");
            err = app_fan_set_swing(val.val.b);
        } else if (strcmp(type, ESP_RMAKER_PARAM_SPEED) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for Fan Speed", val.val.i);
            err = app_fan_set_speed(val.val.i);
        }

        if (err == OSAL_ERR_OK) {
            esp_rmaker_param_update(param, val);
        }
    }
    return ESP_RMAKER_OK;
}

/* Bulk write callback for the switch (power). */
static esp_rmaker_error_t switch_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
        uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        OSAL_LOGI(TAG, "Switch write via : %s", esp_rmaker_req_src_to_string(ctx->src));
    }
    for (uint8_t i = 0; i < count; i++) {
        const esp_rmaker_param_t *param = write_req[i].param;
        const esp_rmaker_param_val_t val = write_req[i].val;
        const char *type = esp_rmaker_param_get_type(param);
        osal_err_t err = OSAL_ERR_FAIL;

        if (strcmp(type, ESP_RMAKER_PARAM_NAME) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for Switch Name", val.val.s);
            err = OSAL_ERR_OK;
        } else if (strcmp(type, ESP_RMAKER_PARAM_POWER) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for Switch Power", val.val.b ? "true" : "false");
            err = app_switch_set_power(val.val.b);
        }

        if (err == OSAL_ERR_OK) {
            esp_rmaker_param_update(param, val);
        }
    }
    return ESP_RMAKER_OK;
}

/* Bulk write callback for the temperature sensor. The temperature itself is
 * read-only (driven by the simulation), so only the device name is writable. */
static esp_rmaker_error_t temp_sensor_write_cb(const esp_rmaker_device_t *device,
        const esp_rmaker_param_write_req_t write_req[],
        uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        OSAL_LOGI(TAG, "Temp Sensor write via : %s", esp_rmaker_req_src_to_string(ctx->src));
    }
    for (uint8_t i = 0; i < count; i++) {
        const esp_rmaker_param_t *param = write_req[i].param;
        const esp_rmaker_param_val_t val = write_req[i].val;
        const char *type = esp_rmaker_param_get_type(param);

        if (strcmp(type, ESP_RMAKER_PARAM_NAME) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for Temp Sensor Name", val.val.s);
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

    /* RMNG provisioning runs before node init. A multi-device node advertises no
     * single device type, so pass the NONE sentinel.
     * See the examples/common/app_network component for more details. */
    app_network_provision(NEO_MFG_DATA_DEVICE_TYPE_NONE, NEO_MFG_DATA_DEVICE_SUBTYPE_NONE);

    /* Register the default RMNG event handler (logs RMNG/OTA/network events).
     * See the examples/common/app_event_loop component for more details. */
    app_event_loop_register_default_handler();

    /* Initialize the RMNG agent. */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Multi-Device", "multi-device");
    if (!node) {
        OSAL_LOGE(TAG, "Could not initialise node. Aborting!!!");
        return OSAL_ERR_FAIL;
    }

    /* Every parameter is seeded from the current driver state, so a change made
     * before the node came up (e.g. a boot-button press) is carried into the
     * data model instead of being reset to the compile-time defaults. */

    /* Light: a lightweight lightbulb with power + brightness only. */
    light_device = esp_rmaker_lightbulb_device_create("Light", NULL, app_light_get_power());
    esp_rmaker_device_add_bulk_cb(light_device, light_write_cb, NULL);
    esp_rmaker_device_add_param(light_device, esp_rmaker_brightness_param_create(ESP_RMAKER_DEF_BRIGHTNESS_ID, app_light_get_brightness()));
    esp_rmaker_node_add_device(node, light_device);

    /* Fan: power (implicit) + writable swing + bounded (1-5) speed. */
    fan_device = esp_rmaker_fan_device_create("Fan", NULL, app_fan_get_power());
    esp_rmaker_device_add_bulk_cb(fan_device, fan_write_cb, NULL);
    esp_rmaker_param_t *swing_param = esp_rmaker_param_create("Swing", ESP_RMAKER_PARAM_DIRECTION,
                                      esp_rmaker_bool(app_fan_get_swing()),
                                      PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(swing_param, ESP_RMAKER_UI_TOGGLE);
    esp_rmaker_device_add_param(fan_device, swing_param);
    esp_rmaker_param_t *speed_param = esp_rmaker_speed_param_create("Speed", app_fan_get_speed());
    esp_rmaker_param_add_bounds(speed_param, esp_rmaker_int(FAN_SPEED_MIN), esp_rmaker_int(FAN_SPEED_MAX), esp_rmaker_int(1));
    esp_rmaker_param_add_ui_type(speed_param, ESP_RMAKER_UI_SLIDER);
    esp_rmaker_device_add_param(fan_device, speed_param);
    esp_rmaker_node_add_device(node, fan_device);

    /* Switch: name + power. */
    switch_device = esp_rmaker_device_create("Switch", ESP_RMAKER_DEVICE_SWITCH, NULL);
    esp_rmaker_device_add_bulk_cb(switch_device, switch_write_cb, NULL);
    esp_rmaker_device_add_param(switch_device, esp_rmaker_name_param_create(ESP_RMAKER_DEF_NAME_PARAM_ID, "Switch"));
    esp_rmaker_param_t *switch_power = esp_rmaker_power_param_create(ESP_RMAKER_DEF_POWER_ID, app_switch_get_power());
    esp_rmaker_device_add_param(switch_device, switch_power);
    esp_rmaker_device_assign_primary_param(switch_device, switch_power);
    esp_rmaker_node_add_device(node, switch_device);

    /* Temperature sensor: a single read-only, time-series temperature (float degC)
     * driven by the temp_sim task started in the driver. */
    temp_sensor_device = esp_rmaker_device_create("Temp Sensor", ESP_RMAKER_DEVICE_TEMP_SENSOR, NULL);
    esp_rmaker_device_add_bulk_cb(temp_sensor_device, temp_sensor_write_cb, NULL);
    esp_rmaker_device_add_param(temp_sensor_device, esp_rmaker_name_param_create(ESP_RMAKER_DEF_NAME_PARAM_ID, "Temp Sensor"));
    temp_sensor_temperature_param = esp_rmaker_param_create("Temperature", ESP_RMAKER_PARAM_TEMPERATURE,
                                    esp_rmaker_float(app_temp_sensor_get_temperature()),
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

    OSAL_LOGI(TAG, "Multi-device ready. Short-press cycles focus (Light/Fan/Switch/Temp Sensor), long-press actuates.");
    return OSAL_ERR_OK;
}
