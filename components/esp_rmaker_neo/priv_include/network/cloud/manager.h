/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file manager.h
 * @brief Functions to manage the cloud.
 */

#ifndef __CLOUD_MANAGER_H__
#define __CLOUD_MANAGER_H__

#include "network/cloud/events.h"
#include "network/mqtt_topics.h"
#include "esp_rmaker_error_types.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Public function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the cloud manager.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_FAIL if registering the MQTT event handler fails, if memory allocation fails, or if mutex creation fails.
 */
esp_rmaker_error_t esp_rmaker_cloud_manager_init(void);

/**
 * @brief Deinitialize the cloud manager.
 *
 * @return ESP_RMAKER_OK on success.
 * @return Error code if unregistering the MQTT event handler fails.
 */
esp_rmaker_error_t esp_rmaker_cloud_manager_deinit(void);

/**
 * @brief Start listening for cloud events, and perform actions when events are received.
 * @note This blocks until the subscription is complete or the timeout is reached.
 *
 * @param[in] timeout_ms Timeout in milliseconds.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_FAIL if MQTT subscribe fails.
 * @return ESP_RMAKER_TIMEOUT if the subscription does not complete within timeout_ms.
 */
esp_rmaker_error_t esp_rmaker_cloud_manager_start_listening(uint32_t timeout_ms);

/**
 * @brief Stop listening for cloud events.
 * @note This blocks until the unsubscription is complete or the timeout is reached.
 *
 * @param[in] timeout_ms Timeout in milliseconds.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_FAIL if MQTT unsubscribe fails.
 * @return ESP_RMAKER_TIMEOUT if the unsubscription does not complete within timeout_ms.
 */
esp_rmaker_error_t esp_rmaker_cloud_manager_stop_listening(uint32_t timeout_ms);

/**
 * @brief Check if the cloud manager is listening for cloud events.
 *
 * @return True if the cloud manager is listening for cloud events, false otherwise.
 */
bool esp_rmaker_cloud_manager_is_listening(void);

/**
 * @brief Send events to the cloud on behalf of the Thing referenced by ``ctx``.
 *
 * @param[in] ctx          Topic ctx identifying the target Thing.
 * @param[in] p_event      Pointer to the event array.
 * @param[in] event_count  Number of events to send.
 * @param[in] sub_channel  Sub channel to send the events to.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if p_event is NULL or event_count is 0.
 * @return ESP_RMAKER_FAIL if JSON payload generation fails, memory allocation fails, or MQTT publish fails.
 */
esp_rmaker_error_t esp_rmaker_cloud_manager_send(const esp_rmaker_topic_ctx_t *ctx,
        esp_rmaker_cloud_event_t *p_event,
        size_t event_count,
        uint32_t sub_channel);

#if CONFIG_RMNG_BRIDGE_ENABLED
/**
 * @brief Send bridge-parent-scoped events to the cloud.
 *
 * Publishes on the bridge ``to_cloud`` topic, used for events whose
 * authoritative routing target is the bridge namespace itself rather
 * than any node's ``to_cloud``.
 *
 * Inbox registration of set-response callbacks (if any) is keyed by
 * self ctx, identical to the node-scoped send path.
 *
 * @param[in] p_event      Pointer to the event array.
 * @param[in] event_count  Number of events to send.
 * @param[in] sub_channel  Sub channel to send the events to.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if p_event is NULL or event_count is 0.
 * @return ESP_RMAKER_FAIL if JSON payload generation fails, memory allocation fails, or MQTT publish fails.
 */
esp_rmaker_error_t esp_rmaker_cloud_manager_send_bridge(esp_rmaker_cloud_event_t *p_event,
        size_t event_count,
        uint32_t sub_channel);
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* __CLOUD_MANAGER_H__ */
