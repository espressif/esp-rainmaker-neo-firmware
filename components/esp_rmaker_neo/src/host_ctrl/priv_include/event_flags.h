/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file event_flags.h
 * @brief Event flags for the host control.
 */

#ifdef __ESP_RMAKER_EVENT_FLAGS_H__
#undef __ESP_RMAKER_EVENT_FLAGS_H__
#endif

#ifndef __ESP_RMAKER_EVENT_FLAGS_H__
#define __ESP_RMAKER_EVENT_FLAGS_H__

#include "esp_rmaker_error_types.h"
#include "osal_event_group.h"

typedef enum {
    ESP_RMAKER_EVENT_FLAGS_ONLINE = (1 << 0),
    ESP_RMAKER_EVENT_FLAGS_STARTED = (1 << 1),
    ESP_RMAKER_EVENT_FLAGS_STOPPED = (1 << 2),
    ESP_RMAKER_EVENT_FLAGS_STATE_REPORTED = (1 << 3),
    ESP_RMAKER_EVENT_FLAGS_TIMESERIES_REPORTED = (1 << 4),
    ESP_RMAKER_EVENT_FLAGS_NOTIFICATION_SENT = (1 << 5),
    ESP_RMAKER_EVENT_FLAGS_STATE_STARTED_LISTENING = (1 << 6),
    ESP_RMAKER_EVENT_FLAGS_GROUP_INFO_RECEIVED = (1 << 7),
    ESP_RMAKER_EVENT_FLAGS_ALEXA_ENABLED_RECEIVED = (1 << 8),
    ESP_RMAKER_EVENT_FLAGS_GVA_ENABLED_RECEIVED = (1 << 16),
    ESP_RMAKER_EVENT_FLAGS_ST_ENABLED_RECEIVED = (1 << 17),
    ESP_RMAKER_EVENT_FLAGS_SCHED_VERSION_RECEIVED = (1 << 9),
    ESP_RMAKER_EVENT_FLAGS_SCHED_DETAILS_RECEIVED = (1 << 10),
    ESP_RMAKER_EVENT_FLAGS_TRIGGER_VERSION_RECEIVED = (1 << 11),
    ESP_RMAKER_EVENT_FLAGS_TRIGGER_DETAILS_RECEIVED = (1 << 12),
    ESP_RMAKER_EVENT_FLAGS_NODE_CONFIG_SENT = (1 << 13),
    ESP_RMAKER_EVENT_FLAGS_NODE_STARTED = (1 << 14),
    ESP_RMAKER_EVENT_FLAGS_NODE_STOPPED = (1 << 15),
    ESP_RMAKER_EVENT_FLAGS_ALL = (ESP_RMAKER_EVENT_FLAGS_ONLINE |
                                  ESP_RMAKER_EVENT_FLAGS_STARTED |
                                  ESP_RMAKER_EVENT_FLAGS_STOPPED |
                                  ESP_RMAKER_EVENT_FLAGS_ONLINE |
                                  ESP_RMAKER_EVENT_FLAGS_STATE_REPORTED |
                                  ESP_RMAKER_EVENT_FLAGS_TIMESERIES_REPORTED |
                                  ESP_RMAKER_EVENT_FLAGS_NOTIFICATION_SENT |
                                  ESP_RMAKER_EVENT_FLAGS_STATE_STARTED_LISTENING |
                                  ESP_RMAKER_EVENT_FLAGS_GROUP_INFO_RECEIVED |
                                  ESP_RMAKER_EVENT_FLAGS_ALEXA_ENABLED_RECEIVED |
                                  ESP_RMAKER_EVENT_FLAGS_GVA_ENABLED_RECEIVED |
                                  ESP_RMAKER_EVENT_FLAGS_ST_ENABLED_RECEIVED |
                                  ESP_RMAKER_EVENT_FLAGS_SCHED_VERSION_RECEIVED |
                                  ESP_RMAKER_EVENT_FLAGS_SCHED_DETAILS_RECEIVED |
                                  ESP_RMAKER_EVENT_FLAGS_TRIGGER_VERSION_RECEIVED |
                                  ESP_RMAKER_EVENT_FLAGS_TRIGGER_DETAILS_RECEIVED |
                                  ESP_RMAKER_EVENT_FLAGS_NODE_CONFIG_SENT),
} esp_rmaker_event_flags_t;

#define esp_rmaker_event_flags_set_online()                     esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_ONLINE)
#define esp_rmaker_event_flags_set_started()                    esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_STARTED)
#define esp_rmaker_event_flags_set_stopped()                    esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_STOPPED)
#define esp_rmaker_event_flags_set_state_reported()             esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_STATE_REPORTED)
#define esp_rmaker_event_flags_set_timeseries_reported()         esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_TIMESERIES_REPORTED)
#define esp_rmaker_event_flags_set_node_config_sent()           esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_NODE_CONFIG_SENT)
#define esp_rmaker_event_flags_set_notification_sent()          esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_NOTIFICATION_SENT)
#define esp_rmaker_event_flags_set_state_started_listening()    esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_STATE_STARTED_LISTENING)
#define esp_rmaker_event_flags_set_group_info_received()        esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_GROUP_INFO_RECEIVED)
#define esp_rmaker_event_flags_set_alexa_enabled_received()     esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_ALEXA_ENABLED_RECEIVED)
#define esp_rmaker_event_flags_set_gva_enabled_received()       esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_GVA_ENABLED_RECEIVED)
#define esp_rmaker_event_flags_set_st_enabled_received()        esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_ST_ENABLED_RECEIVED)
#define esp_rmaker_event_flags_set_sched_version_received()     esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_SCHED_VERSION_RECEIVED)
#define esp_rmaker_event_flags_set_sched_details_received()     esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_SCHED_DETAILS_RECEIVED)
#define esp_rmaker_event_flags_set_trigger_version_received()   esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_TRIGGER_VERSION_RECEIVED)
#define esp_rmaker_event_flags_set_trigger_details_received()   esp_rmaker_event_flags_set(ESP_RMAKER_EVENT_FLAGS_TRIGGER_DETAILS_RECEIVED)

#define esp_rmaker_event_flags_clear_online()                     esp_rmaker_event_flags_clear(ESP_RMAKER_EVENT_FLAGS_ONLINE)
#define esp_rmaker_event_flags_clear_all()                        esp_rmaker_event_flags_clear(ESP_RMAKER_EVENT_FLAGS_ALL)
#define esp_rmaker_event_flags_clear_group_info_received()        esp_rmaker_event_flags_clear(ESP_RMAKER_EVENT_FLAGS_GROUP_INFO_RECEIVED)
#define esp_rmaker_event_flags_clear_alexa_enabled_received()     esp_rmaker_event_flags_clear(ESP_RMAKER_EVENT_FLAGS_ALEXA_ENABLED_RECEIVED)
#define esp_rmaker_event_flags_clear_gva_enabled_received()       esp_rmaker_event_flags_clear(ESP_RMAKER_EVENT_FLAGS_GVA_ENABLED_RECEIVED)
#define esp_rmaker_event_flags_clear_st_enabled_received()        esp_rmaker_event_flags_clear(ESP_RMAKER_EVENT_FLAGS_ST_ENABLED_RECEIVED)
#define esp_rmaker_event_flags_clear_sched_version_received()     esp_rmaker_event_flags_clear(ESP_RMAKER_EVENT_FLAGS_SCHED_VERSION_RECEIVED)
#define esp_rmaker_event_flags_clear_trigger_version_received()   esp_rmaker_event_flags_clear(ESP_RMAKER_EVENT_FLAGS_TRIGGER_VERSION_RECEIVED)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the event flags.
 *
 * @return ESP_RMAKER_OK if successful, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_event_flags_init(void);

/**
 * @brief Deinitialize the event flags.
 *
 * @return ESP_RMAKER_OK if successful, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_event_flags_deinit(void);

/**
 * @brief Set the event flags.
 *
 * @param[in] flags The flags to set.
 */
void esp_rmaker_event_flags_set(osal_event_group_bits_t flags);

/**
 * @brief Clear the event flags.
 *
 * @param[in] flags The flags to clear.
 */
void esp_rmaker_event_flags_clear(osal_event_group_bits_t flags);

/**
 * @brief Wait for the event flags to be set.
 *
 * @param[in] flags The flags to wait for.
 * @param[in] timeout_ms The timeout in milliseconds.
 *
 * @return The error code.
 *         - ESP_RMAKER_OK: All event flags were set.
 *         - ESP_RMAKER_TIMEOUT: The timeout was reached.
 *         - ESP_RMAKER_FAIL: An error occurred.
 */
esp_rmaker_error_t esp_rmaker_event_flags_wait(osal_event_group_bits_t flags, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_EVENT_FLAGS_H__ */
