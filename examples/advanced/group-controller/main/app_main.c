/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
/*
 * RainMaker Neo Controller example.
 *
 * A controller is a normal RainMaker Neo node (own cert, own shadow, joins a group like any node)
 * that is registered with the "controller" capability in the cloud. Scoped to the group it is in,
 * it can:
 *   - list the group's nodes + their config via the REST data APIs, signed with credentials the
 *     IoT Credential Provider issues for the device certificate, and
 *   - read and control the group's nodes over MQTT (shadow get + node/subgroup/group control
 *     publishes).
 *
 * Its group is the node's own group, from the cloud's group info over MQTT. See README.md.
 */

#include <string.h>

#include "app_entry.h"

#include <osal_log.h>
#include <osal_storage.h>
#include <app_network_neo.h>
#include <app_event_loop.h>

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_ota.h>
#include <esp_rmaker_system_ctrl.h>

#include <esp_rmaker_controller.h>
#include "controller_cli.h"

#include "sdkconfig.h"

static const char *TAG = "app_main";

osal_err_t app_run(void)
{
    /* Initialize NVS and the network stack. */
    osal_storage_init(NULL);
    app_network_init();

    /* Register the network reset function, so the console reset commands can trigger it. */
    esp_rmaker_system_ctrl_register_network_reset_fn(app_network_reset_credentials);

    app_event_loop_register_default_handler();

    /* Initialize the agent as a Controller node. */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "RainMaker Neo Controller", "Controller");
    if (!node) {
        OSAL_LOGE(TAG, "Could not initialise node. Aborting!!!");
        return OSAL_ERR_FAIL;
    }

    /* A minimal device so the controller is visible in the phone app. */
    esp_rmaker_device_t *controller_device =
        esp_rmaker_device_create("RMController", "esp.device.controller", NULL);
    esp_rmaker_device_add_param(controller_device,
                                esp_rmaker_name_param_create(ESP_RMAKER_DEF_NAME_PARAM_ID, "RainMaker Neo Controller"));
    esp_rmaker_node_add_device(node, controller_device);

#if CONFIG_ESP_RMAKER_NEO_ON_NETWORK_CHAL_RESP_ENABLE
    /* On-network association: the phone app adds the node to a user over the local network. */
    esp_rmaker_chal_resp_service_enable();
#endif

    /* Timezone (for scheduling). */
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
    esp_rmaker_ota_config_t ota_config = { 0 };
    esp_rmaker_ota_enable(&ota_config);

    /* Network provisioning: the node exists, so its provisioning endpoints are registered.
     * The controller advertises no device type: the manufacturer data only filters the scan results. */
    app_network_provision(NEO_MFG_DATA_DEVICE_TYPE_NONE, NEO_MFG_DATA_DEVICE_SUBTYPE_NONE);

    /* Start the agent (connects MQTT with the device cert). */
    esp_rmaker_start();

    /* The first REST call fetches the API base URL and the credential-provider endpoint from the
     * cloud's server config, and the first credentials from the IoT Credential Provider. */
    if (esp_rmaker_controller_enable() != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Controller not enabled - the CLI commands that call the cloud will fail.");
    }

    /* Start the interactive console with the controller commands. */
    controller_cli_init();

    OSAL_LOGI(TAG, "RainMaker Neo Controller ready. Type 'help' on the console.");
    return OSAL_ERR_OK;
}
