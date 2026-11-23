/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_flow.h
 * @brief Lifecycle entry points for the ESP RainMaker Neo SDK.
 *
 * The initialization/deinitialization sequence is as follows.
 *
 * 1. esp_rmaker_node_init()
 * 2. Create the devices and enable the services
 * 3. (optional) Start the network; provisioning runs and completes
 * 4. esp_rmaker_start(), ... use the SDK ..., esp_rmaker_stop()
 * 5. esp_rmaker_node_deinit()
 *
 * esp_rmaker_node_init() registers the provisioning endpoints (challenge-response and, on an
 * unclaimed node, assisted claiming), so the node must exist before provisioning starts. The
 * start task waits for claiming, reads the credentials and initializes the MQTT client.
 *
 * With CONFIG_ESP_RMAKER_NEO_LOCAL_ONLY the node never reaches the cloud. Node init sets up no MQTT
 * client and no schedule or automation service, and a separate start task only starts the
 * local endpoints. Parameter changes are not queued or reported.
 */

#ifndef __ESP_RMAKER_FLOW_H__
#define __ESP_RMAKER_FLOW_H__

/* Includes *******************************************************/

/* Standard includes. */
#include <stdint.h>

/* Error includes. */
#include "esp_rmaker_error_types.h"

/* Node model includes. */
#include "esp_rmaker_node.h"

/* Configuration includes. */
#include "sdkconfig.h"

#if CONFIG_ESP_RMAKER_NEO_CUSTOM_MQTT_CLIENT_PROVIDER
#include "osal_mqtt_prototypes.h"
#endif /* CONFIG_ESP_RMAKER_NEO_CUSTOM_MQTT_CLIENT_PROVIDER */

/* Structure definitions *******************************************************/

/**
 * @brief Configuration for the ESP RainMaker Neo SDK.
 */
typedef struct {
    /** Enable Time Sync
     * Setting this true will enable SNTP and fetch the current time before
     * attempting to connect to the ESP RainMaker Neo service.
     * Ignored with CONFIG_ESP_RMAKER_NEO_LOCAL_ONLY: a local only node has no network for SNTP.
     */
    bool enable_time_sync;

#if CONFIG_ESP_RMAKER_NEO_CUSTOM_MQTT_CLIENT_PROVIDER
    /** MQTT setup function to use. If not provided, an error will be returned. */
    osal_mqtt_impl_setup_t mqtt_setup_fn;
#endif /* CONFIG_ESP_RMAKER_NEO_CUSTOM_MQTT_CLIENT_PROVIDER */
} esp_rmaker_config_t;

/* Constants *******************************************************/

/**
 * @brief Default configuration for the ESP RainMaker Neo SDK.
 *
 * When CONFIG_ESP_RMAKER_NEO_CUSTOM_MQTT_CLIENT_PROVIDER is set, override mqtt_setup_fn in your application code.
 */
#if CONFIG_ESP_RMAKER_NEO_CUSTOM_MQTT_CLIENT_PROVIDER
#define ESP_RMAKER_DEFAULT_CONFIG (esp_rmaker_config_t) { \
    .enable_time_sync = true, \
    .mqtt_setup_fn = NULL, \
}
#else
#define ESP_RMAKER_DEFAULT_CONFIG (esp_rmaker_config_t) { \
    .enable_time_sync = true, \
}
#endif

/* Function declarations *******************************************************/

/* Initialize/Deinitialize: for the call sequence, see the file description. */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ESP RainMaker Neo Node
 *
 * This initializes the ESP RainMaker Neo agent and creates the node.
 * The model and firmware version for the node are set internally from the platform's project
 * name and version (see osal_sysinfo_get_project_name() / osal_sysinfo_get_fw_version()).
 *
 * @note This should be the first call before using any other ESP RainMaker Neo API, and it must
 *       complete before provisioning starts.
 * @note The call succeeds on an unclaimed node: the credentials are read by esp_rmaker_start().
 *
 * @param[in] config Configuration to be used by the SDK. See ESP_RMAKER_DEFAULT_CONFIG.
 * @param[in] name Name of the node.
 * @param[in] type Type of the node.
 *
 * @return Node handle on success.
 * @return NULL in case of failure.
 */
esp_rmaker_node_t *esp_rmaker_node_init(const esp_rmaker_config_t *config, const char *name, const char *type);

/**
 * @brief Deinitialize ESP RainMaker Neo Node
 *
 * This API deinitializes the ESP RainMaker Neo agent and the node created using esp_rmaker_node_init().
 *
 * @note The SDK must not be running. If a esp_rmaker_stop() is still in flight, this call blocks
 *       until the stop completes; if the SDK is started or starting, it returns
 *       ESP_RMAKER_INVALID_STATE.
 *
 * @param[in] node Node Handle returned by esp_rmaker_node_init().
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_node_deinit(const esp_rmaker_node_t *node);

/**
 * @brief Start the SDK.
 *
 * This queues the start work and returns as soon as the work queue is running; the steps below
 * then run asynchronously:
 *
 * - Waits for assisted claiming to finish, if the node was unclaimed at node init.
 * - Waits for network connectivity.
 * - Waits for time synchronization (blocking or decoupled, depending on the build).
 * - Loads the schedule and automation state from NVS.
 * - Reads the credentials, initializes the MQTT client and connects to the MQTT broker.
 * - Subscribes to the cloud topic and fetches cloud information (group information,
 *   whether Alexa is enabled, etc.).
 * - Publishes the node configuration, if required.
 *
 * RMAKER_EVENT_CORE_STARTED is posted once all of the above has completed.
 *
 * @note Network connectivity is not required for this call to succeed: the start work waits for
 *       it. A working connection is required for the node to come online.
 * @note With CONFIG_ESP_RMAKER_NEO_LOCAL_ONLY the start work only starts the local endpoints and
 *       posts RMAKER_EVENT_CORE_STARTED.
 * @note This function must be called after esp_rmaker_node_init().
 * @note On a node with assisted claiming enabled, call this after provisioning ends.
 *
 * @return ESP_RMAKER_OK on success, or if the SDK is already started.
 * @return ESP_RMAKER_INVALID_STATE if the SDK is not initialized.
 * @return error code in case of failure.
 */
esp_rmaker_error_t esp_rmaker_start(void);

/**
 * @brief Stop the SDK.
 *
 * Like esp_rmaker_start(), this queues the work and returns immediately. The queued work stops
 * the pending retries, unsubscribes from all topics and disconnects from the MQTT broker.
 *
 * @note This function must be called before esp_rmaker_node_deinit().
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_STATE if the SDK is not started.
 */
esp_rmaker_error_t esp_rmaker_stop(void);

/**
 * @brief Removed. A call fails the build. Call esp_rmaker_node_init() before provisioning.
 */
__attribute__((error("esp_rmaker_pre_prov_init() is removed; call esp_rmaker_node_init() before provisioning")))
esp_rmaker_error_t esp_rmaker_pre_prov_init(void);

/**
 * @brief Removed. A call fails the build. The start task does the post-provisioning steps.
 */
__attribute__((error("esp_rmaker_pre_prov_deinit() is removed; the start task does the post-provisioning steps")))
esp_rmaker_error_t esp_rmaker_pre_prov_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_FLOW_H__ */
