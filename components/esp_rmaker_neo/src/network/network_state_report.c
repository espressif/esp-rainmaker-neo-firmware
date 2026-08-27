/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file network_state_report.c
 * @brief Handle state changes.
 */

/* Include files ****************************************************************/

/* Declarations */
#include "network/state_changes.h"

/* Network includes */
#include "network/common.h"
#include "network/mqtt_topics.h"
#include "network/mqtt_channels.h"

/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <inttypes.h>

/* Work queue includes */
#include "esp_rmaker_work_queue.h"
#include "esp_rmaker_runtime_gate.h"

/* Event loop includes */
#include "event_loop.h"

/* Core includes */
#include "core_internal.h"

/* Node model includes */
#include "data_model_internal.h"
#include "node_internal.h"
#include "timeseries.h"

/* Platform common includes */
#include "osal_log.h"
#include "osal_scheduler.h"
#include "osal_semaphore.h"
#include "osal_ticks.h"
#include "osal_mem_alloc.h"
#include "osal_time.h"
#include "osal_random.h"

/* Configuration includes */
#include "sdkconfig.h"
#include "local_config.h"

/* Network includes */
#include "constants/network.h"
#include "constants/identity.h"
#include "network/mqtt_topics.h"

/* Event flags includes */
#include "event_flags.h"

/* Checksum includes */
#include "checksum_impl.h"
#include "constants/nvs.h"

/* Time-sync includes */
#include "osal_timesync.h"

/* Hex formatting (ncfg_ver checksum -> hex string) */
#include "util/esp_rmaker_convert_hex.h"

/* Retry includes */
#include "retry/manager.h"

/* Automation service includes */
#include "services/automation.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
/* Bridge child synthetic-source-of-truth lookup for full reports. */
#include "bridge/bridge_internal.h"
#include "bridge/bridge_child_nvs.h"
#endif

/* Pre-processor definitions ******************************************************/

/**
 * @brief Delay in milliseconds for reporting state changes.
 */
#define STATE_REPORT_DELAY_MS CONFIG_RMAKER_STATE_REPORT_DELAY_MS

/**
 * @brief Minimum size of an empty shadow payload.
 *
 * i.e., {"state":{"reported":{}}} (26 characters, including the null terminator)
 */
#define STATE_SHADOW_PAYLOAD_MIN_SIZE 26

/* Types ************************************************************************/

/**
 * @brief Notify information.
 */
typedef struct {
    /** Version */
    int64_t version;
    /** Alexa enabled */
    bool alexa_enabled;
    /** GVA (Google Voice Assistant) notifications enabled */
    bool gva_enabled;
    /** SmartThings notifications enabled */
    bool st_enabled;
} state_report_notify_info_t;

/**
 * @brief Context for the state retry contexts.
 */
typedef struct {
    /**
     * @brief Retry manager context for the state subscribe retry.
     */
    retry_manager_context_t state_subscribe;
    /**
     * @brief Backoff context for the state report retry.
     */
    esp_rmaker_backoff_retry_context_t state_report;
} __state_retry_contexts_t;

/** Pending state subscribe acks (unicast + group control; bit set when 0). */
static atomic_uint __pending_state_subscribe_count;
/** Pending state unsubscribe acks (unicast + group control; bit set when 0). */
static atomic_uint __pending_state_unsub_count;

/* Global variables **************************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "rmng_net_state_rpt";

/**
 * @brief Mutex for state change management.
 */
static osal_semaphore_handle_t state_mutex;

/**
 * @brief Flag to indicate if the state report is for all parameters.
 */
static atomic_bool state_report_all = false;

/**
 * @brief Count of state report updates pending.
 */
static atomic_int_least8_t state_report_updates_pending = 0;

static __state_retry_contexts_t __state_retry_contexts = {0};

/* Per-node state pipeline lives embedded on the node as
 * ``node->state_update`` (see ::node_state_pipeline_state_t in
 * node_internal.h). The pipeline iterates nodes via
 * ::esp_rmaker_node_for_each; no separate list / self anchor is
 * maintained here.
 *
 * Helper: take a const node, return its mutable embedded pipeline
 * substruct. Callers already hold the state lock. */
static inline node_state_pipeline_state_t *__pipeline_of(const esp_rmaker_node_t *node)
{
    return node ? &((_esp_rmaker_node_t *)node)->state_update : NULL;
}

/* Static function declarations ****************************************************/

/**
 * @brief State subscribe work function.
 * - Subscribes to the 'params to node' topic.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __state_subscribe_work_fn(void *unused);

/**
 * @brief MQTT on complete callback.
 *
 * @param[in] event_handler_arg The argument to pass to the event handler.
 * @param[in] event_base The event base to register the event handler to.
 * @param[in] event_id The event id to register the event handler to.
 * @param[in] event_data The data to send with the event.
 */
static void __mqtt_on_complete_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief MQTT on subscribe callback common.
 *
 * @param[in] topic Topic on which the message was received.
 * @param[in] topic_len Length of the topic.
 * @param[in] payload Data received in the message.
 * @param[in] payload_len Length of the data.
 * @param[in] priv_data The private data passed during subscription.
 * @param[in] payload_work_fn The function to handle the payload.
 */
static void __mqtt_on_subscribe_cb_common(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data, esp_rmaker_work_fn_t payload_work_fn);

/**
 * @brief MQTT on subscribe callback.
 *
 * @param[in] topic Topic on which the message was received.
 * @param[in] topic_len Length of the topic.
 * @param[in] payload Data received in the message.
 * @param[in] payload_len Length of the data.
 * @param[in] priv_data The private data passed during subscription.
 */
static void __mqtt_on_subscribe_cb(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data);

/**
 * @brief MQTT subscribe callback for group control topics. Routes payload to the typed (device-type keyed) handler.
 *
 * @param[in] topic Topic on which the message was received.
 * @param[in] topic_len Length of the topic.
 * @param[in] payload Data received in the message.
 * @param[in] payload_len Length of the data.
 * @param[in] priv_data The private data passed during subscription.
 */
static void __mqtt_on_subscribe_group_cb(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data);

/**
 * @brief State change handler task.
 *
 * @param[in] payload Payload received from the network, of type esp_rmaker_network_payload_t.
 */
static void __state_change_handler_task(void *payload);

/**
 * @brief State change handler task for group control payloads (device-type keyed).
 *
 * @param[in] payload Payload received from the network, of type esp_rmaker_network_payload_t.
 */
static void __state_change_handler_group_task(void *payload);

/**
 * @brief State report scheduler task.
 *
 * @param[in] unused Unused argument.
 */
static void __state_report_scheduler_task(void *unused);

/**
 * @brief State report task. This is the actual task that reports the state, sent to the work queue.
 *
 * @param[in] unused Unused argument.
 */
static void __state_report_task(void *unused);

/**
 * @brief Populate the full state for a given node.
 *
 * @param[in] node Node whose state to populate. Pass NULL for the self node.
 *
 * @note This function must be called with the state lock held.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __populate_full_state_for_node(const esp_rmaker_node_t *node);

/**
 * @brief Populate the shadow payloads.
 *
 * @param[in] node Node whose shadow payloads to populate. Pass NULL for the self node.
 * @param[in] named_payload Pointer to the named payload. If NULL, then only the required size is reported to named_payload_len.
 * @param[in] named_payload_len Pointer to the length of the named payload.
 * @param[in] indexed_payload Pointer to the indexed payload. If NULL, then only the required size is reported to indexed_payload_len.
 * @param[in] indexed_payload_len Pointer to the length of the indexed payload.
 * @param[in] tags_changed If true, the tags have changed (via checksum comparison).
 * @param[in] p_notify_info Pointer to the notify information. If NULL, then no notify information is reported.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __populate_shadow_payloads(const esp_rmaker_node_t *node, char *named_payload, size_t *named_payload_len, char *indexed_payload, size_t *indexed_payload_len, bool tags_changed, state_report_notify_info_t *p_notify_info);

/* Build the named (params) shadow update topic for the given node. */
static int __build_named_topic_for_node(const esp_rmaker_node_t *node, char *buf, size_t size);
/* Build the indexed (iparams) shadow update topic for the given node. */
static int __build_indexed_topic_for_node(const esp_rmaker_node_t *node, char *buf, size_t size);
/* Per-node publish + reset routine, called by __state_report_task. */
static void __publish_node_locked(const esp_rmaker_node_t *node, uint8_t flags, state_report_notify_info_t *p_notify_info, bool *out_failed_any);

/**
 * @brief Reset the flags after populating the payloads.
 * - All report flags are reset.
 * - All parameter flags that AND to the flags passed in are reset.
 *
 * @param[in] flags Signal flags to reset. A '0' indicates to reset all flags. Of type esp_rmaker_signal_flags_t.
 */
static void __reset_flags(uint8_t flags);

/**
 * @brief Insert an update info into the list sorted by update ID.
 * @note This function must be called with the state lock held.
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_ALREADY_EXISTS if the update ID is already in the list, otherwise error code.
 */
static esp_rmaker_error_t __insert_update_info_into_list_sorted_locked(esp_rmaker_state_update_id_t update_id);

/**
 * @brief Insert/replace a flag-bearing or normal entry in the update list.
 *        See implementation comment for dedup semantics.
 */
static esp_rmaker_error_t __insert_update_info_into_list_sorted_locked_full(
    esp_rmaker_state_update_id_t update_id,
    uint8_t flags,
    esp_rmaker_state_update_flag_payload_t flag_payload);

/* Per-node variant of the above; target node == NULL means self. */
static esp_rmaker_error_t __insert_into_node_locked_full(
    const esp_rmaker_node_t *node,
    esp_rmaker_state_update_id_t update_id,
    uint8_t flags,
    esp_rmaker_state_update_flag_payload_t flag_payload);

/* Reset a single node's pending update list. */
static void __reset_node_update_list_locked(const esp_rmaker_node_t *node);

/**
 * @brief Get the checksum of the current node tags state.
 *
 * @param[in] node Node whose tags to checksum.
 * @param[out] checksum Pointer to the checksum, of length RMAKER_CHECKSUM_LEN.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __get_node_tags_checksum(const esp_rmaker_node_t *node, uint8_t checksum[RMAKER_CHECKSUM_LEN]);

/**
 * @brief Get the notify information. Internally this will:
 * - Set all flags based on the local configuration.
 * - Set a unique version number for the notify information.
 *
 * @param[out] p_notify_info Pointer to the notify information.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_STATE if no flags are set, otherwise error code.
 */
static esp_rmaker_error_t __get_notify_info(state_report_notify_info_t *p_notify_info);

/* Static function definitions ****************************************************/

static void __decrement_state_report_updates_pending(void)
{
    if (state_report_updates_pending > 0) {
        state_report_updates_pending--;
    }
    if (state_report_updates_pending == 0) {
        esp_rmaker_event_flags_set_state_reported();
    }
}

/* Visitor: commit (or discard) the pending tag-checksum snapshot stashed
 * on each node by ``__publish_node_locked``. On success the snapshot
 * becomes the new cached hash + persisted to the per-node NVS store.
 * On failure the cached hash is cleared so the next report re-emits. */
static esp_rmaker_error_t __commit_pending_tag_checksums_visitor(const esp_rmaker_node_t *node, void *priv)
{
    bool success = *(bool *)priv;
    _esp_rmaker_node_t *n = (_esp_rmaker_node_t *)node;
    if (!n) {
        return ESP_RMAKER_OK;
    }
    /* tag_check is per-node state; serialize against the report drain. */
    esp_rmaker_node_lock(node);
    if (!n->tag_check.pending_set) {
        esp_rmaker_node_unlock(node);
        return ESP_RMAKER_OK;
    }
    if (success) {
        memcpy(n->tag_check.committed, n->tag_check.pending, RMAKER_CHECKSUM_LEN);
        n->tag_check.committed_set = true;
        if (esp_rmaker_node_is_self(node)) {
            (void)esp_rmaker_checksum_store(n->tag_check.committed, RMAKER_NVS_CHECKSUM_KEY_NODE_TAGS);
        }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        else {
            esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
            if (child) {
                (void)bridge_child_nvs_set_node_tags(child, n->tag_check.committed);
            }
        }
#endif
    } else {
        /* Force re-emit on next report. */
        n->tag_check.committed_set = false;
    }
    n->tag_check.pending_set = false;
    esp_rmaker_node_unlock(node);
    return ESP_RMAKER_OK;
}

static void __commit_pending_tag_checksums(bool success)
{
    esp_rmaker_node_for_each(__commit_pending_tag_checksums_visitor, &success);
}

static esp_rmaker_error_t __state_subscribe_work_fn(void *timeout_ms_arg)
{
    uint32_t timeout_ms = (uint32_t)(uintptr_t)timeout_ms_arg;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
    char subgroups[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE];
    size_t num_subgroups = 0;
    unsigned int pending = 1; /* unicast */

    primary[0] = '\0';
    char *group_info_str = esp_rmaker_local_config_get_group_info_str();
    if (group_info_str != NULL) {
        if (esp_rmaker_local_config_parse_group_info_str(group_info_str, primary, sizeof(primary), subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, &num_subgroups) == ESP_RMAKER_OK && primary[0] != '\0') {
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
            /* Bridge mode: a single wildcard subscription replaces the
             * per-subgroup reconciliation. Bounded sub count, independent
             * of subgroup membership of bridge or children. */
            pending += 2; /* broadcast + wildcard subgroups */
#else
            pending += 1 + (unsigned int)num_subgroups; /* broadcast + one per subgroup */
#endif
        }
        free(group_info_str);
    }

    esp_rmaker_network_clear_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_STATE_CHANGES);
    atomic_store(&__pending_state_subscribe_count, pending);

    /* Subscribe to unicast topic */
    int topic_len = esp_rmaker_mqtt_topic_params_to_node(topic, sizeof(topic));
    if (topic_len < 0 || (size_t)topic_len >= sizeof(topic)) {
        OSAL_LOGE(TAG, "Failed to build params-to-node MQTT topic");
        return ESP_RMAKER_FAIL;
    }
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_STATE_CHANGES,
        .sub = MQTT_CHANNEL_SUB_STATE_CHANGE_START_LISTENING,
    };
    osal_err_t status = esp_rmaker_mqtt_impl.subscribe(&channel, topic, strlen(topic), __mqtt_on_subscribe_cb, QoS1, NULL);
    if (status != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to start listening for state changes: %d", status);
        return ESP_RMAKER_FAIL;
    }

    /* Subscribe to group control topics */
    if (primary[0] != '\0') {
        channel.sub = MQTT_CHANNEL_SUB_STATE_CHANGE_GROUP_CTRL_START_LISTENING;
        int len = esp_rmaker_mqtt_topic_group_control_broadcast(topic, sizeof(topic), primary);
        if (len < 0 || (size_t)len >= sizeof(topic)) {
            OSAL_LOGE(TAG, "Failed to build group control broadcast MQTT topic");
            return ESP_RMAKER_FAIL;
        }
        OSAL_LOGI(TAG, "Subscribing to group control broadcast: %s", topic);
        status = esp_rmaker_mqtt_impl.subscribe(&channel, topic, (size_t)len, __mqtt_on_subscribe_group_cb, QoS1, NULL);
        if (status != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to subscribe to group control broadcast: %d", status);
            return ESP_RMAKER_FAIL;
        }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        /* Bridge mode: one wildcard subscription over the subgroup
         * segment. The dispatcher (__state_change_handler_group_task)
         * parses <sg> off the inbound topic and filters fan-out via
         * esp_rmaker_node_is_in_subgroup. */
        len = esp_rmaker_mqtt_topic_group_control_subgroup_wildcard(topic, sizeof(topic), primary);
        if (len < 0 || (size_t)len >= sizeof(topic)) {
            OSAL_LOGE(TAG, "Failed to build group control subgroup wildcard MQTT topic");
            return ESP_RMAKER_FAIL;
        }
        OSAL_LOGI(TAG, "Subscribing to group control subgroups (wildcard): %s", topic);
        status = esp_rmaker_mqtt_impl.subscribe(&channel, topic, (size_t)len, __mqtt_on_subscribe_group_cb, QoS1, NULL);
        if (status != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to subscribe to group control subgroups wildcard: %d", status);
            return ESP_RMAKER_FAIL;
        }
        (void)subgroups; (void)num_subgroups;
#else
        for (size_t i = 0; i < num_subgroups; i++) {
            len = esp_rmaker_mqtt_topic_group_control_subgroup(topic, sizeof(topic), primary, subgroups[i]);
            if (len < 0 || (size_t)len >= sizeof(topic)) {
                OSAL_LOGE(TAG, "Failed to build group control subgroup MQTT topic");
                return ESP_RMAKER_FAIL;
            }
            OSAL_LOGI(TAG, "Subscribing to group control subgroup: %s", topic);
            status = esp_rmaker_mqtt_impl.subscribe(&channel, topic, (size_t)len, __mqtt_on_subscribe_group_cb, QoS1, NULL);
            if (status != OSAL_ERR_OK) {
                OSAL_LOGE(TAG, "Failed to subscribe to group control subgroup: %d", status);
                return ESP_RMAKER_FAIL;
            }
        }
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
    }

    esp_rmaker_error_t err = esp_rmaker_network_wait_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_STATE_CHANGES, timeout_ms);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to wait for subscribed to state changes: %d", err);
        return err;
    }
    OSAL_LOGI(TAG, "Started listening for state changes (unicast + group control)");
    return ESP_RMAKER_OK;
}

static void __mqtt_on_complete_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    osal_mqtt_event_loop_data_on_complete_t *mqtt_data = (osal_mqtt_event_loop_data_on_complete_t *)event_data;

    /* Check if the channel is for state changes */
    osal_mqtt_event_loop_channel_t channel = mqtt_data->channel;
    if (channel.main != MQTT_CHANNEL_MAIN_STATE_CHANGES) {
        return;
    }

    osal_err_t status = mqtt_data->status;
    bool status_success = status == OSAL_ERR_OK;
    const char *status_str = status_success ? "SUCCESS" : "FAILED";

    switch (channel.sub) {
    case MQTT_CHANNEL_SUB_STATE_CHANGE_UPDATE_NAMED:
        OSAL_LOGI(TAG, "Published state report (named): %s", status_str);
        __decrement_state_report_updates_pending();
        if (!status_success) {
            /* Force a full report; updates/flags might have been cleared */
            esp_rmaker_state_schedule_report(true);
        }
        break;
    case MQTT_CHANNEL_SUB_STATE_CHANGE_UPDATE_INDEXED:
        OSAL_LOGI(TAG, "Published state report (indexed): %s", status_str);
        __commit_pending_tag_checksums(status_success);
        if (!status_success) {
            /* Force a full report; updates/flags might have been cleared */
            esp_rmaker_state_schedule_report(true);
        }
        __decrement_state_report_updates_pending();
        break;
    case MQTT_CHANNEL_SUB_STATE_CHANGE_UPDATE_TIMESERIES:
        OSAL_LOGI(TAG, "Published timeseries data: %s", status_str);
        break;
    case MQTT_CHANNEL_SUB_STATE_CHANGE_DELETE:
        OSAL_LOGI(TAG, "Published delete of named shadow: %s", status_str);
        break;
    case MQTT_CHANNEL_SUB_STATE_CHANGE_START_LISTENING:
    case MQTT_CHANNEL_SUB_STATE_CHANGE_GROUP_CTRL_START_LISTENING: {
        unsigned int prev = atomic_fetch_sub(&__pending_state_subscribe_count, 1);
        if (status == OSAL_ERR_OK && prev == 1) {
            esp_rmaker_core_subscribed_to_params();
        }
        OSAL_LOGI(TAG, "Subscribed to state changes: %s", status_str);
        break;
    }
    case MQTT_CHANNEL_SUB_STATE_CHANGE_STOP_LISTENING:
    case MQTT_CHANNEL_SUB_STATE_CHANGE_GROUP_CTRL_STOP_LISTENING: {
        unsigned int prev = atomic_fetch_sub(&__pending_state_unsub_count, 1);
        if (status == OSAL_ERR_OK && prev == 1) {
            esp_rmaker_core_unsubscribed_from_params();
        }
        OSAL_LOGI(TAG, "Unsubscribed from state changes: %s", status_str);
        break;
    }
    default:
        break;
    }
}

static void __mqtt_on_subscribe_cb_common(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data, esp_rmaker_work_fn_t payload_work_fn)
{
    OSAL_LOGI(TAG, "Received message on topic: %.*s", (int)topic_len, topic);

    esp_rmaker_network_payload_t *p_payload = esp_rmaker_network_make_payload(payload, payload_len);
    if (!p_payload) {
        OSAL_LOGE(TAG, "Failed to make payload");
        return;
    }

    /* Add to work queue */
    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(payload_work_fn, p_payload);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add payload to work queue with esp_rmaker_error_t: %d", err);
        esp_rmaker_network_free_payload(p_payload);
    }
}

static void __mqtt_on_subscribe_cb(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data)
{
    __mqtt_on_subscribe_cb_common(topic, topic_len, payload, payload_len, priv_data, __state_change_handler_task);
}

static void __mqtt_on_subscribe_group_cb(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data)
{
    OSAL_LOGI(TAG, "Received message on topic: %.*s", (int)topic_len, topic);

    char subgroup[RMAKER_SUBGROUP_BUFFER_SIZE];
    esp_rmaker_error_t parse_err = esp_rmaker_mqtt_topic_parse_group_control_subgroup(
                                       topic, topic_len, subgroup, sizeof(subgroup));
    if (parse_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to parse group control topic, dropping message");
        return;
    }

    esp_rmaker_network_payload_t *p_payload = esp_rmaker_network_make_payload(payload, payload_len);
    if (!p_payload) {
        OSAL_LOGE(TAG, "Failed to make payload");
        return;
    }
    /* copy parsed subgroup string
     * (may be "" for the broadcast topic). */
    memcpy(p_payload->subgroup, subgroup, sizeof(p_payload->subgroup));

    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(__state_change_handler_group_task, p_payload);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add group payload to work queue with esp_rmaker_error_t: %d", err);
        esp_rmaker_network_free_payload(p_payload);
    }
}

static void __state_change_handler_task(void *payload)
{
    esp_rmaker_network_payload_t *p_payload = (esp_rmaker_network_payload_t *)payload;
    char *payload_str = (char *)p_payload->payload;
    size_t payload_len = p_payload->payload_len;

    OSAL_LOGD(TAG, "Received state changes: %s", payload_str);

    /* Self-shadow params payload - dispatch to the self node. */
    esp_rmaker_error_t err = data_model_state_handle_update_payload_json(
                                 esp_rmaker_get_node(), payload_str, payload_len, ESP_RMAKER_REQ_SRC_CLOUD);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to handle state changes with esp_rmaker_error_t: %d", err);
    }

    esp_rmaker_network_free_payload(p_payload);
}

/* Visitor used by __state_change_handler_group_task to fan a group-control
 * payload out to every node. Subgroup-aware: when ``subgroup`` is a
 * non-empty string, only nodes whose group_info_str names that subgroup
 * receive the dispatch; an empty string means broadcast. */
typedef struct {
    const char *payload;
    size_t payload_len;
    const char *subgroup;
} __group_dispatch_priv_t;

static esp_rmaker_error_t __group_dispatch_visitor(const esp_rmaker_node_t *node, void *priv)
{
    __group_dispatch_priv_t *p = (__group_dispatch_priv_t *)priv;
    if (p->subgroup[0] != '\0' && !esp_rmaker_node_is_in_subgroup(node, p->subgroup)) {
        return ESP_RMAKER_OK;
    }
    esp_rmaker_error_t err = data_model_state_handle_update_payload_json_group(
                                 node, p->payload, p->payload_len, ESP_RMAKER_REQ_SRC_CLOUD);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Group dispatch for node %p returned %d", (const void *)node, err);
    }
    return ESP_RMAKER_OK;
}

static void __state_change_handler_group_task(void *payload)
{
    esp_rmaker_network_payload_t *p_payload = (esp_rmaker_network_payload_t *)payload;
    char *payload_str = (char *)p_payload->payload;
    size_t payload_len = p_payload->payload_len;

    OSAL_LOGD(TAG, "Received group control state changes (sg='%s'): %s",
              p_payload->subgroup, payload_str);

    /* Group control: walk every node (self + ready children). For a
     * subgroup-targeted topic, the visitor filters by membership; for
     * broadcast (subgroup == ""), every node is dispatched. The
     * data-model handler matches devices by type within each node, so a
     * node that owns no device of the addressed type is a no-op. */
    __group_dispatch_priv_t priv = {
        .payload = payload_str,
        .payload_len = payload_len,
        .subgroup = p_payload->subgroup,
    };
    esp_rmaker_node_for_each(__group_dispatch_visitor, &priv);

    esp_rmaker_network_free_payload(p_payload);
}

static void __state_report_scheduler_task(void *unused)
{
    /* Add to work queue */
    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(__state_report_task, NULL);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add state report task to work queue with esp_rmaker_error_t: %d", err);
    }
}

typedef struct {
    uint8_t flags;
    state_report_notify_info_t *p_notify_info;
    bool *failed_any_publish;
} __state_report_visitor_priv_t;

static esp_rmaker_error_t __state_report_visitor(const esp_rmaker_node_t *node, void *priv)
{
    __state_report_visitor_priv_t *p = (__state_report_visitor_priv_t *)priv;
    /* __publish_node_locked operates on this node's embedded pipeline +
     * tag_check; hold the node lock. Children are visited under the bridge
     * pool lock (bridge > node ordering preserved). */
    esp_rmaker_node_lock(node);
    __publish_node_locked(node, p->flags, p->p_notify_info, p->failed_any_publish);
    esp_rmaker_node_unlock(node);
    return ESP_RMAKER_OK;
}

static void __publish_node_locked(const esp_rmaker_node_t *node_handle, uint8_t flags, state_report_notify_info_t *p_notify_info, bool *out_failed_any)
{
    node_state_pipeline_state_t *ctx = __pipeline_of(node_handle);
    if (!ctx || ctx->head == NULL) {
        return;
    }
    bool is_self = esp_rmaker_node_is_self(node_handle);
    const char *ctx_tag = is_self ? "self" : "child";
    _esp_rmaker_node_t *node = (_esp_rmaker_node_t *)node_handle;

    /* Per-node tags-changed: rehash this node's tags and compare against
     * the cached hash on the node itself. Empty tags -> not changed.
     * If the in-memory cache is unset (boot / post-reset), fall back to
     * NVS so reboots dedup correctly. */
    bool tags_changed = false;
    uint8_t tags_hash[RMAKER_CHECKSUM_LEN];
    if (node && __get_node_tags_checksum((const esp_rmaker_node_t *)node, tags_hash) == ESP_RMAKER_OK) {
        if (node->tag_check.committed_set) {
            tags_changed = memcmp(tags_hash, node->tag_check.committed, RMAKER_CHECKSUM_LEN) != 0;
        } else if (esp_rmaker_node_is_self((const esp_rmaker_node_t *)node)) {
            esp_rmaker_checksum_status_t cs =
                esp_rmaker_checksum_compare(tags_hash, RMAKER_NVS_CHECKSUM_KEY_NODE_TAGS);
            tags_changed = (cs != RMAKER_CHECKSUM_NOT_CHANGED);
            if (cs == RMAKER_CHECKSUM_NOT_CHANGED) {
                memcpy(node->tag_check.committed, tags_hash, RMAKER_CHECKSUM_LEN);
                node->tag_check.committed_set = true;
            }
        }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        else {
            esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node((const esp_rmaker_node_t *)node);
            bridge_child_nvs_record_t rec;
            if (child && bridge_child_nvs_load(child, &rec) == ESP_RMAKER_OK && rec.tags_checksum_set) {
                memcpy(node->tag_check.committed, rec.tags_checksum, RMAKER_CHECKSUM_LEN);
                node->tag_check.committed_set = true;
                tags_changed = memcmp(tags_hash, node->tag_check.committed, RMAKER_CHECKSUM_LEN) != 0;
            } else {
                tags_changed = true;
            }
        }
#endif
    }

    esp_rmaker_error_t err;
    char *named_payload = NULL, *indexed_payload = NULL;
    size_t named_payload_len = 0, indexed_payload_len = 0;

    /* Size pass. */
    err = __populate_shadow_payloads(node_handle, named_payload, &named_payload_len, indexed_payload, &indexed_payload_len, tags_changed, p_notify_info);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get shadow payload sizes (ctx thing=%s): %d", ctx_tag, err);
        goto end;
    }

    if (named_payload_len > STATE_SHADOW_PAYLOAD_MIN_SIZE) {
        named_payload = (char *)OSAL_CALLOC_EXTRAM(named_payload_len, sizeof(char));
        if (!named_payload) {
            OSAL_LOGE(TAG, "Failed to allocate named payload");
            goto end;
        }
    }
    if (indexed_payload_len > STATE_SHADOW_PAYLOAD_MIN_SIZE) {
        indexed_payload = (char *)OSAL_CALLOC_EXTRAM(indexed_payload_len, sizeof(char));
        if (!indexed_payload) {
            OSAL_LOGE(TAG, "Failed to allocate indexed payload");
            goto end;
        }
    }

    /* Fill pass. */
    err = __populate_shadow_payloads(node_handle, named_payload, &named_payload_len, indexed_payload, &indexed_payload_len, tags_changed, p_notify_info);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to populate payloads (ctx thing=%s): %d", ctx_tag, err);
        goto end;
    }

    bool has_named_payload = named_payload_len > STATE_SHADOW_PAYLOAD_MIN_SIZE;
    bool has_indexed_payload = indexed_payload_len > STATE_SHADOW_PAYLOAD_MIN_SIZE;
    state_report_updates_pending += has_named_payload + has_indexed_payload;

    char topic[MQTT_TOPIC_BUFFER_SIZE];
    int topic_len;
    bool ctx_failed = false;

    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_STATE_CHANGES,
        .sub = MQTT_CHANNEL_SUB_STATE_CHANGE_UPDATE_NAMED,
    };

    if (has_named_payload) {
        topic_len = __build_named_topic_for_node(node_handle, topic, sizeof(topic));
        if (topic_len < 0) {
            OSAL_LOGE(TAG, "Failed to build named topic for ctx thing=%s", ctx_tag);
            ctx_failed = true;
        } else {
            OSAL_LOGI(TAG, "Publishing named shadow: %s", topic);
            OSAL_LOGD(TAG, "Named shadow payload: %s", named_payload);
            osal_err_t status = esp_rmaker_mqtt_impl.publish(&channel, topic, (size_t)topic_len, named_payload, named_payload_len, QoS1, false);
            if (status != OSAL_ERR_OK) {
                OSAL_LOGE(TAG, "Failed to publish named shadow: %d", status);
                ctx_failed = true;
            }
        }
    }

    channel.sub = MQTT_CHANNEL_SUB_STATE_CHANGE_UPDATE_INDEXED;
    if (has_indexed_payload && !ctx_failed) {
        topic_len = __build_indexed_topic_for_node(node_handle, topic, sizeof(topic));
        if (topic_len < 0) {
            OSAL_LOGE(TAG, "Failed to build indexed topic for ctx thing=%s", ctx_tag);
            ctx_failed = true;
        } else {
            OSAL_LOGI(TAG, "Publishing indexed shadow: %s", topic);
            OSAL_LOGD(TAG, "Indexed shadow payload: %s", indexed_payload);
            /* Snapshot the tag hash before send so the indexed-ack handler
             * can commit it to ``node->tag_check.committed`` + NVS on success. */
            if (tags_changed && node) {
                memcpy(node->tag_check.pending, tags_hash, RMAKER_CHECKSUM_LEN);
                node->tag_check.pending_set = true;
            }
            osal_err_t status = esp_rmaker_mqtt_impl.publish(&channel, topic, (size_t)topic_len, indexed_payload, indexed_payload_len, QoS1, false);
            if (status != OSAL_ERR_OK) {
                OSAL_LOGE(TAG, "Failed to publish indexed shadow: %d", status);
                if (node) {
                    node->tag_check.pending_set = false;
                }
                ctx_failed = true;
            }
        }
    }

    /* Insert the values into timeseries queue */
    for (esp_rmaker_state_update_info_t *current = ctx->head; current != NULL; current = current->next) {
        if (current->flags != RMAKER_STATE_UPDATE_FLAG_NONE) {
            continue;
        }
        bool is_ts = false, is_ts_cumulative = false;
        err = data_model_timeseries_enabled_for_update_id(current->update_id, &is_ts, &is_ts_cumulative);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to check if update ID is a time series: %d", err);
            continue;
        }
        if (is_ts) {
            esp_rmaker_param_val_t val;
            err = data_model_state_update_id_get_value(current->update_id, &val);
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to get value of update ID: %d", err);
                continue;
            }
            /* Time-series points stamp a wall-clock timestamp. Validate the
             * entry's OWN timestamp (captured at insert time), not the clock
             * at publish time: in the decoupled flow the clock may sync
             * between insert and this flush, which would otherwise let a
             * ~1970 point through. Drop exactly the points stamped before the
             * clock was valid (also covers the ctx_failed republish path). */
            if (!osal_timesync_epoch_ms_is_valid((int64_t)current->timestamp_ms)) {
                OSAL_LOGW(TAG, "Dropping timeseries point with pre-sync timestamp (~%" PRIu32 ").",
                          (uint32_t)(current->timestamp_ms / 1000));
                continue;
            }
            /* Tag the ts entry with its owning topic ctx so the publish
             * path can pick the correct topic. */
            err = timeseries_push_data_new(esp_rmaker_node_topic_ctx(node_handle), current->update_id, &val, current->timestamp_ms, is_ts_cumulative);
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to push timeseries data: %d", err);
                continue;
            }
        }
    }

    if (!ctx_failed) {
        if (is_self) {
            __reset_flags(flags);
        }
        __reset_node_update_list_locked(node_handle);
    } else if (out_failed_any) {
        *out_failed_any = true;
    }

end:
    if (named_payload) {
        free(named_payload);
    }
    if (indexed_payload) {
        free(indexed_payload);
    }
}

static void __state_report_task(void *unused)
{
    /* Runtime gate: if the SDK is stopping/stopped/resetting, bail before
     * walking any node/state memory that reset may be concurrently freeing. */
    if (!esp_rmaker_should_do_work()) {
        OSAL_LOGD(TAG, "State reporting gated off; skipping report");
        return;
    }

    OSAL_LOGI(TAG, "Reporting state: %s", state_report_all ? "All parameters" : "Only changed parameters");

    /* Get flags */
    uint8_t flags = state_report_all ? 0 : RMAKER_SIGNAL_FLAG_VALUE_CHANGE;

    /* Reset state report all flag */
    bool was_state_report_all = state_report_all;
    state_report_all = false;

    bool failed_any_publish = false;

    /* Get the notify information (self-only). Reads local_config only - no
     * shared/per-node state mutation, so needs no lock. */
    state_report_notify_info_t notify_info, *p_notify_info = &notify_info;
    esp_rmaker_error_t err = __get_notify_info(p_notify_info);
    if (err != ESP_RMAKER_OK) {
        p_notify_info = NULL;
    }

    state_report_updates_pending = 0;

    /* Walk every node (self + ready bridge children via the node
     * visitor). Each publish-node call runs under that node's lock (taken
     * inside the visitor), computes its own tag-changed status against the
     * node's cached hash, and skips nodes with an empty pending list. The
     * global state_mutex is NOT held across the drain - per-node state is
     * serialized by the per-node lock. */
    __state_report_visitor_priv_t pub_ctx = { flags, p_notify_info, &failed_any_publish };
    esp_rmaker_node_for_each(__state_report_visitor, &pub_ctx);

    /* Global scheduler/retry bookkeeping is shared across nodes - serialize
     * it with the (now narrow) state_mutex. Never held while taking a node
     * or bridge lock. */
    esp_rmaker_state_lock();
    if (failed_any_publish) {
        OSAL_LOGE(TAG, "Failed to publish some data. Scheduling next retry...");
        /* Schedule the next retry with the same report all flag */
        state_report_all = was_state_report_all;
        esp_rmaker_error_t retry_err = esp_rmaker_backoff_retry(&__state_retry_contexts.state_report, __state_report_scheduler_task, NULL);
        if (retry_err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to schedule next state report retry: %d", retry_err);
        }
    } else {
        esp_rmaker_backoff_reset(&__state_retry_contexts.state_report, STATE_REPORT_DELAY_MS);
    }
    esp_rmaker_state_unlock();
}

static esp_rmaker_error_t __populate_full_state_for_node(const esp_rmaker_node_t *node)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Get all update IDs owned by node. */
    esp_rmaker_state_update_id_t *update_ids = NULL;
    size_t num_update_ids = 0;
    esp_rmaker_error_t err = data_model_state_update_id_get_all(node, &update_ids, &num_update_ids);
    if (err != ESP_RMAKER_OK) {
        char __tname[RMAKER_THING_NAME_BUFFER_SIZE];
        esp_rmaker_node_resolve_thing_name(node, __tname, sizeof(__tname));
        OSAL_LOGE(TAG, "Failed to get all update IDs for node '%s': %d", __tname, err);
        return err;
    }

    /* Stage synthetic ONLINE + NCFG_VER for the node. Sources differ by
     * node kind:
     *   - self: node status flags + local_config NVS.
     *   - child: bridge slot reachability bit + per-child NVS ncfg_ver,
     *            via the bridge layer's accessor.
     */
    if (esp_rmaker_node_is_self(node)) {
        _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
        bool online = (_node->status_flags & RMAKER_NODE_STATUS_FLAG_ONLINE) != 0;

        /* ncfg_ver is the persisted node-config SHA-256 checksum (change-
         * token, no wall clock). Stage it only once a checksum has been
         * stored (node config reported at least once); before that there
         * is nothing meaningful to report and the field is omitted. */
        uint8_t ncfg_hash[RMAKER_CHECKSUM_LEN];
        bool have_ncfg = (esp_rmaker_checksum_load(RMAKER_NVS_CHECKSUM_KEY_NODE_CONFIG, ncfg_hash) == ESP_RMAKER_OK);

        esp_rmaker_node_lock(node);
        (void)__insert_update_info_into_list_sorted_locked_full(NULL, RMAKER_STATE_UPDATE_FLAG_ONLINE,
        (esp_rmaker_state_update_flag_payload_t) {
            .online_value = online
        });
        if (have_ncfg) {
            esp_rmaker_state_update_flag_payload_t ncfg_payload = {0};
            memcpy(ncfg_payload.ncfg_ver_hash, ncfg_hash, RMAKER_CHECKSUM_LEN);
            (void)__insert_update_info_into_list_sorted_locked_full(NULL, RMAKER_STATE_UPDATE_FLAG_NCFG_VER, ncfg_payload);
        }
        esp_rmaker_node_unlock(node);
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    else {
        bool online = false;
        uint8_t ncfg_hash[RMAKER_CHECKSUM_LEN];
        bool have_ncfg = false;
        if (bridge_internal_child_get_report_synthetics(node, &online, ncfg_hash, &have_ncfg) == ESP_RMAKER_OK) {
            (void)esp_rmaker_state_mark_for_update_online_for_node(node, online);
            if (have_ncfg) {
                (void)esp_rmaker_state_mark_for_update_ncfg_ver_for_node(node, ncfg_hash);
            }
        } else {
            char __tname[RMAKER_THING_NAME_BUFFER_SIZE];
            esp_rmaker_node_resolve_thing_name(node, __tname, sizeof(__tname));
            OSAL_LOGW(TAG, "Unknown child '%s' - skipping synthetic staging", __tname);
        }
    }
#endif

    /* If there are no parameter update IDs, return early */
    if (num_update_ids == 0) {
        if (update_ids) {
            free(update_ids);
        }
        return ESP_RMAKER_OK;
    }

    bool inserted[num_update_ids];
    memset(inserted, false, num_update_ids * sizeof(bool));

    /* Insert the update IDs into the list. All update_ids belong to
     * ``node`` (from get_all(node)), so hold that node's lock. */
    esp_rmaker_node_lock(node);
    for (size_t i = 0; i < num_update_ids; i++) {
        err = __insert_update_info_into_list_sorted_locked(update_ids[i]);
        if (err != ESP_RMAKER_OK && err != ESP_RMAKER_ALREADY_EXISTS) {
            OSAL_LOGE(TAG, "Failed to insert update ID into list: %d", err);
            esp_rmaker_node_unlock(node);
            goto __populate_full_state_end;
        }

        /* Mark transfer of ownership of update ID to the info list */
        inserted[i] = true;
    }
    esp_rmaker_node_unlock(node);
    err = ESP_RMAKER_OK;

__populate_full_state_end:
    for (size_t i = 0; i < num_update_ids; i++) {
        if (!inserted[i] && update_ids[i]) {
            data_model_state_update_id_release(update_ids[i]);
        }
    }
    if (update_ids) {
        free(update_ids);
    }
    return err;
}

static void __shadow_common_start(json_gen_str_t *p_jstr)
{
    json_gen_start_object(p_jstr);
    json_gen_push_object(p_jstr, "state");
    json_gen_push_object(p_jstr, "reported");
}

static void __shadow_common_end(json_gen_str_t *p_jstr)
{
    json_gen_pop_object(p_jstr);
    json_gen_pop_object(p_jstr);
    json_gen_end_object(p_jstr);
}

static esp_rmaker_error_t __populate_shadow_payloads(const esp_rmaker_node_t *node_handle, char *named_payload, size_t *named_payload_len, char *indexed_payload, size_t *indexed_payload_len, bool tags_changed, state_report_notify_info_t *p_notify_info)
{
    if (!node_handle) {
        node_handle = esp_rmaker_get_node();
    }
    if (!node_handle) {
        OSAL_LOGE(TAG, "Node handle is NULL.");
        return ESP_RMAKER_INVALID_STATE;
    }
    bool is_self = esp_rmaker_node_is_self(node_handle);
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node_handle;
    node_state_pipeline_state_t *ctx = &_node->state_update;

    /* Initialize JSON generators */
    json_gen_str_t jstr_named, jstr_indexed;
    json_gen_str_start(&jstr_named, named_payload, *named_payload_len, NULL, NULL);
    json_gen_str_start(&jstr_indexed, indexed_payload, *indexed_payload_len, NULL, NULL);
    __shadow_common_start(&jstr_named);
    __shadow_common_start(&jstr_indexed);

    /* Report this node's tags. Per-node tags_changed gate. No per-tag
     * flag filter - when the hash differs we emit the full tag set so
     * the cloud sees the current snapshot in the indexed shadow. */
    if (_node->tags && tags_changed) {
        json_gen_push_object(&jstr_indexed, "data");
        json_gen_push_object(&jstr_indexed, "device");
        json_gen_push_object(&jstr_indexed, "t");

        esp_rmaker_tag_t *tag = _node->tags;
        while (tag) {
            json_gen_obj_set_string(&jstr_indexed, tag->name, tag->value);
            tag = tag->next;
        }
        json_gen_pop_object(&jstr_indexed);
        json_gen_pop_object(&jstr_indexed);
        json_gen_pop_object(&jstr_indexed);
    }

    /* Emit synthetic flag entries (ONLINE, NCFG_VER, ...) at the top
     * level of both shadows. Each flag entry maps to a fixed shadow field. */
    for (esp_rmaker_state_update_info_t *current = ctx->head; current != NULL; current = current->next) {
        if (current->flags & RMAKER_STATE_UPDATE_FLAG_ONLINE) {
            bool online = current->flag_payload.online_value;
            json_gen_obj_set_bool(&jstr_named, "online", online);
            json_gen_obj_set_bool(&jstr_indexed, "online", online);
        }
        if (current->flags & RMAKER_STATE_UPDATE_FLAG_NCFG_VER) {
            /* Render the node-config checksum bytes as a lowercase hex
             * string (no 0x). Cloud does equality-based change detection. */
            char ncfg_ver_hex[RMAKER_CHECKSUM_LEN * 2 + 1];
            if (esp_rmaker_convert_bytes_to_hex(current->flag_payload.ncfg_ver_hash,
                                                RMAKER_CHECKSUM_LEN,
                                                ncfg_ver_hex, sizeof(ncfg_ver_hex)) == ESP_RMAKER_OK) {
                json_gen_obj_set_string(&jstr_named, "ncfg_ver", ncfg_ver_hex);
                json_gen_obj_set_string(&jstr_indexed, "ncfg_ver", ncfg_ver_hex);
            }
        }
    }

    /* Report the parameters */
    if (ctx->count > 0) {
        bool has_indexed_updates = data_model_state_update_info_has_indexed_updates(ctx->head);
        if (has_indexed_updates) {
            json_gen_push_object(&jstr_indexed, "params");
        }
        json_gen_push_object(&jstr_named, "params");
        data_model_state_generate_update_payload_json(ctx->head, &jstr_named, has_indexed_updates ? &jstr_indexed : NULL);
        if (has_indexed_updates) {
            json_gen_pop_object(&jstr_indexed);
        }

        /* Report the notify information to named shadow (self only) */
        if (is_self && p_notify_info && p_notify_info->version > 0) {
            json_gen_push_object(&jstr_named, "notify");

            /* Overall notification version */
#if CONFIG_LIBC_NEWLIB_NANO_FORMAT
            // Newlib nano format does not support int64_t, so we cast to int.
            // (truncation is acceptable as long as the version number is different from the previous update)
            // TODO: remove this workaround once Newlib nano format is fully supported by the JSON generator.
            json_gen_obj_set_int(&jstr_named, "version", (int)(p_notify_info->version));
#else
            json_gen_obj_set_int64(&jstr_named, "version", p_notify_info->version);
#endif /* CONFIG_LIBC_NEWLIB_NANO_FORMAT */

            /* Alexa notification enabled */
            json_gen_obj_set_bool(&jstr_named, "alexa", p_notify_info->alexa_enabled);

            /* GVA notification enabled */
            json_gen_obj_set_bool(&jstr_named, "gva", p_notify_info->gva_enabled);

            /* SmartThings notification enabled */
            json_gen_obj_set_bool(&jstr_named, "smartthings", p_notify_info->st_enabled);

            json_gen_pop_object(&jstr_named);
        }

        json_gen_pop_object(&jstr_named);
    }

    /* ncfg_ver is now piggybacked on the update list (RMAKER_STATE_UPDATE_FLAG_NCFG_VER),
     * emitted by the synthetic-flag loop above. The previous
     * RMAKER_NODE_REPORT_FLAG_NCFG_VER path has been retired. */

    /* End the JSON generators */
    __shadow_common_end(&jstr_named);
    __shadow_common_end(&jstr_indexed);

    /* Get the payload lengths */
    *named_payload_len = json_gen_str_end(&jstr_named);
    *indexed_payload_len = json_gen_str_end(&jstr_indexed);

    return ESP_RMAKER_OK;
}

static void __reset_flags(uint8_t flags)
{
    /* Get node */
    const esp_rmaker_node_t *node = esp_rmaker_get_node();
    if (!node) {
        return;
    }
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;

    /* Reset the tag flags. (Node-level report flags retired together with
     * RMAKER_NODE_REPORT_FLAG_NCFG_VER - ncfg_ver is now piggybacked via
     * the state update list.) */
    esp_rmaker_tag_t *tag = _node->tags;
    while (tag) {
        tag->flags &= !flags ? ~RMAKER_SIGNAL_FLAG_VALUE_ALL : ~flags;
        tag = tag->next;
    }
}

static void __reset_node_update_list_locked(const esp_rmaker_node_t *node)
{
    node_state_pipeline_state_t *ctx = __pipeline_of(node);
    if (!ctx) {
        return;
    }
    while (ctx->head) {
        esp_rmaker_state_update_info_t *head = ctx->head;
        /* Only normal entries own a heap-allocated update_id. Flag-bearing
         * synthetic entries (e.g. ONLINE) carry their value inline. */
        if (head->flags == RMAKER_STATE_UPDATE_FLAG_NONE && head->update_id) {
            data_model_state_update_id_release(head->update_id);
        }
        ctx->head = head->next;
        free(head);
        ctx->count--;
    }
    ctx->head = NULL;
    ctx->count = 0;
}

/**
 * @brief Core insert/replace helper.
 *
 * Either ``update_id`` (parameter update) **or** ``flags`` (synthetic
 * entry) must be set, never both. The caller passes ``flag_payload`` for
 * synthetic entries (e.g. the online bool); it is ignored when
 * ``flags == 0``.
 *
 * Dedup keys:
 *  - flags == 0: another normal entry with a comparing-equal update_id.
 *  - flags != 0: another entry with the **exact same** ``flags`` bitmask
 *    (regardless of update_id).
 *
 * On dedup: the existing entry's value(s) are replaced in place and
 * ESP_RMAKER_ALREADY_EXISTS is returned.
 */
static esp_rmaker_error_t __insert_into_node_locked_full(
    const esp_rmaker_node_t *node,
    esp_rmaker_state_update_id_t update_id,
    uint8_t flags,
    esp_rmaker_state_update_flag_payload_t flag_payload)
{
    if (!node) {
        node = esp_rmaker_get_node();
    }
    node_state_pipeline_state_t *ctx = __pipeline_of(node);
    if (!ctx) {
        return ESP_RMAKER_INVALID_STATE;
    }
    if (flags == RMAKER_STATE_UPDATE_FLAG_NONE && !update_id) {
        OSAL_LOGE(TAG, "(insert) Update ID cannot be NULL for non-flag entry.");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Get the current timestamp in milliseconds */
    uint64_t timestamp_ms = osal_get_time_ms(NULL);

    esp_rmaker_state_update_info_t *current = ctx->head;
    esp_rmaker_state_update_info_t *previous = NULL;

    if (flags != RMAKER_STATE_UPDATE_FLAG_NONE) {
        /* Synthetic entry: dedup by exact-flag match. */
        while (current) {
            if (current->flags == flags) {
                if (flags & RMAKER_STATE_UPDATE_FLAG_ONLINE) {
                    current->flag_payload.online_value = flag_payload.online_value;
                } else if (flags & RMAKER_STATE_UPDATE_FLAG_NCFG_VER) {
                    memcpy(current->flag_payload.ncfg_ver_hash, flag_payload.ncfg_ver_hash, RMAKER_CHECKSUM_LEN);
                }
                current->timestamp_ms = timestamp_ms;
                return ESP_RMAKER_ALREADY_EXISTS;
            }
            previous = current;
            current = current->next;
        }
        /* Not found - fall through to allocation, append at end. */
    } else {
        /* Normal entry: skip flag entries during traversal (they sort by
         * key disjoint from update_ids). */
        while (current) {
            if (current->flags != RMAKER_STATE_UPDATE_FLAG_NONE) {
                previous = current;
                current = current->next;
                continue;
            }
            int compare = data_model_state_update_id_compare(update_id, current->update_id);
            if (compare == 0) {
                /* Already in the list; do a replacement */
                data_model_state_update_id_release(current->update_id);
                current->update_id = update_id;
                current->timestamp_ms = timestamp_ms;
                return ESP_RMAKER_ALREADY_EXISTS;
            }
            if (compare > 0) {
                /* Insert before current */
                goto do_insert;
            }
            previous = current;
            current = current->next;
        }
    }

    /* Insert at the end of the list */
do_insert:;
    esp_rmaker_state_update_info_t *new_update_info = OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_state_update_info_t));
    if (!new_update_info) {
        return ESP_RMAKER_NO_MEM;
    }
    new_update_info->update_id = update_id;
    new_update_info->flags = flags;
    new_update_info->flag_payload = flag_payload;
    new_update_info->timestamp_ms = timestamp_ms;
    new_update_info->next = current;
    if (previous) {
        previous->next = new_update_info;
    } else {
        ctx->head = new_update_info;
    }
    ctx->count++;

    return ESP_RMAKER_OK;
}

/* Self-targeted convenience wrappers used by the existing self-only
 * call sites (mark_for_update, __populate_full_state, etc.). */
static esp_rmaker_error_t __insert_update_info_into_list_sorted_locked_full(
    esp_rmaker_state_update_id_t update_id,
    uint8_t flags,
    esp_rmaker_state_update_flag_payload_t flag_payload)
{
    return __insert_into_node_locked_full(esp_rmaker_get_node(), update_id, flags, flag_payload);
}

static esp_rmaker_error_t __insert_update_info_into_list_sorted_locked(esp_rmaker_state_update_id_t update_id)
{
    /* Resolve the update_id's owning node; NULL -> self. The state ctx
     * is auto-created on first reference to a new node. */
    const esp_rmaker_node_t *owner_node = data_model_state_update_id_to_node(update_id);
    if (!owner_node) {
        owner_node = esp_rmaker_get_node();
    }
    return __insert_into_node_locked_full(owner_node, update_id, RMAKER_STATE_UPDATE_FLAG_NONE,
    (esp_rmaker_state_update_flag_payload_t) {
        0
    });
}

static int __compare_node_tags(const void *a, const void *b)
{
    return strcmp((*((esp_rmaker_tag_t **)a))->name, (*((esp_rmaker_tag_t **)b))->name);
}

static esp_rmaker_error_t __get_node_tags_checksum(const esp_rmaker_node_t *node, uint8_t checksum[RMAKER_CHECKSUM_LEN])
{
    if (!checksum || !node) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_tag_t *first_tag = esp_rmaker_node_get_first_tag(node);
    if (!first_tag) {
        return ESP_RMAKER_NOT_FOUND;
    }

    /* Format: <tag_name>:<tag_value>;<tag_name>:<tag_value>;... */

    /* First pass: get tag count and string buffer size */
    esp_rmaker_tag_t *tag = first_tag;
    size_t tag_count = 0;
    size_t string_buffer_size = 0;
    while (tag) {
        tag_count++;
        string_buffer_size += strlen(tag->name) + strlen(tag->value) + 2; // +2 for the : and ;
        tag = tag->next;
    }
    string_buffer_size++; // for the null terminator

    /* Make array of tag pointers */
    esp_rmaker_tag_t *tag_ptrs[tag_count];

    /* Second pass: fill array */
    int idx = 0; tag = first_tag;
    while (tag) {
        tag_ptrs[idx++] = tag;
        tag = tag->next;
    }

    /* Sort array by tag name */
    qsort(tag_ptrs, tag_count, sizeof(esp_rmaker_tag_t *), __compare_node_tags);

    /* Generate string */
    char checksum_str[string_buffer_size];
    idx = 0;
    for (int i = 0; i < tag_count; i++) {
        idx += snprintf(checksum_str + idx, string_buffer_size - idx, "%s:%s;", tag_ptrs[i]->name, tag_ptrs[i]->value);
    }

    /* Generate checksum */
    return esp_rmaker_checksum_generate((const uint8_t *)checksum_str, strlen(checksum_str), checksum);
}

static esp_rmaker_error_t __get_notify_info(state_report_notify_info_t *p_notify_info)
{
    if (!p_notify_info) {
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Get all flags based on local configuration */
    bool alexa_enabled = esp_rmaker_local_config_get_alexa_en();
    bool gva_enabled = esp_rmaker_local_config_get_gva_en();
    bool st_enabled = esp_rmaker_local_config_get_st_en();

    /* Return if no flags are set */
    bool at_least_one_flag_set = alexa_enabled || gva_enabled || st_enabled;
    if (!at_least_one_flag_set) {
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Set the version number as timestamp in milliseconds / 10.
     * Uniqueness is the only requirement; boot-relative time at 10 ms
     * resolution is sufficiently unique even without a synced clock, so
     * this is deliberately NOT gated on time sync. */
    uint64_t time_ms = osal_get_time_ms(NULL);
    p_notify_info->version = (int64_t)(time_ms / 10);

    /* Set the flags */
    p_notify_info->alexa_enabled = alexa_enabled;
    p_notify_info->gva_enabled = gva_enabled;
    p_notify_info->st_enabled = st_enabled;

    return ESP_RMAKER_OK;
}

/* Public function definitions ****************************************************/

esp_rmaker_error_t esp_rmaker_state_init(void)
{
    /* Register the MQTT on complete event handler */
    esp_rmaker_error_t err = event_loop_register_mqtt_on_complete_handler(__mqtt_on_complete_event_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to register MQTT on complete event handler: %d", err);
        return err;
    }

    /* Create the mutex */
    state_mutex = osal_semaphore_create_mutex();
    if (!state_mutex) {
        OSAL_LOGE(TAG, "Failed to create mutex for state change management");
        return ESP_RMAKER_FAIL;
    }

    /* Initialize the retry contexts */
    __state_retry_contexts = (__state_retry_contexts_t) {
        .state_subscribe = {
            .backoff = {
                .reset_on_success = true,
                .ctx = ESP_RMAKER_BACKOFF_DEFAULT_RETRY_CONTEXT(),
            },
            .task = {
                .func = __state_subscribe_work_fn,
                .priv_data = NULL,
            },
            .callbacks = {
                .on_failure = NULL,
            },
        },
        .state_report = {
            .handle = NULL,
            .delay_ctx = {
                .delay_ms = {
                    .current = STATE_REPORT_DELAY_MS,
                    .max = 5 * 60 * 1000, // 5 minutes
                },
                .params = {
                    .exp_factor = 2, // 2x the delay
                    .max_jitter_ms = 1000, // 1 second
                },
            },
        },
    };
    __state_retry_contexts.state_report.handle = NULL;
    __state_retry_contexts.state_subscribe.backoff.base_delay_ms = __state_retry_contexts.state_subscribe.backoff.ctx.delay_ctx.delay_ms.current;

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_state_deinit(void)
{
    /* Reset the state retry context */
    esp_rmaker_backoff_reset(&__state_retry_contexts.state_report, STATE_REPORT_DELAY_MS);

    /* Unregister the MQTT on complete event handler before tearing down mutex/list */
    esp_rmaker_error_t err = event_loop_unregister_mqtt_on_complete_handler(__mqtt_on_complete_event_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to unregister MQTT on complete event handler: %d", err);
        return err;
    }

    /* Reset every node's update info list (self + ready children), each
     * under its own lock. */
    esp_rmaker_state_drop_all_nodes();

    if (state_mutex) {
        osal_semaphore_delete(state_mutex);
        state_mutex = NULL;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_state_lock(void)
{
    osal_err_t err = osal_semaphore_take(state_mutex, OSAL_MAX_DELAY);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to lock state change management: %d", err);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_state_unlock(void)
{
    osal_err_t err = osal_semaphore_give(state_mutex);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to unlock state change management: %d", err);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

void esp_rmaker_state_attempt_start_listening(uint32_t timeout_ms)
{
    /* Execute the state subscribe retry */
    __state_retry_contexts.state_subscribe.task.func = __state_subscribe_work_fn;
    __state_retry_contexts.state_subscribe.task.priv_data = (void *)(uintptr_t)timeout_ms;
    retry_manager_execute_context(&__state_retry_contexts.state_subscribe);
}

esp_rmaker_error_t esp_rmaker_state_stop_listening(uint32_t timeout_ms)
{
    /* Stop any existing retries */
    retry_manager_stop_context(&__state_retry_contexts.state_subscribe);

    char topic[MQTT_TOPIC_BUFFER_SIZE];
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
    char subgroups[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE];
    size_t num_subgroups = 0;
    unsigned int pending = 1; /* unicast */

    primary[0] = '\0';
    char *group_info_str = esp_rmaker_local_config_get_group_info_str();
    if (group_info_str != NULL) {
        if (esp_rmaker_local_config_parse_group_info_str(group_info_str, primary, sizeof(primary), subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, &num_subgroups) == ESP_RMAKER_OK && primary[0] != '\0') {
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
            /* Mirror the subscribe path: bridge mode uses one wildcard unsubscribe for
             * all subgroups, so the ack count is fixed regardless of num_subgroups. */
            pending += 2; /* broadcast + wildcard subgroups */
#else
            pending += 1 + (unsigned int)num_subgroups; /* broadcast + one per subgroup */
#endif
        }
        free(group_info_str);
    }

    esp_rmaker_network_clear_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_STATE_CHANGES);
    atomic_store(&__pending_state_unsub_count, pending);

    bool failed_any = false;
    /* Unsubscribe from unicast topic */
    int topic_len = esp_rmaker_mqtt_topic_params_to_node(topic, sizeof(topic));
    if (topic_len < 0 || (size_t)topic_len >= sizeof(topic)) {
        OSAL_LOGE(TAG, "Failed to build params-to-node MQTT topic");
        return ESP_RMAKER_FAIL;
    }
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_STATE_CHANGES,
        .sub = MQTT_CHANNEL_SUB_STATE_CHANGE_STOP_LISTENING,
    };
    osal_err_t status = esp_rmaker_mqtt_impl.unsubscribe(&channel, topic, strlen(topic), QoS1);
    if (status != OSAL_ERR_OK) {
        OSAL_LOGW(TAG, "Failed to stop listening for state changes: %d", status);
        failed_any = true;
    }

    /* Unsubscribe from group control topics */
    if (primary[0] != '\0') {
        channel.sub = MQTT_CHANNEL_SUB_STATE_CHANGE_GROUP_CTRL_STOP_LISTENING;
        int len = esp_rmaker_mqtt_topic_group_control_broadcast(topic, sizeof(topic), primary);
        if (len < 0 || (size_t)len >= sizeof(topic)) {
            OSAL_LOGE(TAG, "Failed to build group control broadcast MQTT topic");
            failed_any = true;
        } else {
            OSAL_LOGI(TAG, "Unsubscribing from group control broadcast: %s", topic);
            status = esp_rmaker_mqtt_impl.unsubscribe(&channel, topic, (size_t)len, QoS1);
            if (status != OSAL_ERR_OK) {
                OSAL_LOGW(TAG, "Failed to unsubscribe from group control broadcast: %d", status);
                failed_any = true;
            }
        }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        len = esp_rmaker_mqtt_topic_group_control_subgroup_wildcard(topic, sizeof(topic), primary);
        if (len < 0 || (size_t)len >= sizeof(topic)) {
            OSAL_LOGE(TAG, "Failed to build group control subgroup wildcard MQTT topic");
            failed_any = true;
        } else {
            OSAL_LOGI(TAG, "Unsubscribing from group control subgroups (wildcard): %s", topic);
            status = esp_rmaker_mqtt_impl.unsubscribe(&channel, topic, (size_t)len, QoS1);
            if (status != OSAL_ERR_OK) {
                OSAL_LOGW(TAG, "Failed to unsubscribe from group control subgroups wildcard: %d", status);
                failed_any = true;
            }
        }
        (void)subgroups; (void)num_subgroups;
#else
        for (size_t i = 0; i < num_subgroups; i++) {
            len = esp_rmaker_mqtt_topic_group_control_subgroup(topic, sizeof(topic), primary, subgroups[i]);
            if (len < 0 || (size_t)len >= sizeof(topic)) {
                OSAL_LOGE(TAG, "Failed to build group control subgroup MQTT topic");
                failed_any = true;
            } else {
                OSAL_LOGI(TAG, "Unsubscribing from group control subgroup: %s", topic);
                status = esp_rmaker_mqtt_impl.unsubscribe(&channel, topic, (size_t)len, QoS1);
                if (status != OSAL_ERR_OK) {
                    OSAL_LOGW(TAG, "Failed to unsubscribe from group control subgroup: %d", status);
                    failed_any = true;
                }
            }
        }
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
    }

    if (failed_any) {
        OSAL_LOGE(TAG, "Failed to unsubscribe from state changes (unicast + group control)");
        return ESP_RMAKER_FAIL;
    }

    esp_rmaker_error_t err = esp_rmaker_network_wait_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_STATE_CHANGES, timeout_ms);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to wait for unsubscribed from state changes: %d", err);
        return err;
    }
    OSAL_LOGI(TAG, "Stopped listening for state changes (unicast + group control)");
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_state_stop_reporting(void)
{
    esp_rmaker_state_lock();
    esp_rmaker_backoff_reset(&__state_retry_contexts.state_report, STATE_REPORT_DELAY_MS);
    esp_rmaker_state_unlock();
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_state_schedule_report_for_node(const esp_rmaker_node_t *node, bool report_all)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Populate the full state for the given node if reporting all parameters */
    if (report_all) {
        esp_rmaker_error_t err = __populate_full_state_for_node(node);
        if (err != ESP_RMAKER_OK) {
            char __tname[RMAKER_THING_NAME_BUFFER_SIZE];
            esp_rmaker_node_resolve_thing_name(node, __tname, sizeof(__tname));
            OSAL_LOGE(TAG, "Failed to populate full state for '%s': %d", __tname, err);
            return err;
        }
    }

    /* The state_report_all flag is global by design: when any node schedules
     * a full report, the next drain emits both named (`reported`) and
     * indexed shadows for nodes that have a non-empty list, ensuring fresh
     * shadows aren't shadowed by stale indexed payloads. */
    state_report_all |= report_all;

    /* Schedule the report using the default delay */
    esp_rmaker_error_t err = esp_rmaker_backoff_fire(&__state_retry_contexts.state_report, __state_report_scheduler_task, NULL);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to schedule state report task: %d", err);
        return err;
    }

    char __tname[RMAKER_THING_NAME_BUFFER_SIZE];
    esp_rmaker_node_resolve_thing_name(node, __tname, sizeof(__tname));
    OSAL_LOGD(TAG, "Scheduled state report task for %s state ('%s')", state_report_all ? "entire" : "changed", __tname);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_state_schedule_report(bool report_all)
{
    return esp_rmaker_state_schedule_report_for_node(esp_rmaker_get_node(), report_all);
}

esp_rmaker_error_t esp_rmaker_state_report(bool report_all)
{
    /* Populate the full state if reporting all parameters */
    if (report_all) {
        esp_rmaker_error_t err = __populate_full_state_for_node(esp_rmaker_get_node());
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to populate full state: %d", err);
            return err;
        }
    }

    // Cancel any existing scheduled task.
    esp_rmaker_backoff_reset(&__state_retry_contexts.state_report, STATE_REPORT_DELAY_MS);

    // Report the state immediately.
    state_report_all |= report_all;
    __state_report_scheduler_task(NULL);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_state_mark_for_update_online_for_node(const esp_rmaker_node_t *node, bool online)
{
    if (!node) {
        node = esp_rmaker_get_node();
    }

    esp_rmaker_node_lock(node);
    esp_rmaker_error_t err = __insert_into_node_locked_full(
                                 node, NULL, RMAKER_STATE_UPDATE_FLAG_ONLINE,
    (esp_rmaker_state_update_flag_payload_t) {
        .online_value = online
    });
    esp_rmaker_node_unlock(node);
    if (err != ESP_RMAKER_OK && err != ESP_RMAKER_ALREADY_EXISTS) {
        OSAL_LOGE(TAG, "Failed to insert ONLINE update entry: %d", err);
        return err;
    }
    err = esp_rmaker_state_schedule_report(false);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to schedule report after mark online: %d", err);
    }
    return err;
}

esp_rmaker_error_t esp_rmaker_state_mark_for_update_ncfg_ver_for_node(const esp_rmaker_node_t *node, const uint8_t hash[RMAKER_CHECKSUM_LEN])
{
    if (!node) {
        node = esp_rmaker_get_node();
    }
    if (!hash) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_state_update_flag_payload_t payload = {0};
    memcpy(payload.ncfg_ver_hash, hash, RMAKER_CHECKSUM_LEN);

    esp_rmaker_node_lock(node);
    esp_rmaker_error_t err = __insert_into_node_locked_full(
                                 node, NULL, RMAKER_STATE_UPDATE_FLAG_NCFG_VER, payload);
    esp_rmaker_node_unlock(node);
    if (err != ESP_RMAKER_OK && err != ESP_RMAKER_ALREADY_EXISTS) {
        OSAL_LOGE(TAG, "Failed to insert NCFG_VER update entry: %d", err);
        return err;
    }
    err = esp_rmaker_state_schedule_report(false);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to schedule report after mark ncfg_ver: %d", err);
    }
    return err;
}

/* Per-node topic builders - resolve to the node's embedded topic ctx. */

static int __build_named_topic_for_node(const esp_rmaker_node_t *node, char *buf, size_t size)
{
    return esp_rmaker_mqtt_topic_params_named_shadow_update(esp_rmaker_node_topic_ctx(node), buf, size);
}

static int __build_indexed_topic_for_node(const esp_rmaker_node_t *node, char *buf, size_t size)
{
    return esp_rmaker_mqtt_topic_params_indexed_shadow_update(esp_rmaker_node_topic_ctx(node), buf, size);
}

/* Per-node pipeline lifecycle ************************************************
 *
 * State substruct lives embedded on the node (``node->state_update``),
 * so there's no separate find-or-create step - every node "exists" in
 * the pipeline from init to delete. ::esp_rmaker_state_drop_node is
 * called from ::esp_rmaker_node_delete to release the pending list
 * before the node memory goes away. The bridge child slot's
 * ``topic_ctx.valid`` is checked at publish time (visitor skips ready
 * children only) so there's nothing to "reap". */

void esp_rmaker_state_drop_node(const esp_rmaker_node_t *node)
{
    if (!node) {
        return;
    }
    esp_rmaker_node_lock(node);
    __reset_node_update_list_locked(node);
    esp_rmaker_node_unlock(node);
}

static esp_rmaker_error_t __drop_node_visitor(const esp_rmaker_node_t *node, void *priv)
{
    (void)priv;
    /* Per-node drop takes the node lock; runs under the bridge pool lock
     * for children (bridge -> node ordering). */
    esp_rmaker_state_drop_node(node);
    return ESP_RMAKER_OK;
}

void esp_rmaker_state_drop_all_nodes(void)
{
    esp_rmaker_node_for_each(__drop_node_visitor, NULL);
}

esp_rmaker_error_t esp_rmaker_state_delete_named_shadow_for_node(const esp_rmaker_node_t *node)
{
    if (!node) {
        node = esp_rmaker_get_node();
    }
    /* Get the topic for the named shadow delete */
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    int topic_len = esp_rmaker_mqtt_topic_params_named_shadow_delete(esp_rmaker_node_topic_ctx(node), topic, sizeof(topic));
    if (topic_len < 0 || (size_t)topic_len >= sizeof(topic)) {
        OSAL_LOGE(TAG, "Failed to build named shadow delete MQTT topic");
        return ESP_RMAKER_FAIL;
    }

    /* Publish the delete message */
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_STATE_CHANGES,
        .sub = MQTT_CHANNEL_SUB_STATE_CHANGE_DELETE,
    };
    osal_err_t status = esp_rmaker_mqtt_impl.publish(&channel, topic, strlen(topic), "{}", 2, QoS1, false);
    if (status != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to schedule delete of named shadow: %d", status);
        return ESP_RMAKER_FAIL;
    }

    OSAL_LOGI(TAG, "Scheduled delete of named shadow on topic: %s", topic);
    return ESP_RMAKER_OK;
}

const char *esp_rmaker_req_src_to_string(esp_rmaker_req_src_t src)
{
    switch (src) {
    case ESP_RMAKER_REQ_SRC_INIT:
        return "Initialization";
    case ESP_RMAKER_REQ_SRC_CLOUD:
        return "Cloud";
    case ESP_RMAKER_REQ_SRC_SCHEDULE:
        return "Schedule";
    case ESP_RMAKER_REQ_SRC_SCENE_ACTIVATE:
        return "Scene Activate";
    case ESP_RMAKER_REQ_SRC_SCENE_DEACTIVATE:
        return "Scene Deactivate";
    case ESP_RMAKER_REQ_SRC_LOCAL:
        return "Local";
    case ESP_RMAKER_REQ_SRC_FIRMWARE:
        return "Firmware";
    default:
        return "UNKNOWN";
    }
}

esp_rmaker_error_t esp_rmaker_state_mark_for_update(esp_rmaker_state_update_id_t update_id)
{
    if (!update_id) {
        OSAL_LOGE(TAG, "Update ID cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Read the value and fire automation triggers BEFORE handing the
     * update_id to the pending list. __insert_update_info_into_list_sorted_locked
     * transfers ownership of update_id to the list (and on a duplicate key
     * release()s the previous entry). The report drain runs on a separate task
     * and only holds the per-node lock during insert, so once inserted the
     * update_id may be released (freed) concurrently - dereferencing it after
     * insertion (get_value / to_node / check_and_fire) is a use-after-free. */
    esp_rmaker_param_val_t val;
    esp_rmaker_error_t err = data_model_state_update_id_get_value(update_id, &val);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get value of update ID: %d", err);
    }

    /* Check if the update ID should trigger any triggers it is registered to. */
    if (err == ESP_RMAKER_OK) {
        esp_rmaker_error_t fire_err = esp_rmaker_automation_service_update_id_check_and_fire(update_id, val);
        if (fire_err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to check and fire triggers: %d", fire_err);
        }
    }

    /* Resolve the owning node (NULL -> self) and lock it; the insert helper
     * re-resolves to the same owner. After insertion update_id is owned by
     * the list and must NOT be dereferenced here again. */
    const esp_rmaker_node_t *owner_node = data_model_state_update_id_to_node(update_id);
    if (!owner_node) {
        owner_node = esp_rmaker_get_node();
    }
    esp_rmaker_node_lock(owner_node);
    /* Add the update ID to the update info list */
    esp_rmaker_error_t insert_err = __insert_update_info_into_list_sorted_locked(update_id);
    esp_rmaker_node_unlock(owner_node);
    if (insert_err != ESP_RMAKER_OK && insert_err != ESP_RMAKER_ALREADY_EXISTS) {
        OSAL_LOGE(TAG, "Failed to insert update ID into update info list: %d", insert_err);
        return insert_err;
    }

    /* Schedule a report */
    err = esp_rmaker_state_schedule_report(false);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to schedule report: %d", err);
    }

    return err;
}
