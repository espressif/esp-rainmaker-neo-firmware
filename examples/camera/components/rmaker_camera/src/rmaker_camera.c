/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include <esp_rmaker_core.h>
#include "esp_rmaker_node.h"
#include "esp_rmaker_data_model.h"
#include "esp_rmaker_standard_params.h"

#include "rmaker_camera.h"
#include "rmaker_aws_credentials.h"
#include "app_webrtc.h"
#include "kvs_signaling.h"

static const char *TAG = "rmaker_camera";

/* The Channel param. rmaker_webrtc_camera_init() sets its value once the node ID is known. */
static esp_rmaker_param_t *s_channel_param;

/* Create a RainMaker camera device with name and channel parameters */
esp_rmaker_device_t *esp_rmaker_camera_device_create(const char *dev_name, void *priv_data)
{
    esp_rmaker_device_t *device = esp_rmaker_device_create(dev_name, RMAKER_CAMERA_DEVICE_TYPE, priv_data);
    if (device) {
        /* Add name parameter */
        esp_rmaker_device_add_param(device, esp_rmaker_name_param_create(ESP_RMAKER_DEF_NAME_PARAM_ID, dev_name));

        /* Always add the Channel param: the app needs it to open the stream. An unclaimed
         * node gets its node ID only at claim, during provisioning, so the value starts
         * empty there and rmaker_webrtc_camera_init() fills it in. */
        char channel_name[64] = {0};
        char *node_id = esp_rmaker_get_node_id();
        if (node_id) {
            snprintf(channel_name, sizeof(channel_name), "%s%s", RMAKER_CAMERA_CHANNEL_NAME_PREFIX, node_id);
            free(node_id);
        }
        s_channel_param = esp_rmaker_param_create(RMAKER_CAMERA_DEF_CHANNEL_NAME, RMAKER_CAMERA_PARAM_CHANNEL,
                          esp_rmaker_str(channel_name), PROP_FLAG_READ);
        if (s_channel_param) {
            esp_rmaker_device_add_param(device, s_channel_param);
        }
    }
    return device;
}

/* Event handler for WebRTC events */
static void app_webrtc_event_handler(app_webrtc_event_data_t *event_data, void *user_ctx)
{
    if (event_data == NULL) {
        return;
    }

    switch (event_data->event_id) {
    case APP_WEBRTC_EVENT_INITIALIZED:
        ESP_LOGI(TAG, "[KVS Event] WebRTC Initialized.");
        break;
    case APP_WEBRTC_EVENT_DEINITIALIZING:
        ESP_LOGI(TAG, "[KVS Event] WebRTC Deinitialized.");
        break;
    case APP_WEBRTC_EVENT_SIGNALING_CONNECTING:
        ESP_LOGI(TAG, "[KVS Event] Signaling Connecting.");
        break;
    case APP_WEBRTC_EVENT_SIGNALING_CONNECTED:
        ESP_LOGI(TAG, "[KVS Event] Signaling Connected.");
        break;
    case APP_WEBRTC_EVENT_SIGNALING_DISCONNECTED:
        ESP_LOGI(TAG, "[KVS Event] Signaling Disconnected.");
        break;
    case APP_WEBRTC_EVENT_SIGNALING_DESCRIBE:
        ESP_LOGI(TAG, "[KVS Event] Signaling Describe.");
        break;
    case APP_WEBRTC_EVENT_SIGNALING_GET_ENDPOINT:
        ESP_LOGI(TAG, "[KVS Event] Signaling Get Endpoint.");
        break;
    case APP_WEBRTC_EVENT_SIGNALING_GET_ICE:
        ESP_LOGI(TAG, "[KVS Event] Signaling Get ICE.");
        break;
    case APP_WEBRTC_EVENT_PEER_CONNECTION_REQUESTED:
        ESP_LOGI(TAG, "[KVS Event] Peer Connection Requested.");
        break;
    case APP_WEBRTC_EVENT_PEER_CONNECTED:
        ESP_LOGI(TAG, "[KVS Event] Peer Connected: %s", event_data->peer_id);
        break;
    case APP_WEBRTC_EVENT_PEER_DISCONNECTED:
        ESP_LOGI(TAG, "[KVS Event] Peer Disconnected: %s", event_data->peer_id);
        break;
    case APP_WEBRTC_EVENT_STREAMING_STARTED:
        ESP_LOGI(TAG, "[KVS Event] Streaming Started for Peer: %s", event_data->peer_id);
        break;
    case APP_WEBRTC_EVENT_STREAMING_STOPPED:
        ESP_LOGI(TAG, "[KVS Event] Streaming Stopped for Peer: %s", event_data->peer_id);
        break;
    case APP_WEBRTC_EVENT_RECEIVED_OFFER:
        ESP_LOGI(TAG, "[KVS Event] Received Offer.");
        break;
    case APP_WEBRTC_EVENT_SENT_ANSWER:
        ESP_LOGI(TAG, "[KVS Event] Sent Answer.");
        break;
    case APP_WEBRTC_EVENT_ERROR:
    case APP_WEBRTC_EVENT_SIGNALING_ERROR:
    case APP_WEBRTC_EVENT_PEER_CONNECTION_FAILED:
        ESP_LOGE(TAG, "[KVS Event] Error Event %d: Code %d, Message: %s",
                 (int)event_data->event_id, (int)event_data->status_code, event_data->message);
        break;
    default:
        ESP_LOGI(TAG, "[KVS Event] Unhandled Event ID: %d", (int)event_data->event_id);
        break;
    }
}

int rmaker_webrtc_camera_init(rmaker_webrtc_camera_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Config parameter cannot be NULL");
        return -1;
    }

    if (config->peer_connection_if == NULL) {
        ESP_LOGE(TAG, "peer_connection_if cannot be NULL");
        return -1;
    }

    /* Register the event callback */
    if (app_webrtc_register_event_callback(app_webrtc_event_handler, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to register KVS event callback.");
    }

    /* Call mode-specific initialization callback if provided */
    if (config->init_callback) {
        if (config->init_callback(config->init_callback_user_data) != 0) {
            ESP_LOGE(TAG, "Mode-specific initialization callback failed");
            return -1;
        }
    }

    /* Create channel name using the node ID (rmng-v1-{node_id}, created by
     * the RainMaker Neo cloud at node registration) */
    char *node_id = esp_rmaker_get_node_id();
    if (!node_id) {
        ESP_LOGE(TAG, "Failed to get the node ID");
        return -1;
    }
    static char channel_name[64] = {0};
    snprintf(channel_name, sizeof(channel_name), "%s%s", RMAKER_CAMERA_CHANNEL_NAME_PREFIX, node_id);
    free(node_id);

    /* A node claimed during this boot created the Channel param with an empty value */
    if (s_channel_param && esp_rmaker_param_update_and_report(s_channel_param, esp_rmaker_str(channel_name)) != ESP_RMAKER_OK) {
        ESP_LOGW(TAG, "Failed to report the channel name");
    }

    /* The credential endpoint, video role alias and region come only from the
     * deployment's server config. The node asks for it on each connection until
     * it arrives, so keep waiting. */
    esp_err_t err;
    while ((err = rmaker_camera_resolve_deployment(RMAKER_CAMERA_SERVER_CONFIG_WAIT_MS)) == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Server config not received yet, still waiting");
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "The server config has no credential endpoint, video role alias or region");
        return -1;
    }
    const char *aws_region = rmaker_camera_get_aws_region();
    if (rmaker_camera_credentials_init() != ESP_OK) {
        return -1;
    }

    /* Set up KVS signaling configuration */
    static kvs_signaling_config_t kvs_signaling_cfg = {0};

    kvs_signaling_cfg.pChannelName = channel_name;

    /* Callback-based credentials supplier (IoT Credential Provider via the
     * RainMaker Neo node certificate) */
    kvs_signaling_cfg.useIotCredentials = false;
    kvs_signaling_cfg.iotCoreCredentialEndpoint = NULL;
    kvs_signaling_cfg.iotCoreCert = NULL;
    kvs_signaling_cfg.iotCorePrivateKey = NULL;
    kvs_signaling_cfg.iotCoreRoleAlias = NULL;
    kvs_signaling_cfg.iotCoreThingName = NULL;

    kvs_signaling_cfg.awsRegion = (char *)aws_region;  /* Region is needed for STUN URL */
    kvs_signaling_cfg.fetch_credentials_cb = rmaker_fetch_aws_credentials;
    kvs_signaling_cfg.fetch_credentials_user_data = 0;

    /* Configure WebRTC app */
    app_webrtc_config_t app_webrtc_config = APP_WEBRTC_CONFIG_DEFAULT();

    app_webrtc_config.signaling_client_if = kvs_signaling_client_if_get();
    app_webrtc_config.signaling_cfg = &kvs_signaling_cfg;

    app_webrtc_config.peer_connection_if = (webrtc_peer_connection_if_t *)config->peer_connection_if;
    app_webrtc_config.video_capture = config->video_capture;
    app_webrtc_config.audio_capture = config->audio_capture;
    app_webrtc_config.video_player = config->video_player;
    app_webrtc_config.audio_player = config->audio_player;

    ESP_LOGI(TAG, "Initializing WebRTC application:");
    ESP_LOGI(TAG, "  - Role: MASTER (cameras initiate connections)");
    ESP_LOGI(TAG, "  - Channel: %s", kvs_signaling_cfg.pChannelName);
    ESP_LOGI(TAG, "  - Region: %s", aws_region);
    ESP_LOGI(TAG, "  - Credentials: callback-based (IoT Credential Provider, role alias %s)",
             rmaker_camera_get_role_alias());

    WEBRTC_STATUS status = app_webrtc_init(&app_webrtc_config);
    if (status != WEBRTC_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Failed to initialize WebRTC application: 0x%08" PRIx32, (uint32_t)status);
        return -1;
    }

    /* Run WebRTC application (creates a task internally) */
    ESP_LOGI(TAG, "Starting WebRTC application");
    status = app_webrtc_run();
    if (status != WEBRTC_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Failed to start WebRTC application: 0x%08" PRIx32, (uint32_t)status);
        app_webrtc_terminate();
        return -1;
    }
    ESP_LOGI(TAG, "WebRTC application started successfully");
    return 0;
}
