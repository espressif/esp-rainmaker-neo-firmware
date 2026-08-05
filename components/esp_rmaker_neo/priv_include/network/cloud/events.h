/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file events.h
 * @brief Functions to generate the cloud events.
 */

#ifndef __CLOUD_EVENTS_H__
#define __CLOUD_EVENTS_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

/* Error types includes */
#include "esp_rmaker_error_types.h"

/* Constants includes */
#include "constants/cloud.h"

/* Topic ctx (used by per-ctx response routing) */
#include "network/mqtt_topics.h"

/* Structure definitions *******************************************************/

/**
 * @brief Cloud 'set' event response.
 */
typedef struct {
    bool success; /* True if the event was successful, false otherwise */
    char *error_message; /* Error message if the event was not successful */
} esp_rmaker_cloud_event_set_response_t;

/**
 * @brief Cloud 'set' event response callback.
 * @param[in] p_response Pointer to the response.
 * @param[in] priv_data Private data passed to the callback.
 */
typedef void (*esp_rmaker_cloud_event_set_response_cb_t)(esp_rmaker_cloud_event_set_response_t *p_response, void *priv_data);

/**
 * @brief Cloud 'set' event response callback context.
 */
typedef struct {
    esp_rmaker_cloud_event_set_response_cb_t cb;
    void *priv_data;
} esp_rmaker_cloud_event_set_response_cb_context_t;

/**
 * @brief Cloud event.
 */
typedef struct {
    char *name; /* Event name */
    char *data; /* Event data */
    esp_rmaker_cloud_event_set_response_cb_context_t *p_set_response_cb_context; /* Pointer to the response callback context for 'set' events */
} esp_rmaker_cloud_event_t;

/**
 * @brief Cloud events tracker. Used to track if after processing an array of events, there may be more events to retrieve.
 *
 * Carries the topic ctx that owns the inbound payload so that any
 * follow-up events queued in ``events_pending`` can be published on the
 * correct Thing's ``to_cloud`` topic. NULL ``ctx`` is treated as self.
 */
typedef struct {
    uint16_t events_processed; /**< Events that have been processed. */
    uint16_t events_pending; /**< After processing, the number of events that are pending a send. */
    const esp_rmaker_topic_ctx_t *ctx; /**< Topic ctx for the source payload (NULL -> self). */
} esp_rmaker_cloud_events_tracker_t;

/**
 * @brief Function pointer to build a cloud event.
 * @param[in] p_event Pointer to build the event into.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
typedef esp_rmaker_error_t (*esp_rmaker_cloud_event_builder_t)(esp_rmaker_cloud_event_t *p_event);

/**
 * @brief Function pointer to handle a cloud event version response.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @param[in] version Version number.
 */
typedef void (*esp_rmaker_cloud_event_version_response_cb_t)(esp_rmaker_cloud_events_tracker_t *p_events_tracker, int version);

/**
 * @brief Function pointer to handle a cloud event details response.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @param[in] details Details string.
 * @param[in] version Version number.
 */
typedef void (*esp_rmaker_cloud_event_details_response_cb_t)(esp_rmaker_cloud_events_tracker_t *p_events_tracker, const char *details, int version);

/* Constants **********************************************************************/

/**
 * @brief Cloud event flag positions.
 */
typedef enum {
    RMAKER_CLOUD_EVENT_FLAG_POS_getGroupInfo = 0, /**< Get group info. */
    RMAKER_CLOUD_EVENT_FLAG_POS_getAlexaEn = 1, /**< Get Alexa enable. */
    RMAKER_CLOUD_EVENT_FLAG_POS_getSchedVer = 2, /**< Get schedule version. */
    RMAKER_CLOUD_EVENT_FLAG_POS_getSchedDetails = 3, /**< Get schedule details. */
    RMAKER_CLOUD_EVENT_FLAG_POS_getTriggerVer = 4, /**< Get trigger version. */
    RMAKER_CLOUD_EVENT_FLAG_POS_getTriggerDetails = 5, /**< Get trigger details. */
    RMAKER_CLOUD_EVENT_FLAG_POS_getGVAEn = 6, /**< Get GVA (Google Voice Assistant) enable. */
    RMAKER_CLOUD_EVENT_FLAG_POS_getTimeSync = 7, /**< Get current server time. */
} esp_rmaker_cloud_event_flag_pos_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cloud event builders. Indexed by RMAKER_CLOUD_EVENT_FLAG_POS_*.
 */
extern const esp_rmaker_cloud_event_builder_t RMAKER_CLOUD_EVENT_BUILDERS[];

/* Public function declarations *******************************************************/

/**
 * @brief Set cloud event 'getGroupInfo'.
 * @note This event is used to get the group information.
 *
 * @param[out] p_event Pointer to the event.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_cloud_event_getGroupInfo(esp_rmaker_cloud_event_t *p_event);

/**
 * @brief Handle 'getGroupInfo' response.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @param[in] primary Primary group name.
 * @param[in] subgroups Subgroup names. Must be an array of RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE strings.
 * @param[in] num_subgroups Number of subgroups.
 */
void esp_rmaker_cloud_event_response_getGroupInfo(esp_rmaker_cloud_events_tracker_t *p_events_tracker, const char *primary, char subgroups[][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE], size_t num_subgroups);

/**
 * @brief Set cloud event 'getAlexaEn'.
 * @note This event is used to check if Alexa is enabled.
 *
 * @param[out] p_event Pointer to the event.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_cloud_event_getAlexaEn(esp_rmaker_cloud_event_t *p_event);

/**
 * @brief Handle 'getAlexaEn' response.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @param[in] alexa_en True if Alexa is enabled, false otherwise.
 */
void esp_rmaker_cloud_event_response_getAlexaEn(esp_rmaker_cloud_events_tracker_t *p_events_tracker, bool alexa_en);

/**
 * @brief Set cloud event 'getGVAEn'.
 * @note This event is used to check if GVA notifications are enabled.
 *
 * @param[out] p_event Pointer to the event.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_cloud_event_getGVAEn(esp_rmaker_cloud_event_t *p_event);

/**
 * @brief Handle 'getGVAEn' response.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @param[in] gva_en True if GVA is enabled, false otherwise.
 */
void esp_rmaker_cloud_event_response_getGVAEn(esp_rmaker_cloud_events_tracker_t *p_events_tracker, bool gva_en);

/**
 * @brief Set cloud event 'getTimeSync'.
 * @note This event is used to get the current server time, so the node can
 *       coarse-set its clock without waiting for SNTP.
 *
 * @param[out] p_event Pointer to the event.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_cloud_event_getTimeSync(esp_rmaker_cloud_event_t *p_event);

/**
 * @brief Handle 'getTimeSync' response.
 * @note Sets the system time from the received value only if the clock is
 *       not already valid; SNTP remains authoritative.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @param[in] time_ms Server time in milliseconds since the Unix epoch (UTC).
 */
void esp_rmaker_cloud_event_response_getTimeSync(esp_rmaker_cloud_events_tracker_t *p_events_tracker, int64_t time_ms);

/**
 * @brief Set cloud event 'getSchedVer'.
 * @note This event is used to get the schedule version.
 * @param[out] p_event Pointer to the event.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_cloud_event_getSchedVer(esp_rmaker_cloud_event_t *p_event);

/**
 * @brief Handle 'getSchedVer' response.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @param[in] sched_ver Schedule version number.
 */
void esp_rmaker_cloud_event_response_getSchedVer(esp_rmaker_cloud_events_tracker_t *p_events_tracker, int sched_ver);

/**
 * @brief Set cloud event 'getSchedDetails'.
 * @note This event is used to get the schedule details.
 * @param[out] p_event Pointer to the event.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_cloud_event_getSchedDetails(esp_rmaker_cloud_event_t *p_event);

/**
 * @brief Handle 'getSchedDetails' response.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @param[in] sched_details Schedule details string.
 * @param[in] version Version number the details belong to.
 */
void esp_rmaker_cloud_event_response_getSchedDetails(esp_rmaker_cloud_events_tracker_t *p_events_tracker, const char *sched_details, int version);

/**
 * @brief Set cloud event 'getTriggerVer'.
 * @note This event is used to get the trigger version.
 * @param[out] p_event Pointer to the event.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_cloud_event_getTriggerVer(esp_rmaker_cloud_event_t *p_event);

/**
 * @brief Handle 'getTriggerVer' response.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @param[in] trigger_ver Trigger version number.
 */
void esp_rmaker_cloud_event_response_getTriggerVer(esp_rmaker_cloud_events_tracker_t *p_events_tracker, int trigger_ver);


/**
 * @brief Set cloud event 'getTriggerDetails'.
 * @note This event is used to get the trigger details.
 * @param[out] p_event Pointer to the event.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_cloud_event_getTriggerDetails(esp_rmaker_cloud_event_t *p_event);

/**
 * @brief Handle 'getTriggerDetails' response.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @param[in] trigger_details Trigger details string.
 * @param[in] version Version number the details belong to.
 */
void esp_rmaker_cloud_event_response_getTriggerDetails(esp_rmaker_cloud_events_tracker_t *p_events_tracker, const char *trigger_details, int version);

/**
 * @brief Set cloud event 'setNodeConfig'.
 * @note This event is used to set the node configuration.
 * @param[out] p_event Pointer to the event.
 * @param[in] node_config_str Node configuration string.
 * @param[in] p_set_response_cb_context Pointer to the response callback context for 'set' events. Should be allocated until the callback is called. Will be freed by the event handler.
 * @note The callback context is freed by the event handler.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_cloud_event_setNodeConfig(esp_rmaker_cloud_event_t *p_event, char *node_config_str, esp_rmaker_cloud_event_set_response_cb_context_t *p_set_response_cb_context);

/**
 * @brief Resolve the effective topic ctx for an events_tracker (NULL -> self).
 */
static inline const esp_rmaker_topic_ctx_t *esp_rmaker_cloud_events_tracker_ctx(const esp_rmaker_cloud_events_tracker_t *t)
{
    return (t && t->ctx) ? t->ctx : &esp_rmaker_topic_ctx_self;
}

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

/**
 * @brief Build the ``addChild`` cloud event.
 *
 * Request a cloud Thing be created for a bridged child of this bridge.
 * Sent via ::esp_rmaker_cloud_manager_send through the standard node
 * ``to_cloud`` channel. The cloud response arrives as a ``bridgeAck``
 * event on ``from_cloud`` and is routed to the bridge subsystem by the
 * cloud manager's 'b' dispatch.
 *
 * @param[out] p_event              Event to fill.
 * @param[in]  request_payload_json JSON object payload of the form
 *                                  ``{"request_id":"...","child_suffix":"...","bridge_local_id":"..."}``.
 *                                  Owned by the caller; remains live
 *                                  through ::esp_rmaker_cloud_manager_send.
 */
esp_rmaker_error_t esp_rmaker_cloud_event_addChild(
    esp_rmaker_cloud_event_t *p_event,
    char *request_payload_json);

/**
 * @brief Build the ``removeChild`` cloud event.
 *
 * @param[out] p_event              Event to fill.
 * @param[in]  request_payload_json JSON object payload of the form
 *                                  ``{"request_id":"...","child_thing_name":"..."}``.
 */
esp_rmaker_error_t esp_rmaker_cloud_event_removeChild(
    esp_rmaker_cloud_event_t *p_event,
    char *request_payload_json);

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* __CLOUD_EVENTS_H__ */
