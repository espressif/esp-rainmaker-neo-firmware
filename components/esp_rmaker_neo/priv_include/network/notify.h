/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file notify.h
 * @brief Direct notification functions.
 */

#ifndef __NETWORK_NOTIFY_H__
#define __NETWORK_NOTIFY_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdbool.h>
#include <stdint.h>

/* Error types includes */
#include "esp_rmaker_error_types.h"
#include "esp_rmaker_node.h"

/* State (update ID) includes */
#include "esp_rmaker_state.h"

/* JSON includes */
#include "json_generator.h"

/* Types **********************************************************************/

/**
 * @brief Function to report payload to the notification.
 * @param[in] jptr Pointer to the JSON generator.
 * @param[in] data Pointer to the data to be added to the notification.
 * @param[in] is_sizing Whether this function call is to size the payload.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
typedef esp_rmaker_error_t (*esp_rmaker_notify_report_payload_t)(json_gen_str_t *jptr, void *data, bool is_sizing);

/**
 * @brief Direct notification data.
 */
typedef struct {
    esp_rmaker_notify_report_payload_t report_payload_fn; /* Function to report payload to the notification. */
    void *data;                                           /* Data to be reported to the notification. */
} esp_rmaker_notification_t;

/* Public function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the notification manager.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_notify_init(void);

/**
 * @brief Deinitialize the notification manager.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_notify_deinit(void);

/**
 * @brief Send a direct notification.
 * @param[in] p_notification Pointer to the notification.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_notify_send(esp_rmaker_notification_t *p_notification);

/**
 * @brief Send a direct notification to a specific node's notify topic.
 * @param[in] node Owning node (self or a bridge child).
 * @param[in] p_notification Pointer to the notification.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_notify_send_for_node(const esp_rmaker_node_t *node, esp_rmaker_notification_t *p_notification);

/**
 * @brief Send a push notification for a given state update.
 *
 * @param[in] update_id The state update ID that triggered this notification.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_notify_send_push(esp_rmaker_state_update_id_t update_id);

/**
 * @brief Send a direct notification to the self node and block until it flushes.
 *
 * Publishes to the self node's notify topic and waits for the QoS1 publish to complete.
 * Use when the publish must be confirmed before proceeding (e.g. during factory reset, before
 * credentials/NVS are wiped). Must be called from a task other than the MQTT event_loop task.
 *
 * @param[in] p_notification Pointer to the notification.
 * @param[in] timeout_ms Max time to wait for the publish to be confirmed.
 * @return ESP_RMAKER_OK if confirmed, ESP_RMAKER_TIMEOUT if not confirmed in time,
 *         ESP_RMAKER_INVALID_STATE if notify is not initialized, otherwise a publish error.
 */
esp_rmaker_error_t esp_rmaker_notify_send_sync(esp_rmaker_notification_t *p_notification, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __NETWORK_NOTIFY_H__ */
