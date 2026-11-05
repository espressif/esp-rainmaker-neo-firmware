/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_main.c
 * @brief RainMaker Neo WebRTC camera, standalone mode: RainMaker Neo node, KVS signaling and the
 *        media plane all run on one chip (e.g. the ESP32-P4 of a P4-EYE, with
 *        Wi-Fi/BLE on the hosted co-processor).
 */

#include <ctype.h>
#include <string.h>

#include "esp_log.h"

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
#include "kvs_peer_connection.h"
#include "media_stream.h"
#include "esp_cli.h"

#if CONFIG_ESP_HOSTED_HOST_FEAT_BT
/* On P4 the BLE controller lives on the C6/C5 co-processor and is reached over
 * esp_hosted's HCI transport; it must be explicitly brought up before BLE
 * provisioning (NimBLE cannot init a local controller on the P4). */
#include "esp_hosted_bt_host_stack.h"
#endif

#if CONFIG_ESP_HOSTED && !CONFIG_ESP_HOSTED_AUTO_CALL_INIT_BEFORE_APP_MAIN
#include "esp_hosted.h"
#endif

#ifdef CONFIG_SLAVE_FLASHER_ENABLE
#include "slave_flasher.h"
static vprintf_like_t s_original_vprintf = NULL;

/* Prefix co-processor flashing logs with [HOST] so they're easy to tell apart
 * from the slave's own output that slave_flasher streams back over UART. */
static int custom_vprintf(const char *fmt, va_list args)
{
    printf("\033[1;36m[HOST]\033[0m ");
    return s_original_vprintf ? s_original_vprintf(fmt, args) : vprintf(fmt, args);
}
#endif

static const char *TAG = "app_main";

/* Signalled on the first MQTT connection; WebRTC bring-up waits for it so the
 * heavy KVS/media allocations don't race the SDK's connect path. */
static osal_semaphore_handle_t s_mqtt_connected_sem;

static void mqtt_connected_handler(void *arg, osal_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base == RMAKER_COMMON_EVENT && event_id == RMAKER_MQTT_EVENT_CONNECTED) {
        osal_semaphore_give(s_mqtt_connected_sem);
    }
}

#if CONFIG_ESP_HOSTED_HOST_FEAT_BT
/* Runs inside app_network_provision(), after node init and before the
 * network (and BLE provisioning) starts. Brings the co-processor's
 * BT controller up and binds NimBLE to hosted's HCI. The controller is left up
 * after provisioning: the only safe teardown signal (NimBLE fully stopped)
 * isn't exposed by app_network, and tearing down earlier races the NimBLE host
 * shutdown. */
static void hosted_bt_controller_up(void)
{
    esp_hosted_bt_host_stack_cfg_t bt_cfg = ESP_HOSTED_BT_HOST_STACK_CONFIG_DEFAULT();
    if (esp_hosted_bt_host_stack_setup(&bt_cfg) != ESP_OK) {
        OSAL_LOGW(TAG, "Failed to set up the co-processor BT host stack");
    }
}
#endif

/* Abstracted main function.
 * For how the ESP-IDF entry point uses this, see the examples/common/app_entry component. */
osal_err_t app_run(void)
{
    OSAL_LOGI(TAG, "RainMaker Neo WebRTC Camera (standalone)");

    /* The KVS esp_cli REPL owns the console. Register only the RainMaker Neo commands
     * (reset-network, reset-to-factory, ...) into the esp_console registry it
     * dispatches from: esp_rmaker_console_init() would start a second REPL,
     * which on a UART console fights esp_cli for the UART0 driver. */
    esp_cli_start();
#if CONFIG_ESP_RMAKER_NEO_CONSOLE_ENABLE
    esp_rmaker_register_commands();
#endif

#ifdef CONFIG_SLAVE_FLASHER_ENABLE
    /* On single-PCB P4 boards (e.g. P4-EYE) the on-board C6/C5 co-processor is
     * flashed in-system: write the embedded network_adapter image over UART
     * before bringing up the hosted SDIO link. Must run before app_network_init(). */
    s_original_vprintf = esp_log_set_vprintf(custom_vprintf);
    esp_err_t flash_ret = flash_slave();
    esp_log_set_vprintf(s_original_vprintf);
    if (flash_ret != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to flash co-processor: %s", esp_err_to_name(flash_ret));
        return OSAL_ERR_FAIL;
    }
#endif

#if CONFIG_ESP_HOSTED && !CONFIG_ESP_HOSTED_AUTO_CALL_INIT_BEFORE_APP_MAIN
    /* hosted's auto-init is a constructor, so it would enumerate the co-processor
     * while flash_slave() still holds it in ROM download mode, and give up.
     * Boards that flash their own co-processor bring hosted up here instead. */
    if (esp_hosted_init() != ESP_OK || esp_hosted_connect_to_slave() != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to bring up the hosted co-processor");
        return OSAL_ERR_FAIL;
    }
#endif

    /* Initialize NVS and the network stack. */
    osal_storage_init(NULL);
    app_network_init();
    /* Before provisioning, so the console's reset-network / reset-to-factory work even when provisioning or claiming fails. */
    esp_rmaker_system_ctrl_register_network_reset_fn(app_network_reset_credentials);

#if CONFIG_ESP_HOSTED_HOST_FEAT_BT
    app_network_prov_hooks_t prov_hooks = {
        .on_begin = hosted_bt_controller_up,
    };
    app_network_set_prov_hooks(&prov_hooks);
#endif

    /* Register the default RainMaker Neo event handler (logs RainMaker Neo, OTA and network events). */
    app_event_loop_register_default_handler();

    /* Initialise the node. Time sync stays enabled: the KVS SigV4 request
     * signing and credential-expiry math need correct wall-clock time. */
    esp_rmaker_config_t config = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "WebRTC Camera", "Camera");
    if (!node) {
        OSAL_LOGE(TAG, "Failed to create node");
        return OSAL_ERR_FAIL;
    }

    /* Device name: "<CHIP> Camera", e.g. "ESP32P4 Camera" */
    char dev_name[32] = {0};
    snprintf(dev_name, sizeof(dev_name), "%s Camera", CONFIG_IDF_TARGET);
    for (int i = 0; dev_name[i] && dev_name[i] != ' '; i++) {
        dev_name[i] = toupper((unsigned char)dev_name[i]);
    }

    /* Create the camera device (adds name and channel parameters) */
    esp_rmaker_device_t *camera_device = esp_rmaker_camera_device_create(dev_name, NULL);
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

    /* Network provisioning: the node exists, so its provisioning endpoints are registered.
     * Blocks until provisioned and connected. See the examples/common/app_network component. */
    APP_RETURN_ON_ERR(app_network_provision(NEO_MFG_DATA_DEVICE_TYPE_NONE, NEO_MFG_DATA_DEVICE_SUBTYPE_NONE),
                      "Failed to provision the network");

    /* Start the node */
    if (esp_rmaker_start() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to start node");
        return OSAL_ERR_FAIL;
    }

    /* Wait for the first MQTT connection before bringing up WebRTC: the KVS
     * stack's allocations and its credential fetch (needs network + synced
     * time) shouldn't race the SDK's own connect path. */
    OSAL_LOGI(TAG, "Waiting for MQTT connection before starting WebRTC...");
    osal_semaphore_take(s_mqtt_connected_sem, OSAL_MAX_DELAY);
    osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, mqtt_connected_handler);

    /* The credential fetch signs with the wall clock, so it needs synced time too. */
    OSAL_LOGI(TAG, "Waiting for time sync...");
    osal_timesync_wait_for_sync(0);

    /* Initialize and start WebRTC (KVS master on channel rmng-v1-{node_id}) */
    OSAL_LOGI(TAG, "Starting WebRTC...");
    rmaker_webrtc_camera_config_t camera_config = {
        .peer_connection_if = kvs_peer_connection_if_get(),
        .video_capture = media_stream_get_video_capture_if(),
        /* Audio capture opens the board's codec, so pass it only when there is one. */
#if CONFIG_MEDIA_STREAM_HAS_AUDIO_CODEC
        .audio_capture = media_stream_get_audio_capture_if(),
#endif
    };
    if (rmaker_webrtc_camera_init(&camera_config) != 0) {
        OSAL_LOGE(TAG, "Failed to start WebRTC");
        return OSAL_ERR_FAIL;
    }

    OSAL_LOGI(TAG, "Camera ready.");
    return OSAL_ERR_OK;
}
