/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

/**
 * @file app_main.c
 * @brief Unified (ESP-IDF + POSIX) OTA simulator.
 *
 * A remotely-driven RainMaker Neo node used by the integration-test harness to exercise
 * the OTA flow. It exposes an "OTA Remote" device whose parameters let the test
 * drive OTA diagnostics, mark valid/invalid, kill the node, and observe resume
 * offsets.
 */

/* Standard includes */
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

/* App entry (owns app_main/main; provides app_run + OSAL_LOGx via os headers) */
#include "app_entry.h"

/* Platform includes */
#include "osal_sysinfo.h"
#include "osal_event_loop.h"

/* RMNG core + OTA + services */
#include "esp_rmaker_core.h"
#include "esp_rmaker_system_ctrl.h"
#include "esp_rmaker_standard_services.h"
#include "esp_rmaker_event_loop.h"
#include "esp_rmaker_ota.h"
#include "esp_rmaker_ota_event_loop.h"

/* Configuration */
#include "sdkconfig.h"

#if defined(ESP_PLATFORM)
/* ESP-only network bring-up + RMNG provisioning */
#include "osal_storage.h"
#include "app_network_neo.h"
#endif /* ESP_PLATFORM */

/* Constants: OTA-remote data model ***/

#define OTA_REMOTE_DEVICE_NAME "OTA Remote"
#define OTA_REMOTE_DEVICE_TYPE "esp.device.ota_remote"

#define OTA_REMOTE_FW_VER_PARAM_NAME "FW Version"
#define OTA_REMOTE_FW_VER_PARAM_TYPE "esp.param.fw_ver"

#define OTA_REMOTE_JOB_FW_VER_PARAM_NAME "Job FW Version"
#define OTA_REMOTE_JOB_FW_VER_PARAM_TYPE "esp.param.job_fw_ver"

#define OTA_REMOTE_MARK_PARAM_NAME "Mark"
#define OTA_REMOTE_MARK_PARAM_TYPE "esp.param.mark"

#define OTA_REMOTE_DIAG_STAGE_PARAM_NAME "DStage"
#define OTA_REMOTE_DIAG_STAGE_PARAM_TYPE "esp.param.dstage"

#define OTA_REMOTE_DIAG_STATUS_PARAM_NAME "DStatus"
#define OTA_REMOTE_DIAG_STATUS_PARAM_TYPE "esp.param.dstatus"

#define OTA_REMOTE_DIAG_RETURN_PARAM_TYPE "esp.param.dreturn"
#define OTA_REMOTE_DIAG_RETURN_POST_INIT_PARAM_NAME "DRet Init"
#define OTA_REMOTE_DIAG_RETURN_POST_MQTT_PARAM_NAME "DRet MQTT"

#define OTA_REMOTE_KILLSWITCH_PARAM_NAME "Kill"
#define OTA_REMOTE_KILLSWITCH_PARAM_TYPE "esp.param.kill_switch"

/* Resume offset: byte offset of a resumed download (> 0 only on genuine resume);
 * reset to 0 at each RMAKER_OTA_EVENT_STARTING so stale shadow values are overwritten. */
#define OTA_REMOTE_RESUME_OFFSET_PARAM_NAME "Resume Offset"
#define OTA_REMOTE_RESUME_OFFSET_PARAM_TYPE "esp.param.resume_offset"

/* Delay in seconds before rebooting the node after an OTA update. */
#define OTA_REBOOT_DELAY_SECONDS 3

typedef enum {
    OTA_REMOTE_DIAG_STAGE_MIN = -1,
    OTA_REMOTE_DIAG_STAGE_NONE = 0,
    OTA_REMOTE_DIAG_STAGE_INIT = 1,
    OTA_REMOTE_DIAG_STAGE_POST_MQTT = 2,
    OTA_REMOTE_DIAG_STAGE_MAX
} ota_remote_diag_stage_t;

typedef enum {
    OTA_REMOTE_DIAG_STATUS_MIN = -1,
    OTA_REMOTE_DIAG_STATUS_FAIL = 0,
    OTA_REMOTE_DIAG_STATUS_PENDING = 1,
    OTA_REMOTE_DIAG_STATUS_SUCCESS = 2,
    OTA_REMOTE_DIAG_STATUS_MAX
} ota_remote_diag_status_t;

/* Tag for logging */
static const char *TAG = "ota-sim";

/* Handles for the OTA-remote device and its parameters. */
static struct {
    esp_rmaker_device_t *ota_remote_device;
    esp_rmaker_param_t *fw_ver_param;
    esp_rmaker_param_t *job_fw_ver_param;
    esp_rmaker_param_t *mark_param;
    esp_rmaker_param_t *diag_stage_param;
    esp_rmaker_param_t *diag_status_param;
    struct {
        esp_rmaker_param_t *post_init;
        esp_rmaker_param_t *post_mqtt;
    } diag_return_params;
    esp_rmaker_param_t *killswitch_param;
    esp_rmaker_param_t *resume_offset_param;
} g_handles = {0};

/* Diagnostics status conversions ********************************************/

static esp_rmaker_ota_diag_status_t diag_status_ota_remote_to_rmaker_ota(ota_remote_diag_status_t status)
{
    switch (status) {
    case OTA_REMOTE_DIAG_STATUS_FAIL:    return OTA_DIAG_STATUS_FAIL;
    case OTA_REMOTE_DIAG_STATUS_PENDING: return OTA_DIAG_STATUS_PENDING;
    case OTA_REMOTE_DIAG_STATUS_SUCCESS: return OTA_DIAG_STATUS_SUCCESS;
    default:                             return OTA_DIAG_STATUS_FAIL;
    }
}

static ota_remote_diag_status_t diag_status_rmaker_ota_to_ota_remote(esp_rmaker_ota_diag_status_t status)
{
    switch (status) {
    case OTA_DIAG_STATUS_FAIL:    return OTA_REMOTE_DIAG_STATUS_FAIL;
    case OTA_DIAG_STATUS_PENDING: return OTA_REMOTE_DIAG_STATUS_PENDING;
    case OTA_DIAG_STATUS_SUCCESS: return OTA_REMOTE_DIAG_STATUS_SUCCESS;
    default:                      return OTA_REMOTE_DIAG_STATUS_FAIL;
    }
}

/* OTA-remote device construction (inlined) **********************************/

static esp_rmaker_error_t __add_diag_return_param(esp_rmaker_device_t *device, const char *param_name,
        ota_remote_diag_status_t default_status, esp_rmaker_param_t **out_param)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_name, OTA_REMOTE_DIAG_RETURN_PARAM_TYPE,
                                esp_rmaker_int(default_status), PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
    if (!param) {
        OSAL_LOGE(TAG, "Failed to create diagnostics return parameter");
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_param_add_ui_type(param, ESP_RMAKER_UI_DROPDOWN);
    esp_rmaker_param_add_bounds(param, esp_rmaker_int(OTA_REMOTE_DIAG_STATUS_MIN + 1),
                                esp_rmaker_int(OTA_REMOTE_DIAG_STATUS_MAX - 1), esp_rmaker_int(1));
    esp_rmaker_error_t err = esp_rmaker_device_add_param(device, param);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add diagnostics return parameter to OTA remote device");
        return err;
    }
    *out_param = param;
    return ESP_RMAKER_OK;
}

/* Build the "OTA Remote" device with all its parameters and add it to the node. */
static esp_rmaker_error_t __add_ota_remote_device(const esp_rmaker_node_t *node,
        esp_rmaker_device_write_cb_t write_cb,
        const char *fw_ver,
        ota_remote_diag_status_t default_diag)
{
    esp_rmaker_error_t err;

    esp_rmaker_device_t *device = esp_rmaker_device_create(OTA_REMOTE_DEVICE_NAME, OTA_REMOTE_DEVICE_TYPE, NULL);
    if (!device) {
        OSAL_LOGE(TAG, "Failed to create OTA remote device");
        return ESP_RMAKER_FAIL;
    }
    /* Publish the handle before adding any parameters: adding a PERSIST param with
     * a stored value (e.g. after an OTA reboot) replays that value through the
     * write callback immediately, and the callback matches on this handle. */
    g_handles.ota_remote_device = device;

    err = esp_rmaker_device_add_cb(device, write_cb, NULL);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add write callback to OTA remote device");
        return err;
    }

    /* Firmware version (read-only). */
    g_handles.fw_ver_param = esp_rmaker_param_create(OTA_REMOTE_FW_VER_PARAM_NAME, OTA_REMOTE_FW_VER_PARAM_TYPE,
                             esp_rmaker_str(fw_ver), PROP_FLAG_READ);
    if (!g_handles.fw_ver_param) {
        OSAL_LOGE(TAG, "Failed to create firmware version parameter");
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_param_add_ui_type(g_handles.fw_ver_param, ESP_RMAKER_UI_TEXT);
    esp_rmaker_device_add_param(device, g_handles.fw_ver_param);

    /* Job firmware version (read-only). */
    g_handles.job_fw_ver_param = esp_rmaker_param_create(OTA_REMOTE_JOB_FW_VER_PARAM_NAME, OTA_REMOTE_JOB_FW_VER_PARAM_TYPE,
                                 esp_rmaker_str(""), PROP_FLAG_READ);
    if (!g_handles.job_fw_ver_param) {
        OSAL_LOGE(TAG, "Failed to create job firmware version parameter");
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_param_add_ui_type(g_handles.job_fw_ver_param, ESP_RMAKER_UI_TEXT);
    esp_rmaker_device_add_param(device, g_handles.job_fw_ver_param);

    /* Mark valid/invalid (write). */
    g_handles.mark_param = esp_rmaker_param_create(OTA_REMOTE_MARK_PARAM_NAME, OTA_REMOTE_MARK_PARAM_TYPE,
                           esp_rmaker_bool(false), PROP_FLAG_WRITE);
    if (!g_handles.mark_param) {
        OSAL_LOGE(TAG, "Failed to create mark parameter");
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_param_add_ui_type(g_handles.mark_param, ESP_RMAKER_UI_TOGGLE);
    esp_rmaker_device_add_param(device, g_handles.mark_param);

    /* Diagnostics stage (read-only). */
    g_handles.diag_stage_param = esp_rmaker_param_create(OTA_REMOTE_DIAG_STAGE_PARAM_NAME, OTA_REMOTE_DIAG_STAGE_PARAM_TYPE,
                                 esp_rmaker_int(OTA_REMOTE_DIAG_STAGE_NONE), PROP_FLAG_READ | PROP_FLAG_PERSIST);
    if (!g_handles.diag_stage_param) {
        OSAL_LOGE(TAG, "Failed to create diagnostics stage parameter");
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_param_add_ui_type(g_handles.diag_stage_param, ESP_RMAKER_UI_DROPDOWN);
    esp_rmaker_param_add_bounds(g_handles.diag_stage_param, esp_rmaker_int(OTA_REMOTE_DIAG_STAGE_MIN + 1),
                                esp_rmaker_int(OTA_REMOTE_DIAG_STAGE_MAX - 1), esp_rmaker_int(1));
    esp_rmaker_device_add_param(device, g_handles.diag_stage_param);

    /* Diagnostics status (read-only). */
    g_handles.diag_status_param = esp_rmaker_param_create(OTA_REMOTE_DIAG_STATUS_PARAM_NAME, OTA_REMOTE_DIAG_STATUS_PARAM_TYPE,
                                  esp_rmaker_int(OTA_REMOTE_DIAG_STATUS_FAIL), PROP_FLAG_READ | PROP_FLAG_PERSIST);
    if (!g_handles.diag_status_param) {
        OSAL_LOGE(TAG, "Failed to create diagnostics status parameter");
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_param_add_ui_type(g_handles.diag_status_param, ESP_RMAKER_UI_DROPDOWN);
    esp_rmaker_param_add_bounds(g_handles.diag_status_param, esp_rmaker_int(OTA_REMOTE_DIAG_STATUS_MIN + 1),
                                esp_rmaker_int(OTA_REMOTE_DIAG_STATUS_MAX - 1), esp_rmaker_int(1));
    esp_rmaker_device_add_param(device, g_handles.diag_status_param);

    /* Diagnostics return values (write). */
    err = __add_diag_return_param(device, OTA_REMOTE_DIAG_RETURN_POST_INIT_PARAM_NAME, default_diag,
                                  &g_handles.diag_return_params.post_init);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    err = __add_diag_return_param(device, OTA_REMOTE_DIAG_RETURN_POST_MQTT_PARAM_NAME, default_diag,
                                  &g_handles.diag_return_params.post_mqtt);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Killswitch (write). */
    g_handles.killswitch_param = esp_rmaker_param_create(OTA_REMOTE_KILLSWITCH_PARAM_NAME, OTA_REMOTE_KILLSWITCH_PARAM_TYPE,
                                 esp_rmaker_bool(false), PROP_FLAG_WRITE);
    if (!g_handles.killswitch_param) {
        OSAL_LOGE(TAG, "Failed to create killswitch parameter");
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_param_add_ui_type(g_handles.killswitch_param, ESP_RMAKER_UI_TOGGLE);
    esp_rmaker_device_add_param(device, g_handles.killswitch_param);

    /* Resume offset (read-only). */
    g_handles.resume_offset_param = esp_rmaker_param_create(OTA_REMOTE_RESUME_OFFSET_PARAM_NAME, OTA_REMOTE_RESUME_OFFSET_PARAM_TYPE,
                                    esp_rmaker_int(0), PROP_FLAG_READ);
    if (!g_handles.resume_offset_param) {
        OSAL_LOGE(TAG, "Failed to create resume offset parameter");
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_device_add_param(device, g_handles.resume_offset_param);

    err = esp_rmaker_node_add_device(node, device);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add OTA remote device to node");
        return err;
    }

    return ESP_RMAKER_OK;
}

/* Callbacks *****************************************************************/

static esp_rmaker_error_t __ota_remote_write_callback(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (device != g_handles.ota_remote_device) {
        OSAL_LOGE(TAG, "Invalid device");
        return ESP_RMAKER_FAIL;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;

    if (param == g_handles.mark_param) {
        bool mark = val.val.b;
        OSAL_LOGI(TAG, "Mark parameter written: %s", mark ? "true" : "false");
        err = mark ? esp_rmaker_ota_mark_valid() : esp_rmaker_ota_mark_invalid();
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGW(TAG, "Failed to mark OTA as valid/invalid; not updating mark parameter");
            return err;
        }
    } else if (strcmp(esp_rmaker_param_get_type(param), OTA_REMOTE_DIAG_RETURN_PARAM_TYPE) == 0) {
        int diag_return = val.val.i;
        if (diag_return <= OTA_REMOTE_DIAG_STATUS_MIN || diag_return >= OTA_REMOTE_DIAG_STATUS_MAX) {
            OSAL_LOGE(TAG, "Invalid diagnostics return value");
            return ESP_RMAKER_INVALID_ARG;
        }
        char *param_id = esp_rmaker_param_get_id(param);
        OSAL_LOGI(TAG, "Writing diagnostics return parameter '%s' -> %d", param_id ? param_id : "unknown", diag_return);
    } else if (param == g_handles.killswitch_param) {
        bool killswitch = val.val.b;
        OSAL_LOGI(TAG, "Killswitch parameter written: %s", killswitch ? "true" : "false");
        if (!killswitch) {
            return ESP_RMAKER_OK;
        }
        OSAL_LOGI(TAG, "Killing the node...");
        err = esp_rmaker_stop();
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to stop the node");
            return err;
        }
        OSAL_LOGW(TAG, "Node stopped. Node will stop responding to MQTT messages.");
        return ESP_RMAKER_OK;
    } else if (ctx && ctx->src == ESP_RMAKER_REQ_SRC_INIT) {
        /* Stored-value replay for a read-only status param (e.g. DStage/DStatus)
         * after an OTA reboot. The SDK has already restored the value; nothing to
         * do here, and it is not a real write request. */
        return ESP_RMAKER_OK;
    } else {
        OSAL_LOGE(TAG, "Invalid write parameter");
        return ESP_RMAKER_FAIL;
    }

    if (esp_rmaker_param_update(param, val) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update parameter");
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_ota_diag_status_t __get_diag_return_value(esp_rmaker_param_t *param)
{
    do {
        esp_rmaker_param_val_t *diag_return_val = esp_rmaker_param_get_val(param);
        if (diag_return_val == NULL) {
            break;
        }
        int diag_return = diag_return_val->val.i;
        if (diag_return <= OTA_REMOTE_DIAG_STATUS_MIN || diag_return >= OTA_REMOTE_DIAG_STATUS_MAX) {
            break;
        }
        return diag_status_ota_remote_to_rmaker_ota(diag_return);
    } while (0);

    OSAL_LOGW(TAG, "Invalid diagnostics return value for parameter; returning success");
    return OTA_DIAG_STATUS_SUCCESS;
}

static esp_rmaker_ota_diag_status_t __ota_diag_fn(esp_rmaker_ota_diag_priv_t *ota_diag_priv, void *priv)
{
    esp_rmaker_ota_diag_status_t diag_status = OTA_DIAG_STATUS_SUCCESS;
    ota_remote_diag_stage_t diag_stage = OTA_REMOTE_DIAG_STAGE_NONE;
    switch (ota_diag_priv->state) {
    case OTA_DIAG_STATE_INIT:
        OSAL_LOGI(TAG, "OTA diagnostics function called during OTA enable");
        diag_status = __get_diag_return_value(g_handles.diag_return_params.post_init);
        diag_stage = OTA_REMOTE_DIAG_STAGE_INIT;
        break;
    case OTA_DIAG_STATE_POST_MQTT:
        OSAL_LOGI(TAG, "OTA diagnostics function called after MQTT has connected");
        diag_status = __get_diag_return_value(g_handles.diag_return_params.post_mqtt);
        diag_stage = OTA_REMOTE_DIAG_STAGE_POST_MQTT;
        break;
    default:
        OSAL_LOGE(TAG, "Unknown OTA diagnostic state: %d", (int)ota_diag_priv->state);
        diag_status = OTA_DIAG_STATUS_FAIL;
        diag_stage = OTA_REMOTE_DIAG_STAGE_NONE;
        break;
    }

    esp_rmaker_param_update(g_handles.diag_stage_param, esp_rmaker_int(diag_stage));
    esp_rmaker_param_update_and_report(g_handles.diag_status_param, esp_rmaker_int(diag_status_rmaker_ota_to_ota_remote(diag_status)));
    return diag_status;
}

/* OTA event handler: report job version / resume offset and reboot when a new
 * image is ready. */
static void __ota_event_handler(void *event_handler_arg, osal_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base != RMAKER_OTA_EVENT) {
        return;
    }

    const esp_rmaker_ota_status_details_t *status_details = (const esp_rmaker_ota_status_details_t *)event_data;

    switch ((esp_rmaker_ota_event_t) event_id) {
    case RMAKER_OTA_EVENT_STARTING: {
        char *fw_version = "unknown";
        if (status_details != NULL && status_details->type == ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_STARTING) {
            fw_version = (char *)status_details->details.starting.fw_version;
        }
        esp_rmaker_param_update(g_handles.job_fw_ver_param, esp_rmaker_str(fw_version));
        esp_rmaker_param_update_and_report(g_handles.resume_offset_param, esp_rmaker_int(0));
        break;
    }
    case RMAKER_OTA_EVENT_RESUMED: {
        uint32_t offset = (event_data != NULL) ? *(const uint32_t *)event_data : 0;
        esp_rmaker_param_update_and_report(g_handles.resume_offset_param, esp_rmaker_int((int)offset));
        break;
    }
    case RMAKER_OTA_EVENT_FETCH_REQUEST_IGNORED:
        OSAL_LOGW(TAG, "OTA fetch request ignored. Triggering a delayed fetch request in 10 seconds...");
        esp_rmaker_ota_fetch_with_delay(10);
        break;
    case RMAKER_OTA_EVENT_REQ_FOR_REBOOT:
        OSAL_LOGI(TAG, "New firmware ready; rebooting in %d seconds", (int)OTA_REBOOT_DELAY_SECONDS);
        esp_rmaker_system_ctrl_reboot(OTA_REBOOT_DELAY_SECONDS);
        break;
    default:
        break;
    }
}

#if CONFIG_ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE
/* Local-control challenge-response toggles with the local-control service. */
static void __rmaker_event_handler(void *event_handler_arg, osal_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base != RMAKER_EVENT) {
        return;
    }
    switch ((esp_rmaker_event_t) event_id) {
    case RMAKER_EVENT_LOCAL_CTRL_STARTED:
        esp_rmaker_chal_resp_service_enable();
        break;
    case RMAKER_EVENT_LOCAL_CTRL_STOPPED:
        esp_rmaker_chal_resp_service_disable();
        break;
    default:
        break;
    }
}
#endif /* CONFIG_ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE */

#if !defined(ESP_PLATFORM)
/* POSIX-only: the console reset-network command needs a callback. There are no
 * real network credentials to reset on the host, so this is a no-op stub. */
static esp_rmaker_error_t app_network_reset_credentials(void)
{
    OSAL_LOGW(TAG, "Network reset called");
    return ESP_RMAKER_OK;
}
#endif /* !ESP_PLATFORM */

/* Portable startup **********************************************************/

osal_err_t app_run(void)
{
    OSAL_LOGI(TAG, "Starting ota-sim");

    /* Initialise the RMNG serial console. */
    esp_rmaker_console_init();

    /* Seed RNG (used by the SDK's jitter/backoff). */
    srand(time(NULL));

#if defined(ESP_PLATFORM)
    /* Bring up NVS and the network, then run RMNG provisioning. POSIX has no
     * local network stack, so the node skips all of this. */
    osal_storage_init(NULL);
    app_network_init();
    APP_RETURN_ON_ERR(esp_rmaker_pre_prov_init(), "Failed to initialise pre-provisioning components");
    APP_RETURN_ON_ERR(app_network_start_neo(), "Failed to start the network and await provisioning");
    APP_RETURN_ON_ERR(esp_rmaker_pre_prov_deinit(), "Failed to deinitialise pre-provisioning components");
#else /* !ESP_PLATFORM */
    /* Register the console reset-network callback (POSIX). */
    esp_rmaker_system_ctrl_register_network_reset_fn(app_network_reset_credentials);
#endif /* ESP_PLATFORM */

    /* Initialise the node. */
    esp_rmaker_config_t config = {
        .enable_time_sync = true,
    };
    const esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "OTA Sim", "ota-sim");
    if (!node) {
        OSAL_LOGE(TAG, "Failed to create node");
        return OSAL_ERR_FAIL;
    }

    /* Add the OTA-remote device (default diagnostics: success). */
    if (__add_ota_remote_device(node, __ota_remote_write_callback,
                                osal_sysinfo_get_fw_version(),
                                OTA_REMOTE_DIAG_STATUS_SUCCESS) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to setup node as OTA remote");
        return OSAL_ERR_FAIL;
    }

    /* Enable optional services. */
#if CONFIG_ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE
    /* Challenge-response endpoint only (no local control endpoints) */
    esp_rmaker_chal_resp_service_enable();
#else
    esp_rmaker_local_ctrl_service_enable();
#endif

    /* Enable OTA before starting the SDK. */
    OSAL_LOGI(TAG, "Enabling OTA...");
    /* Ensure the default event loop exists before attaching handlers
     * (INVALID_STATE just means it was already created). */
    osal_err_t loop_err = osal_event_loop_create_default();
    if (loop_err != OSAL_ERR_OK && loop_err != OSAL_ERR_INVALID_STATE) {
        OSAL_LOGE(TAG, "Failed to create the default event loop");
        return OSAL_ERR_FAIL;
    }
    osal_event_handler_register(RMAKER_OTA_EVENT, RMAKER_OTA_EVENT_BASE_ANY, __ota_event_handler, NULL);
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE
    osal_event_handler_register(RMAKER_EVENT, RMAKER_EVENT_BASE_ANY, __rmaker_event_handler, NULL);
#endif

    esp_rmaker_ota_config_t ota_config = {
        .ota_cb = NULL,
        .ota_diag = __ota_diag_fn,
        .priv = NULL,
    };
    if (esp_rmaker_ota_enable(&ota_config) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to enable OTA");
    } else {
        OSAL_LOGI(TAG, "OTA enabled successfully; auto-fetching pending jobs");
    }

    /* Start the SDK. */
    if (esp_rmaker_start() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to start the node");
        return OSAL_ERR_FAIL;
    }

    OSAL_LOGI(TAG, "ota-sim ready.");
    return OSAL_ERR_OK;
}
