/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_main.c
 * @brief RainMaker Neo WebRTC camera, split mode (signaling side).
 *
 * Runs on the ESP32-C6 or ESP32-C5 co-processor of a P4 board: the RainMaker
 * Neo node, BLE provisioning and the KVS signaling client live here, and this
 * chip is also the esp_hosted Wi-Fi co-processor of the ESP32-P4. The P4 runs
 * the KVS SDK's streaming_only example (media capture/encode + RTP);
 * signaling is relayed to it over webrtc_bridge, so media never round-trips
 * through the co-processor.
 */

#include "app_entry.h"

#include <osal_semaphore.h>
#include <osal_storage.h>
#include <osal_timesync.h>
#include <osal_event_loop.h>
#include <app_network_neo.h>
#include <app_event_loop.h>

#include <esp_rmaker_core.h>
#include <esp_rmaker_console.h>
#include <esp_rmaker_system_ctrl.h>
#include <esp_rmaker_common_events.h>

/* Camera / WebRTC includes */
#include "rmaker_camera.h"
#include "esp_cli.h"
#include "esp_work_queue.h"
#include "bridge_peer_connection.h"
#include "webrtc_bridge.h"
#include "eh_cp.h"
#include "power_save_handler.h"
#if CONFIG_IDF_TARGET_ESP32C5 && CONFIG_BT_LE_LP_CLK_SRC_MAIN_XTAL
#include <network_provisioning/manager.h>
#include "esp_private/esp_modem_clock.h"
#endif
#include "host_ps_integration.h"

static const char *TAG = "app_main";

/* Signalled on the first MQTT connection; WebRTC bring-up waits for it so the
 * KVS credential fetch (needs network + synced time) doesn't race the SDK's
 * connect path. */
static osal_semaphore_handle_t s_mqtt_connected_sem;

static void mqtt_connected_handler(void *arg, osal_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base == RMAKER_COMMON_EVENT && event_id == RMAKER_MQTT_EVENT_CONNECTED) {
        osal_semaphore_give(s_mqtt_connected_sem);
    }
}

#if CONFIG_IDF_TARGET_ESP32C5 && CONFIG_BT_LE_LP_CLK_SRC_MAIN_XTAL
/* BLE init keeps the main XTAL on through light sleep for its low-power clock.
 * On the C5, esp_bt_controller_deinit() does not release it: IDF has the
 * modem_clock_deselect_lp_clock_source(PERIPH_BT_MODULE) calls commented out in
 * components/bt/controller/esp32c5/bt.c (lines 1115 and 1140 in v5.5.5 and
 * v6.0.3). The C6 controller makes the same calls. Without this, every light
 * sleep after BLE provisioning costs about 4-5 mA more. app_network's handler,
 * registered first, has torn BLE down by now.
 * IDF fixes this in "fix(bt): fix deselect slow clk missing on ESP32-C5"
 * It is in v5.5.6 and v6.1.1, and on release/v6.0 after v6.0.3. With that
 * fix this second call does nothing more. Remove it once the minimum supported
 * IDF has the fix. */
static void on_prov_end(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    modem_clock_deselect_lp_clock_source(PERIPH_BT_MODULE);
    OSAL_LOGI(TAG, "Provisioning ended: released the BLE XTAL clock");
}
#endif

/* esp_hosted's CLI restart on a host wake clears the esp_console command
 * table; register the commands again. */
static void register_cli_after_wake(void)
{
    esp_cli_register_cmds();
#if CONFIG_ESP_RMAKER_NEO_CONSOLE_ENABLE
    esp_rmaker_register_commands();
#endif
}

/* Split-mode hook, run by rmaker_webrtc_camera_init() before app_webrtc_init() */
static int split_mode_init_callback(void *user_data)
{
    (void)user_data;

    /* Init the KVS work queue early with a smaller stack than app_webrtc's
     * 32 KB default, to keep this shared task small on the C6. */
    esp_work_queue_config_t work_queue_config = ESP_WORK_QUEUE_CONFIG_DEFAULT();
    work_queue_config.stack_size = 8 * 1024;
    if (esp_work_queue_init_with_config(&work_queue_config) != ESP_OK ||
            esp_work_queue_start() != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to start the KVS work queue");
        return -1;
    }

    /* The bridge command framework must exist before app_webrtc_init(), which
     * registers internal handlers (e.g. GET_TIME) through it. */
    if (bridge_cmd_init() != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to initialise the bridge command framework");
        return -1;
    }
    return 0;
}

/* Abstracted main function.
 * For how the ESP-IDF entry point uses this, see the examples/common/app_entry component. */
osal_err_t app_run(void)
{
    OSAL_LOGI(TAG, "RainMaker Neo WebRTC Camera (split mode, signaling side)");

    /* The KVS esp_cli REPL owns the console. Register only the RainMaker Neo commands
     * (reset-network, reset-to-factory, ...) into the esp_console registry it
     * dispatches from: esp_rmaker_console_init() would start a second REPL,
     * which on a UART console fights esp_cli for the UART0 driver. */
    esp_cli_start();
#if CONFIG_ESP_RMAKER_NEO_CONSOLE_ENABLE
    esp_rmaker_register_commands();
#endif
    host_ps_integration_set_cli_register_cb(register_cli_after_wake);

    /* Initialize NVS and the network stack. app_network_init() creates the
     * default STA netif, which esp_hosted's network split picks up, so it must
     * run before eh_cp_init(). */
    osal_storage_init(NULL);
    app_network_init();
#if CONFIG_IDF_TARGET_ESP32C5 && CONFIG_BT_LE_LP_CLK_SRC_MAIN_XTAL
    esp_event_handler_register(NETWORK_PROV_EVENT, NETWORK_PROV_END, &on_prov_end, NULL);
#endif
    /* Before provisioning, so the console's reset-network / reset-to-factory work even when provisioning or claiming fails. */
    esp_rmaker_system_ctrl_register_network_reset_fn(app_network_reset_credentials);

    /* Bring up the esp_hosted co-processor (Wi-Fi for the streaming P4) before
     * Wi-Fi starts. AUTO_INIT is off so that it comes after the netif above. */
    if (eh_cp_init() != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to initialise the hosted co-processor");
        return OSAL_ERR_FAIL;
    }

    /* Before anything can ask the co-processor to sleep */
    power_save_init();

    /* Register the default RainMaker Neo event handler (logs RainMaker Neo, OTA and network events). */
    app_event_loop_register_default_handler();

    /* Initialise the node. Time sync stays enabled: the KVS SigV4 request
     * signing and credential-expiry math need correct wall-clock time (the P4
     * also takes its time from here over the bridge). */
    esp_rmaker_config_t config = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "WebRTC Camera", "Camera");
    if (!node) {
        OSAL_LOGE(TAG, "Failed to create node");
        return OSAL_ERR_FAIL;
    }

    /* Create the camera device (adds name and channel parameters) */
    esp_rmaker_device_t *camera_device = esp_rmaker_camera_device_create("Camera", NULL);
    if (!camera_device) {
        OSAL_LOGE(TAG, "Failed to create camera device");
        return OSAL_ERR_FAIL;
    }
    if (esp_rmaker_node_add_device(node, camera_device) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add device to node");
        return OSAL_ERR_FAIL;
    }

    /* Timezone service, so the app can set the node's timezone */
    if (esp_rmaker_timezone_service_enable() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to enable timezone service");
        return OSAL_ERR_FAIL;
    }

    /* System service: remote reboot / network reset / factory reset */
    esp_rmaker_system_serv_config_t system_serv_config = {
        .flags = SYSTEM_SERV_FLAGS_ALL,
        .reboot_seconds = 2,
        .reset_seconds = 2,
        .reset_reboot_seconds = 2,
        .network_reset_fn = app_network_reset_credentials,
    };
    if (esp_rmaker_system_service_enable(&system_serv_config) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to enable system service");
        return OSAL_ERR_FAIL;
    }

    /* Register for the MQTT-connected event before starting the node */
    s_mqtt_connected_sem = osal_semaphore_create_binary();
    if (!s_mqtt_connected_sem ||
            osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED,
                                        mqtt_connected_handler, NULL) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to set up MQTT-connected wait");
        return OSAL_ERR_FAIL;
    }

    /* Network provisioning (BLE, local to this chip): the node exists, so its provisioning
     * endpoints are registered. Blocks until provisioned and connected. */
    APP_RETURN_ON_ERR(app_network_provision(NEO_MFG_DATA_DEVICE_TYPE_NONE, NEO_MFG_DATA_DEVICE_SUBTYPE_NONE),
                      "Failed to provision the network");

    /* Start the node */
    if (esp_rmaker_start() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to start node");
        return OSAL_ERR_FAIL;
    }

    OSAL_LOGI(TAG, "Waiting for MQTT connection before starting WebRTC...");
    osal_semaphore_take(s_mqtt_connected_sem, OSAL_MAX_DELAY);
    osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, mqtt_connected_handler);

    /* The credential fetch signs with the wall clock, so it needs synced time too. */
    OSAL_LOGI(TAG, "Waiting for time sync...");
    osal_timesync_wait_for_sync(0);

    /* Start KVS signaling (master on channel rmng-v1-{node_id}); the bridge
     * peer connection hands each session to the streaming P4. */
    OSAL_LOGI(TAG, "Starting WebRTC signaling...");
    /* Wake the P4 when media starts; the bridge adapter wakes it for an offer */
    power_save_register_webrtc_events();

    rmaker_webrtc_camera_config_t camera_config = {
        .peer_connection_if = bridge_peer_connection_if_get(),
        .init_callback = split_mode_init_callback,
    };
    if (rmaker_webrtc_camera_init(&camera_config) != 0) {
        OSAL_LOGE(TAG, "Failed to start WebRTC signaling");
        return OSAL_ERR_FAIL;
    }

    OSAL_LOGI(TAG, "Camera signaling ready.");
    return OSAL_ERR_OK;
}
