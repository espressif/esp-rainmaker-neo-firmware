/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#pragma once

#include <esp_rmaker_core.h>
#include "esp_rmaker_data_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Channel name prefix for KVS WebRTC. The RainMaker Neo cloud creates a
 * SINGLE_MASTER signaling channel named rmng-v1-{node_id} at node
 * registration (see rmng misc/specs/kvs-camera-streaming.md). */
#define RMAKER_CAMERA_CHANNEL_NAME_PREFIX "rmng-v1-"

/* RainMaker device and parameter definitions */
#define RMAKER_CAMERA_DEF_CHANNEL_NAME "Channel"
#define RMAKER_CAMERA_DEVICE_TYPE "esp.device.camera"
#define RMAKER_CAMERA_PARAM_CHANNEL "esp.param.channel"

/**
 * @brief Optional initialization callback function type
 *
 * Called during rmaker_webrtc_camera_init() to allow mode-specific
 * initialization.
 *
 * @param user_data User data passed during config setup
 * @return 0 on success, non-zero on failure
 */
typedef int (*rmaker_webrtc_camera_init_callback_t)(void *user_data);

/**
 * @brief Configuration structure for WebRTC camera initialization
 */
typedef struct {
    void *peer_connection_if;                          /*!< Required: Peer connection interface implementation (opaque handle) */
    void *video_capture;                                /*!< Optional: Video capture interface (NULL for signaling-only mode) */
    void *audio_capture;                                /*!< Optional: Audio capture interface (NULL for signaling-only mode) */
    void *video_player;                                 /*!< Optional: Video player interface (NULL for signaling-only mode) */
    void *audio_player;                                 /*!< Optional: Audio player interface (NULL for signaling-only mode) */
    rmaker_webrtc_camera_init_callback_t init_callback; /*!< Optional: Mode-specific initialization callback */
    void *init_callback_user_data;                      /*!< Optional: User data passed to init_callback */
} rmaker_webrtc_camera_config_t;

/**
 * @brief Create a RainMaker camera device with name and channel parameters
 *
 * Creates a camera device and adds the name parameter and channel
 * parameter (derived from the node ID) to it.
 *
 * @param dev_name Device name
 * @param priv_data Private data to associate with the device
 * @return Pointer to the created device, or NULL on failure
 */
esp_rmaker_device_t *esp_rmaker_camera_device_create(const char *dev_name, void *priv_data);

/**
 * @brief Initialize and start the WebRTC stack
 *
 * Initializes the WebRTC stack with RainMaker Neo-vended AWS credentials and starts
 * the WebRTC application in a separate task.
 *
 * @param config Configuration structure containing peer connection and media interfaces
 * @return 0 on success, -1 on failure
 */
int rmaker_webrtc_camera_init(rmaker_webrtc_camera_config_t *config);

#ifdef __cplusplus
}
#endif
