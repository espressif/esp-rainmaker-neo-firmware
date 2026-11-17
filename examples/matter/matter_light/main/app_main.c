/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>

#include "app_entry.h"

#include <osal_storage.h>
#include <app_network_neo.h>
#include <app_event_loop.h>

#include <esp_rmaker_core.h>
#include <esp_rmaker_credentials_access.h>
#include <esp_rmaker_ota.h>

/* Matter mirror */
#include <esp_rmaker_matter_mirror.h>
#include <esp_rmaker_matter_standard_params.h>

#include "app_priv.h"

/* Configuration includes */
#include "sdkconfig.h"

#if CONFIG_ESP_RMAKER_NEO_ASSISTED_CLAIM
#error "Assisted claiming stores the credentials during provisioning, which here runs after node init has read them. Use a pre-claimed factory partition."
#endif

static const char *TAG = "app_main";

esp_rmaker_device_t *light_device;

/* Bulk write callback to handle commands received from the RainMaker Neo cloud - and,
 * via the Matter mirror, from Matter controllers (src = ESP_RMAKER_REQ_SRC_EXTERNAL). */
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
        osal_err_t err = OSAL_ERR_FAIL;

        /* Name parameter should be handled here, if using the bulk write callback. */
        if (strcmp(type, ESP_RMAKER_PARAM_NAME) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for %s", val.val.s, esp_rmaker_param_get_id(param));
            err = OSAL_ERR_OK;
        } else if (strcmp(type, ESP_RMAKER_PARAM_POWER) == 0) {
            OSAL_LOGI(TAG, "Received value = %s for Power", val.val.b ? "true" : "false");
            err = app_driver_set_power(val.val.b);
        } else if (strcmp(type, ESP_RMAKER_PARAM_BRIGHTNESS) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for Brightness", val.val.i);
            err = app_driver_set_brightness(val.val.i);
        } else if (strcmp(type, ESP_RMAKER_PARAM_CCT) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for CCT", val.val.i);
            err = app_driver_set_cct(val.val.i);
            if (err == OSAL_ERR_OK) {
                app_driver_set_light_mode(ESP_RMAKER_LIGHT_MODE_CCT);
            }
        } else if (strcmp(type, ESP_RMAKER_PARAM_HUE) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for Hue", val.val.i);
            err = app_driver_set_hue(val.val.i);
            if (err == OSAL_ERR_OK) {
                app_driver_set_light_mode(ESP_RMAKER_LIGHT_MODE_HSV);
            }
        } else if (strcmp(type, ESP_RMAKER_PARAM_SATURATION) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for Saturation", val.val.i);
            err = app_driver_set_saturation(val.val.i);
            if (err == OSAL_ERR_OK) {
                app_driver_set_light_mode(ESP_RMAKER_LIGHT_MODE_HSV);
            }
        } else if (strcmp(type, ESP_RMAKER_PARAM_LIGHT_MODE) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for Light Mode", val.val.i);
            err = app_driver_set_light_mode((esp_rmaker_light_mode_t)val.val.i);
        } else if (strcmp(type, ESP_RMAKER_PARAM_IDENTIFY) == 0) {
            OSAL_LOGI(TAG, "Received value = %d for Identify", val.val.i);
            err = app_driver_identify(ESP_RMAKER_MATTER_MIRROR_IDENTIFY_PLAIN, val.val.i);
        }

        if (err == OSAL_ERR_OK) {
            esp_rmaker_param_update(param, val);
        }
    }

    return ESP_RMAKER_OK;
}

/* Identify requests from the fabric: the light renders the Identify effects itself, so it takes
 * them here rather than through the identify param, which carries a duration alone. Declining a
 * request (an effect this light has no pattern for) falls back to that param. */
static esp_rmaker_error_t identify_cb(const char *device_id, esp_rmaker_matter_mirror_identify_effect_t effect_id,
                                      uint8_t effect_variant, int32_t seconds)
{
    (void)device_id; /* one mirrored device on this node */
    (void)effect_variant;
    return (app_driver_identify(effect_id, seconds) == OSAL_ERR_OK) ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
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
    /* Initialise the RainMaker Neo serial console. */
    esp_rmaker_console_init();

    /* Initialize application-specific hardware drivers and set the initial state. */
    app_driver_init();

    /* Initialize NVS and the network stack. */
    osal_storage_init(NULL);
    app_network_init();

    /* Use the Matter DAC as the MQTT client credentials, with the node id taken from its common
     * name. Must precede every init that reads credentials. */
    if (esp_rmaker_credentials_provider_override(esp_rmaker_matter_mirror_get_dac_credentials()) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Could not install the Matter DAC credentials. Aborting!!!");
        return OSAL_ERR_FAIL;
    }

    /* Register the default RainMaker Neo event handler (logs RainMaker Neo/OTA/network events).
     * See the examples/common/app_event_loop component for more details. */
    app_event_loop_register_default_handler();

    /* Initialize the RainMaker Neo agent. */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Light", "light");
    if (!node) {
        OSAL_LOGE(TAG, "Could not initialise node. Aborting!!!");
        return OSAL_ERR_FAIL;
    }

    /* Create a Lightbulb device with the standard name + power parameters, and
     * add brightness + CCT + hue/saturation + light mode. Parameters are seeded
     * from the current driver state. The full color set lowers to a Matter
     * Extended Color Light (0x010D) with CT + HS features and derived XY. */
    light_device = esp_rmaker_lightbulb_device_create("Light", NULL, app_driver_get_power());
    esp_rmaker_device_add_bulk_cb(light_device, bulk_write_cb, NULL);
    esp_rmaker_device_add_param(light_device, esp_rmaker_brightness_param_create(ESP_RMAKER_DEF_BRIGHTNESS_ID, app_driver_get_brightness()));
    esp_rmaker_device_add_param(light_device, esp_rmaker_cct_param_create(ESP_RMAKER_DEF_CCT_ID, app_driver_get_cct()));
    esp_rmaker_device_add_param(light_device, esp_rmaker_hue_param_create(ESP_RMAKER_DEF_HUE_ID, app_driver_get_hue()));
    esp_rmaker_device_add_param(light_device, esp_rmaker_saturation_param_create(ESP_RMAKER_DEF_SATURATION_ID, app_driver_get_saturation()));
    /* Light mode is a dropdown of bare integers; without labels a client can only offer
     * "1" and "2". The labels match what the Matter mapping declares for
     * esp.param.light-mode, so this node and a controller-derived one read identically. */
    esp_rmaker_param_t *light_mode_param = esp_rmaker_light_mode_param_create(ESP_RMAKER_DEF_LIGHT_MODE_ID, app_driver_get_light_mode(), false);
    const esp_rmaker_param_value_label_t light_modes[] = {
        { esp_rmaker_int(ESP_RMAKER_LIGHT_MODE_HSV), "HSV" },
        { esp_rmaker_int(ESP_RMAKER_LIGHT_MODE_CCT), "CCT" },
    };
    esp_rmaker_param_add_valid_values(light_mode_param, light_modes, 2);
    esp_rmaker_device_add_param(light_device, light_mode_param);
    /* Identify: the cloud/app path into the same LED patterns the fabric drives. */
    esp_rmaker_device_add_param(light_device, esp_rmaker_identify_param_create(ESP_RMAKER_DEF_IDENTIFY_ID));
    esp_rmaker_node_add_device(node, light_device);

    /* Render Identify/TriggerEffect on the LED instead of flattening every effect to a duration.
     * Registered before the mirror is enabled, so the first request already reaches it. */
    esp_rmaker_matter_mirror_register_identify_handler(identify_cb);

    /* Mirror the node onto Matter: builds the endpoint/cluster tree from the
     * devices/params above and starts the Matter stack (on-network commissioning). */
    if (esp_rmaker_matter_mirror_enable(node, NULL) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to enable the Matter mirror. Aborting!!!");
        return OSAL_ERR_FAIL;
    }

    /* The Matter StartUp state has been applied by now, so release the LED. Held dark until here
     * so the boot state is the only one the user ever sees. */
    app_driver_mark_ready();

#if ESP_RMAKER_MATTER_MIRROR_RMNG_PROV_REQUIRED
    /* RainMaker Neo provisioning runs only once the Matter stack is up, so the two proceed concurrently:
     * the light takes its Matter start-up state at boot rather than when Wi-Fi connects. Compiled
     * out when the onboarding mode has Matter commissioning bring up the network instead, so the
     * provisioning stack drops out of the image.
     * See the examples/common/app_network component for more details. */
    app_network_provision(NEO_MFG_DATA_DEVICE_TYPE_LIGHT, NEO_MFG_DATA_DEVICE_SUBTYPE_LIGHT);
#endif

    /**
     * Enable optional services.
     * - No local control/on-network challenge response services are needed.
     */

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

#if ESP_RMAKER_MATTER_MIRROR_COMMISSIONING
    /* Wait for onboarding: returns at once once the network is up and nothing is pending, and holds
     * off in the Matter-first mode until commissioning is done and CHIPoBLE has released its
     * memory - the RainMaker Neo agent needs that heap. */
    if (esp_rmaker_matter_mirror_wait_ready(0) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Onboarding did not complete; starting the RainMaker Neo agent anyway.");
    }
#endif

    /* Start the RainMaker Neo agent. */
    esp_rmaker_start();

    OSAL_LOGI(TAG, "Matter-mirror light ready.");
    return OSAL_ERR_OK;
}
