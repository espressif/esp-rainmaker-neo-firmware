/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cloud_manager.c
 * @brief Implementation of the cloud manager.
 */

#include "network/cloud/manager.h"
#include "network/cloud/events.h"
#include "constants/identity.h"

/* Include files ****************************************************************/

/* Standard includes */
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdatomic.h>

/* Platform includes */
#include "osal_log.h"
#include "osal_semaphore.h"
#include "osal_event_group.h"
#include "osal_mem_alloc.h"

/* JSON includes */
#include "json_generator.h"
#include "json_parser.h"

/* Core includes */
#include "core_internal.h"

/* Network includes */
#include "network/common.h"
#include "network/mqtt_topics.h"
#include "network/mqtt_channels.h"

/* Work queue includes */
#include "esp_rmaker_work_queue.h"
#include "esp_rmaker_runtime_gate.h"

/* Retry manager includes */
#include "retry/manager.h"

/* Event loop includes */
#include "event_loop.h"

/* Constants includes */
#include "constants/cloud.h"
#include "constants/network.h"
#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "bridge/bridge_internal.h"
#endif


/* Structure definitions **********************************************************/

typedef struct {
    char *event_name;
    const esp_rmaker_topic_ctx_t *ctx;   /**< Owning topic ctx (self or child). */
    esp_rmaker_cloud_event_set_response_cb_context_t *p_set_response_cb_context;
} esp_rmaker_cloud_inbox_entry_t;

typedef struct {
    esp_rmaker_cloud_inbox_entry_t *entries;
    size_t num_entries;
    osal_semaphore_handle_t mutex;
} esp_rmaker_cloud_inbox_t;

/**
 * @brief Work-queue task argument. Wraps the raw payload plus the topic
 *        ctx the payload arrived on so the dispatcher knows which Thing
 *        the events should be scoped to.
 */
typedef struct {
    esp_rmaker_network_payload_t *p_payload;
    const esp_rmaker_topic_ctx_t *ctx;
} __cloud_manager_dispatch_task_arg_t;

typedef struct {
    uint32_t sub_channel;
    esp_rmaker_cloud_event_builder_t builder;
    esp_rmaker_cloud_event_version_response_cb_t version_response_cb;
} esp_rmaker_cloud_version_retry_context_t;

/* Constants **********************************************************************/

/**
 * @brief Base delay for version retry tasks.
 */
#define RMAKER_CLOUD_MANAGER_VERSION_RETRY_BASE_DELAY_MS 1000

/* Global variables **************************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "rmng_net_cloud_mgr";

/**
 * @brief Inbox.
 */
static esp_rmaker_cloud_inbox_t inbox;

static struct {
    esp_rmaker_cloud_version_retry_context_t sched;   /**< The retry context for the schedule version task */
    esp_rmaker_cloud_version_retry_context_t trigger; /**< The retry context for the trigger version task */
} version_retry_contexts;

/* Subscribe/unsubscribe co-gating ************************************************
 *
 * Listening setup may issue more than one MQTT subscribe (the self
 * from_cloud topic, plus the bridge_filter_cloud + bridge_filter_params
 * filters when the bridge is enabled). The ``SUBSCRIBED_TO_CLOUD`` bit
 * must reflect "all expected acks received successfully" rather than
 * "first ack arrived" - otherwise callers proceed before children's
 * from_cloud / params subscriptions are live. Same for the
 * unsubscribe path.
 *
 * Counters are atomic because acks are delivered on the MQTT event
 * loop's task, while the issuer + wait_bits live on the caller's
 * thread. The ``in_flight`` bools gate late acks from incorrectly
 * flipping the bit after the caller has already given up. */
static struct {
    atomic_uint subscribe_expected;
    atomic_uint subscribe_completed;
    atomic_bool subscribe_in_flight;
    atomic_uint unsubscribe_expected;
    atomic_uint unsubscribe_completed;
    atomic_bool unsubscribe_in_flight;
} __listening_gate;

/**
 * @brief Version retry contexts.
 */
static struct {
    retry_manager_context_t sched;   /**< The retry manager context for the schedule version task */
    retry_manager_context_t trigger; /**< The retry manager context for the trigger version task */
} version_retry_manager_contexts;

/* Static function declarations ***************************************************/

/**
 * @brief Lock inbox.
 */
static void __lock_inbox(void);

/**
 * @brief Unlock inbox.
 */
static void __unlock_inbox(void);

/**
 * @brief Make JSON payload.
 *
 * @param[in] p_event Pointer to the event.
 * @param[in] event_count Number of events to send.
 * @param[in] buf Pointer to the buffer.
 * @param[in] buf_size Size of the buffer.
 *
 * @return Required buffer size on success.
 * @return -1 on failure.
 */
static int __cloud_manager_make_json_payload(esp_rmaker_cloud_event_t *p_event, size_t event_count, char *buf, size_t buf_size);

/**
 * @brief On complete callback.
 *
 * @param[in] event_handler_arg The argument to pass to the event handler.
 * @param[in] event_base The event base to register the event handler to.
 * @param[in] event_id The event id to register the event handler to.
 * @param[in] event_data The data to send with the event.
 */
static void __cloud_manager_on_complete_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief Subscribe callback. We only really care about the payload.
 *
 * @param[in] topic Topic on which the message was received. Should always be the from_cloud topic.
 * @param[in] topic_len Length of the topic
 * @param[in] payload Data received in the message
 * @param[in] payload_len Length of the data
 * @param[in] priv_data The private data passed during subscription
 */
static void __cloud_manager_subscribe_cb( const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data );

/**
 * @brief Payload handler task (work queue worker).
 *
 * @param[in] arg Pointer to a heap-allocated ::__cloud_manager_dispatch_task_arg_t.
 */
static void __payload_handler_task(void *arg);

/**
 * @brief Get event response payload handler.
 *
 * @param[in] event_name Name of the event.
 * @param[in] p_jctx Pointer to the JSON context. Assumes that the object is already entered.
 * @param[in] p_events_tracker Pointer to the events tracker (carries ctx).
 * @note This function will leave the object.
 */
static void __get_event_response_payload_handler(char *event_name, jparse_ctx_t *p_jctx, esp_rmaker_cloud_events_tracker_t *p_events_tracker);

/**
 * @brief Set event response payload handler.
 *
 * @param[in] ctx Topic ctx the response was received on (used to scope
 *                the inbox lookup to (event_name, ctx)).
 * @param[in] event_name Name of the event.
 * @param[in] p_jctx Pointer to the JSON context. Assumes that the object is already entered.
 * @note This function will leave the object.
 */
static void __set_event_response_payload_handler(const esp_rmaker_topic_ctx_t *ctx, char *event_name, jparse_ctx_t *p_jctx);

/**
 * @brief Send any pending events.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @return bitmask of events that have not been processed. If all events have been processed, this will return 0.
 */
static uint16_t __send_any_pending_events(esp_rmaker_cloud_events_tracker_t *p_events_tracker);

/* --- Versioning --- */

/**
 * @brief Handle a details array event.
 * The JSON payload format for this event is:
 * @code
 * {
 *     "version": <version>,
 *     <details_key>: [
 *         // ... details ...
 *     ]
 * }
 * @endcode
 * @param[in] details_cb Details callback.
 * @param[in] p_events_tracker Pointer to the events tracker.
 * @param[in] p_jctx Pointer to the JSON context. Assumes that the object is already entered.
 * @param[in] details_key Details key.
 */
static void __handle_details_arr_event(esp_rmaker_cloud_event_details_response_cb_t details_cb, esp_rmaker_cloud_events_tracker_t *p_events_tracker, jparse_ctx_t *p_jctx, const char *details_key);

/**
 * @brief Version retry task.
 * @param[in] p_context_arg Pointer to the context argument.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __version_retry_task(void *p_context_arg);

/* Static function definitions ****************************************************/

static void __lock_inbox(void)
{
    osal_semaphore_take(inbox.mutex, OSAL_MAX_DELAY);
}

static void __unlock_inbox(void)
{
    osal_semaphore_give(inbox.mutex);
}

int __cloud_manager_make_json_payload(esp_rmaker_cloud_event_t *p_event, size_t event_count, char *buf, size_t buf_size)
{
    json_gen_str_t jpayload;
    json_gen_str_start(&jpayload, buf, buf_size, NULL, NULL);

    json_gen_start_object(&jpayload);

    /* Make events array */
    json_gen_push_array(&jpayload, "event");

    /* Add events */
    for (size_t i = 0; i < event_count; i++) {
        json_gen_arr_set_string(&jpayload, p_event[i].name);
    }

    json_gen_pop_array(&jpayload);

    /* Add additional payloads, if any */
    for (size_t i = 0; i < event_count; i++) {
        if (!p_event[i].data) {
            continue;
        }

        char first_char = p_event[i].data[0];
        if (first_char == '{') {
            json_gen_push_object_str(&jpayload, p_event[i].name, p_event[i].data);
        } else if (first_char == '[') {
            json_gen_push_array_str(&jpayload, p_event[i].name, p_event[i].data);
        } else {
            json_gen_obj_set_string(&jpayload, p_event[i].name, p_event[i].data);
        }
    }

    json_gen_end_object(&jpayload);

    return json_gen_str_end(&jpayload);
}

/* Channel classifiers for listening co-gating. Defined as functions
 * (not macros) so the call sites read cleanly. */
static bool __is_subscribe_ack(const osal_mqtt_event_loop_channel_t *ch)
{
    if (ch->main == MQTT_CHANNEL_MAIN_CLOUD_MANAGER &&
            ch->sub == MQTT_CHANNEL_SUB_CLOUD_MANAGER_START_LISTENING) {
        return true;
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    if (ch->main == MQTT_CHANNEL_MAIN_BRIDGE &&
            (ch->sub == MQTT_CHANNEL_SUB_BRIDGE_FILTER_CLOUD_SUBSCRIBE ||
             ch->sub == MQTT_CHANNEL_SUB_BRIDGE_FILTER_PARAMS_SUBSCRIBE)) {
        return true;
    }
#endif
    return false;
}

static bool __is_unsubscribe_ack(const osal_mqtt_event_loop_channel_t *ch)
{
    if (ch->main == MQTT_CHANNEL_MAIN_CLOUD_MANAGER &&
            ch->sub == MQTT_CHANNEL_SUB_CLOUD_MANAGER_STOP_LISTENING) {
        return true;
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    if (ch->main == MQTT_CHANNEL_MAIN_BRIDGE &&
            (ch->sub == MQTT_CHANNEL_SUB_BRIDGE_FILTER_CLOUD_UNSUBSCRIBE ||
             ch->sub == MQTT_CHANNEL_SUB_BRIDGE_FILTER_PARAMS_UNSUBSCRIBE)) {
        return true;
    }
#endif
    return false;
}

static void __cloud_manager_on_complete_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    osal_mqtt_event_loop_data_on_complete_t *mqtt_data = (osal_mqtt_event_loop_data_on_complete_t *)event_data;
    osal_mqtt_event_loop_channel_t channel = mqtt_data->channel;
    osal_err_t status = mqtt_data->status;
    const char *status_str = status == OSAL_ERR_OK ? "SUCCESS" : "FAILED";

    /* Listening co-gate: any of the N subscribe ack channels increments
     * the shared completed counter; when it reaches the expected count
     * we flip the SUBSCRIBED_TO_CLOUD bit. Late acks (after the caller
     * gave up) are dropped via the in_flight guard. */
    if (__is_subscribe_ack(&channel)) {
        OSAL_LOGI(TAG, "Cloud subscribe ack (main=%d sub=%d): %s",
                  (int)channel.main, (int)channel.sub, status_str);
        if (status == OSAL_ERR_OK && atomic_load(&__listening_gate.subscribe_in_flight)) {
            unsigned int prev = atomic_fetch_add(&__listening_gate.subscribe_completed, 1);
            unsigned int expected = atomic_load(&__listening_gate.subscribe_expected);
            /* >= (not ==): the issuer may lower subscribe_expected after a
             * partial subscribe failure, possibly after acks have already
             * passed the old target. esp_rmaker_core_subscribed_to_cloud() is
             * idempotent, so firing once we meet-or-exceed expected is safe. */
            if (prev + 1 >= expected) {
                esp_rmaker_core_subscribed_to_cloud();
            }
        }
        return;
    }
    if (__is_unsubscribe_ack(&channel)) {
        OSAL_LOGI(TAG, "Cloud unsubscribe ack (main=%d sub=%d): %s",
                  (int)channel.main, (int)channel.sub, status_str);
        if (status == OSAL_ERR_OK && atomic_load(&__listening_gate.unsubscribe_in_flight)) {
            unsigned int prev = atomic_fetch_add(&__listening_gate.unsubscribe_completed, 1);
            unsigned int expected = atomic_load(&__listening_gate.unsubscribe_expected);
            /* >= for the same reason as the subscribe gate: expected may be
             * lowered after a partial unsubscribe issuance failure. */
            if (prev + 1 >= expected) {
                esp_rmaker_core_unsubscribed_from_cloud();
            }
        }
        return;
    }

    /* Other channel events belong to the cloud manager itself. */
    if (channel.main != MQTT_CHANNEL_MAIN_CLOUD_MANAGER) {
        return;
    }

    switch (channel.sub) {
    case MQTT_CHANNEL_SUB_CLOUD_MANAGER_SEND:
        OSAL_LOGI(TAG, "Cloud publish event: %s", status_str);
        break;
    case MQTT_CHANNEL_SUB_CLOUD_MANAGER_VERSION_SCHEDULE:
        if (status != OSAL_ERR_OK) {
            retry_manager_resume_context(&version_retry_manager_contexts.sched);
        } else {
            retry_manager_stop_context(&version_retry_manager_contexts.sched);
        }
        break;
    case MQTT_CHANNEL_SUB_CLOUD_MANAGER_VERSION_TRIGGER:
        if (status != OSAL_ERR_OK) {
            retry_manager_resume_context(&version_retry_manager_contexts.trigger);
        } else {
            retry_manager_stop_context(&version_retry_manager_contexts.trigger);
        }
        break;
    default:
        break;
    }
}

static void __cloud_manager_subscribe_cb( const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data )
{
    /* Runtime gate: while stopping/stopped/resetting, drop the cloud event
     * before allocating or enqueuing. */
    if (!esp_rmaker_should_do_work()) {
        return;
    }

    esp_rmaker_network_payload_t *p_payload = esp_rmaker_network_make_payload(payload, payload_len);
    if (!p_payload) {
        OSAL_LOGE(TAG, "Failed to make payload");
        return;
    }

    __cloud_manager_dispatch_task_arg_t *arg = OSAL_CALLOC_EXTRAM(1, sizeof(*arg));
    if (!arg) {
        OSAL_LOGE(TAG, "Failed to allocate dispatch task arg");
        esp_rmaker_network_free_payload(p_payload);
        return;
    }
    arg->p_payload = p_payload;
    arg->ctx = &esp_rmaker_topic_ctx_self;

    /* Add to work queue */
    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(__payload_handler_task, arg);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add payload to work queue with esp_rmaker_error_t: %d", err);
        esp_rmaker_network_free_payload(p_payload);
        free(arg);
    }
}

/**
 * @brief Core dispatch logic - parses the JSON ``event`` array and
 *        routes each entry into the appropriate handler.
 *
 * Self path: ``ctx == &esp_rmaker_topic_ctx_self``.
 * Child path: ``ctx`` is the slot's stable topic ctx pointer.
 */
static void __dispatch_parsed_payload(const esp_rmaker_topic_ctx_t *ctx, const char *payload_str, size_t payload_len)
{
    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, (char *)payload_str, (int)payload_len) != 0) {
        OSAL_LOGE(TAG, "Failed to start JSON parser for payload: %.*s", (int)payload_len, payload_str);
        return;
    }
    char __tname[RMAKER_THING_NAME_BUFFER_SIZE];
    esp_rmaker_topic_ctx_resolve_thing_name(ctx, __tname, sizeof(__tname));
    OSAL_LOGD(TAG, "Cloud received payload (thing=%s): %.*s", __tname, (int)payload_len, payload_str);

    int num_events = 0;
    if (json_obj_get_array(&jctx, "event", &num_events) == 0) {
        /* Bound the stack buffer below: `num_events` is the payload's own array
         * length, so sizing a VLA straight off it lets a payload pick this task's
         * stack usage. */
        if (num_events > RMAKER_CLOUD_EVENT_MAX_COUNT) {
            OSAL_LOGW(TAG, "Payload carries %d events but at most %d are honoured; dropping the rest",
                      num_events, RMAKER_CLOUD_EVENT_MAX_COUNT);
            num_events = RMAKER_CLOUD_EVENT_MAX_COUNT;
        } else if (num_events <= 0) {
            OSAL_LOGW(TAG, "Payload carries %d events but at least 1 is required; dropping the payload", num_events);
            json_obj_leave_array(&jctx);
            json_parse_end(&jctx);
            return;
        }
        char event_names[num_events][RMAKER_CLOUD_EVENT_NAME_MAX_LEN];
        for (int i = 0; i < num_events; i++) {
            json_arr_get_string(&jctx, i, event_names[i], sizeof(event_names[i]));
        }
        json_obj_leave_array(&jctx);

        esp_rmaker_cloud_events_tracker_t events_tracker = {
            .events_processed = 0,
            .events_pending = 0,
            .ctx = ctx,
        };

        for (int i = 0; i < num_events; i++) {
            char *event_name = event_names[i];
            char first_char = event_name[0];
            if (json_obj_get_object(&jctx, event_name) != 0) {
                OSAL_LOGE(TAG, "Failed to get payload for event: %s", event_name);
                continue;
            }
            if (first_char == 'g') {
                __get_event_response_payload_handler(event_name, &jctx, &events_tracker);
            } else if (first_char == 's') {
                __set_event_response_payload_handler(ctx, event_name, &jctx);
            }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
            else if (first_char == 'b') {
                if (ctx != &esp_rmaker_topic_ctx_self) {
                    OSAL_LOGW(TAG, "Bridge event '%s' arriving on child '%s' - ignoring", event_name, __tname);
                    json_obj_leave_object(&jctx);
                    continue;
                }
                esp_rmaker_error_t b_err = bridge_internal_dispatch_from_cloud_event(event_name, &jctx);
                if (b_err == ESP_RMAKER_NOT_FOUND) {
                    OSAL_LOGW(TAG, "Bridge dispatcher: unknown event name '%s'", event_name);
                }
                json_obj_leave_object(&jctx);
            }
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
            else {
                OSAL_LOGE(TAG, "Received invalid event name: %s", event_name);
                json_obj_leave_object(&jctx);
            }
        }

        uint16_t pending_events = __send_any_pending_events(&events_tracker);

        /* Version retry on details-fetch failure is wired for the self
         * path only; the per-channel retry context bakes in self
         * semantics. Children fall back to the on-reconnect resync. */
        if (ctx == &esp_rmaker_topic_ctx_self) {
            if (pending_events & (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getSchedDetails)) {
                retry_manager_execute_context(&version_retry_manager_contexts.sched);
            }
            if (pending_events & (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getTriggerDetails)) {
                retry_manager_execute_context(&version_retry_manager_contexts.trigger);
            }
        }
    }
    json_parse_end(&jctx);
}

void cloud_manager_internal_dispatch_payload(const esp_rmaker_topic_ctx_t *ctx, const char *payload_str, size_t payload_len)
{
    if (!payload_str || payload_len == 0) {
        return;
    }
    if (!ctx) {
        ctx = &esp_rmaker_topic_ctx_self;
    }
    __dispatch_parsed_payload(ctx, payload_str, payload_len);
}

static void __payload_handler_task(void *arg_in)
{
    __cloud_manager_dispatch_task_arg_t *arg = (__cloud_manager_dispatch_task_arg_t *)arg_in;
    if (!arg) {
        return;
    }
    /* Runtime gate backstop: if stop/reset began after this task was enqueued,
     * drop the payload rather than dispatch handlers that walk node/state
     * memory reset may be freeing. */
    if (!esp_rmaker_should_do_work()) {
        if (arg->p_payload) {
            esp_rmaker_network_free_payload(arg->p_payload);
        }
        free(arg);
        return;
    }
    esp_rmaker_network_payload_t *p_payload = arg->p_payload;
    if (p_payload) {
        __dispatch_parsed_payload(arg->ctx,
                                  (const char *)p_payload->payload,
                                  p_payload->payload_len);
        esp_rmaker_network_free_payload(p_payload);
    }
    free(arg);
}

static void __get_event_response_payload_handler(char *event_name, jparse_ctx_t *p_jctx, esp_rmaker_cloud_events_tracker_t *p_events_tracker)
{
    /* Get group info */
    if (strcmp(event_name, "getGroupInfo") == 0) {
        char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
        char subgroups[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE];
        int num_subgroups = 0;
        if (json_obj_get_string(p_jctx, "pgrp", primary, sizeof(primary)) != 0) {
            OSAL_LOGW(TAG, "Failed to get primary group name, making empty primary group");
            primary[0] = '\0';
        }
        if (json_obj_get_array(p_jctx, "subgrps", &num_subgroups) == 0) {
            /* `num_subgroups` is the element count straight off the payload; `subgroups`
             * is a fixed stack array. Clamp before indexing it, or a "subgrps" array
             * longer than the array writes payload-controlled strings past its end. */
            if (num_subgroups > RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT) {
                OSAL_LOGW(TAG, "Got %d subgroups but at most %d are supported; dropping the rest",
                          num_subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT);
                num_subgroups = RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT;
            } else if (num_subgroups < 0) {
                num_subgroups = 0;
            }
            /* Have subgroups */
            for (int i = 0; i < num_subgroups; i++) {
                if (json_arr_get_string(p_jctx, i, subgroups[i], sizeof(subgroups[i])) != 0) {
                    OSAL_LOGW(TAG, "Failed to get subgroup name, making empty subgroup");
                    subgroups[i][0] = '\0';
                }
            }
            json_obj_leave_array(p_jctx);
        }

        esp_rmaker_cloud_event_response_getGroupInfo(p_events_tracker, primary, subgroups, num_subgroups);
    }

    /* Get Alexa enable */
    else if (strcmp(event_name, "getAlexaEn") == 0) {
        bool alexa_en;
        if (json_obj_get_bool(p_jctx, "enabled", &alexa_en) != 0) {
            OSAL_LOGE(TAG, "Failed to get Alexa enable");
            goto get_handler_end;
        }
        esp_rmaker_cloud_event_response_getAlexaEn(p_events_tracker, alexa_en);
    }

    /* Get GVA enable */
    else if (strcmp(event_name, "getGVAEn") == 0) {
        bool gva_en;
        if (json_obj_get_bool(p_jctx, "enabled", &gva_en) != 0) {
            OSAL_LOGE(TAG, "Failed to get GVA enable");
            goto get_handler_end;
        }
        esp_rmaker_cloud_event_response_getGVAEn(p_events_tracker, gva_en);
    }

    /* Get schedule version */
    else if (strcmp(event_name, "getSchedVer") == 0) {
        int sched_ver;
        if (json_obj_get_int(p_jctx, "version", &sched_ver) != 0) {
            OSAL_LOGE(TAG, "Failed to get schedule version");
            goto get_handler_end;
        }
        esp_rmaker_cloud_event_response_getSchedVer(p_events_tracker, sched_ver);
    }

    /* Get trigger version */
    else if (strcmp(event_name, "getTriggerVer") == 0) {
        int trigger_ver;
        if (json_obj_get_int(p_jctx, "version", &trigger_ver) != 0) {
            OSAL_LOGE(TAG, "Failed to get trigger version");
            goto get_handler_end;
        }
        esp_rmaker_cloud_event_response_getTriggerVer(p_events_tracker, trigger_ver);
    }

    /* Get schedule details */
    else if (strcmp(event_name, "getSchedDetails") == 0) {
        static const char *details_key = "Schedules";
        __handle_details_arr_event(esp_rmaker_cloud_event_response_getSchedDetails, p_events_tracker, p_jctx, details_key);
    }

    /* Get trigger details */
    else if (strcmp(event_name, "getTriggerDetails") == 0) {
        static const char *details_key = "triggers";
        __handle_details_arr_event(esp_rmaker_cloud_event_response_getTriggerDetails, p_events_tracker, p_jctx, details_key);
    }

    /* Get time sync */
    else if (strcmp(event_name, "getTimeSync") == 0) {
        int64_t time_ms;
        if (json_obj_get_int64(p_jctx, "time", &time_ms) != 0) {
            OSAL_LOGE(TAG, "Failed to get time sync value");
            goto get_handler_end;
        }
        esp_rmaker_cloud_event_response_getTimeSync(p_events_tracker, time_ms);
    }

get_handler_end:
    json_obj_leave_object(p_jctx);
}

static void __set_event_response_payload_handler(const esp_rmaker_topic_ctx_t *ctx, char *event_name, jparse_ctx_t *p_jctx)
{
    /* Find callback context for (event_name, ctx). */
    esp_rmaker_cloud_event_set_response_cb_context_t *p_set_response_cb_context = NULL;

    __lock_inbox();
    for (size_t i = 0; i < inbox.num_entries; i++) {
        if (inbox.entries[i].event_name == NULL) {
            continue;
        }
        if (strcmp(event_name, inbox.entries[i].event_name) != 0) {
            continue;
        }
        if (inbox.entries[i].ctx != ctx) {
            continue;
        }
        p_set_response_cb_context = inbox.entries[i].p_set_response_cb_context;
        inbox.entries[i].p_set_response_cb_context = NULL;
        inbox.entries[i].ctx = NULL;
        break;
    }
    __unlock_inbox();

    if (!p_set_response_cb_context) {
        char __tname_cb[RMAKER_THING_NAME_BUFFER_SIZE];
        esp_rmaker_topic_ctx_resolve_thing_name(ctx, __tname_cb, sizeof(__tname_cb));
        OSAL_LOGE(TAG, "Failed to find callback context for event '%s' on thing=%s", event_name, __tname_cb);
        goto set_handler_end;
    }

    /* Parse payload */
    esp_rmaker_cloud_event_set_response_t response;

    /* Check success */
    char status[10];
    if (json_obj_get_string(p_jctx, "status", status, sizeof(status)) != 0) {
        OSAL_LOGE(TAG, "Failed to get status");
        goto set_handler_end;
    }
    response.success = strcmp(status, "success") == 0;
    if (!response.success) {
        /* Get error message */
        char error_message[100];
        if (json_obj_get_string(p_jctx, "message", error_message, sizeof(error_message)) == 0) {
            response.error_message = error_message;
        }
    } else {
        response.error_message = NULL;
    }

    /* Call callback and free */
    p_set_response_cb_context->cb(&response, p_set_response_cb_context->priv_data);
    free(p_set_response_cb_context);

set_handler_end:
    json_obj_leave_object(p_jctx);
}

static uint16_t __send_any_pending_events(esp_rmaker_cloud_events_tracker_t *p_events_tracker)
{
    /* Get all events that are pending */
    uint16_t events_pending = p_events_tracker->events_pending & ~p_events_tracker->events_processed;
    if (!events_pending) {
        return 0;
    }

    /* Count the number of set bits in events_pending */
    uint16_t num_events = 0, temp = events_pending;
    while (temp) {
        num_events += temp & 1;
        temp >>= 1;
    }

    /* Build and send events */
    esp_rmaker_cloud_event_t events[num_events];
    int idx = 0, pos = 0;
    temp = events_pending;
    while (temp > 0) {
        if (temp & 1) {
            RMAKER_CLOUD_EVENT_BUILDERS[pos](&events[idx++]);
        }
        temp >>= 1;
        pos++;
    }
    const esp_rmaker_topic_ctx_t *ctx = esp_rmaker_cloud_events_tracker_ctx(p_events_tracker);
    esp_rmaker_error_t err = esp_rmaker_cloud_manager_send(ctx, events, num_events, MQTT_CHANNEL_SUB_CLOUD_MANAGER_SEND);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to send pending events with esp_rmaker_error_t: %d", err);
        return events_pending;
    }

    /* Treat all events as processed */
    return 0;
}

static void __handle_details_arr_event(esp_rmaker_cloud_event_details_response_cb_t details_cb, esp_rmaker_cloud_events_tracker_t *p_events_tracker, jparse_ctx_t *p_jctx, const char *details_key)
{
    int version = -1;
    if (json_obj_get_int(p_jctx, "version", &version) != 0) {
        OSAL_LOGE(TAG, "Failed to get version for details '%s'", details_key);
        version = -1;
    }

    int details_len = 0;
    if (json_obj_get_array_strlen(p_jctx, details_key, &details_len) != 0) {
        OSAL_LOGD(TAG, "Failed to get details length for '%s', sending empty details", details_key);
        details_cb(p_events_tracker, "[]", version);
        return;
    }
    OSAL_LOGD(TAG, "Details '%s' length: %d", details_key, details_len);
    if (details_len < 0) {
        OSAL_LOGE(TAG, "Negative details length %d for '%s'", details_len, details_key);
        return;
    }
    /* Heap, not a stack buffer sized from the payload: a full schedule or trigger
     * list is legitimately large, so any stack ceiling tight enough to be safe on the
     * 4 KB work-queue task would truncate real data. Failing the allocation degrades
     * to "event ignored" instead of smashing the stack. */
    char *details = OSAL_CALLOC_EXTRAM(1, (size_t)details_len + 1);
    if (details == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate %d bytes for details '%s'", details_len + 1, details_key);
        return;
    }
    if (json_obj_get_array_str(p_jctx, details_key, details, details_len + 1) != 0) {
        OSAL_LOGE(TAG, "Failed to get details '%s'", details_key);
        free(details);
        return;
    }
    details[details_len] = '\0';
    details_cb(p_events_tracker, details, version);
    free(details);
}

static esp_rmaker_error_t __version_retry_task(void *p_context_arg)
{
    esp_rmaker_cloud_version_retry_context_t *p_context = (esp_rmaker_cloud_version_retry_context_t *)p_context_arg;
    if (p_context == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    uint32_t sub_channel = p_context->sub_channel;

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    esp_rmaker_cloud_event_t event;
    esp_rmaker_cloud_events_tracker_t events_tracker = {0};
    if (p_context->builder(&event) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get version for channel %" PRIu32, sub_channel);
        err = ESP_RMAKER_INVALID_ARG;
        goto __version_retry_task_fail;
    }
    /* Version retry is self-only (see dispatch path). */
    events_tracker.ctx = &esp_rmaker_topic_ctx_self;
    err = esp_rmaker_cloud_manager_send(&esp_rmaker_topic_ctx_self, &event, 1, sub_channel);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to send version for channel %" PRIu32 " with esp_rmaker_error_t: %d", sub_channel, err);
        err = ESP_RMAKER_FAIL;
        goto __version_retry_task_fail;
    }
    return ESP_RMAKER_OK;

__version_retry_task_fail:
    p_context->version_response_cb(&events_tracker, -1);
    return err;
}

/* Public function definitions ****************************************************/

esp_rmaker_error_t esp_rmaker_cloud_manager_init(void)
{
    /* Register the event handler */
    esp_rmaker_error_t err = event_loop_register_mqtt_on_complete_handler(__cloud_manager_on_complete_event_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to register MQTT on complete event handler with esp_rmaker_error_t: %d", err);
        return err;
    }

    /* Inbox: one slot for self + one per child slot, dynamically populated
     * on first-use registration in esp_rmaker_cloud_manager_send. */
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    inbox.num_entries = 1 + CONFIG_RMNG_BRIDGE_MAX_CHILDREN;
#else
    inbox.num_entries = 1;
#endif
    inbox.entries = OSAL_CALLOC_EXTRAM(inbox.num_entries, sizeof(esp_rmaker_cloud_inbox_entry_t));
    if (!inbox.entries) {
        OSAL_LOGE(TAG, "Failed to allocate memory for inbox entries");
        inbox.num_entries = 0;
        return ESP_RMAKER_FAIL;
    }

    /* Create mutex */
    inbox.mutex = osal_semaphore_create_mutex();
    if (!inbox.mutex) {
        OSAL_LOGE(TAG, "Failed to create mutex for inbox");
        return ESP_RMAKER_FAIL;
    }

    /* Initialize the version retry contexts */
    version_retry_contexts.sched = (esp_rmaker_cloud_version_retry_context_t) {
        .sub_channel = MQTT_CHANNEL_SUB_CLOUD_MANAGER_VERSION_SCHEDULE,
        .builder = esp_rmaker_cloud_event_getSchedVer,
        .version_response_cb = esp_rmaker_cloud_event_response_getSchedVer,
    };
    version_retry_contexts.trigger = (esp_rmaker_cloud_version_retry_context_t) {
        .sub_channel = MQTT_CHANNEL_SUB_CLOUD_MANAGER_VERSION_TRIGGER,
        .builder = esp_rmaker_cloud_event_getTriggerVer,
        .version_response_cb = esp_rmaker_cloud_event_response_getTriggerVer,
    };

    version_retry_manager_contexts.sched = (retry_manager_context_t) {
        .backoff = {
            .base_delay_ms = RMAKER_CLOUD_MANAGER_VERSION_RETRY_BASE_DELAY_MS,
            .reset_on_success = false,
            .ctx = ESP_RMAKER_BACKOFF_DEFAULT_RETRY_CONTEXT(),
        },
        .task = {
            .func = __version_retry_task,
            .priv_data = (void *) &version_retry_contexts.sched,
        },
        .callbacks = {
            .on_failure = NULL,
        },
    };
    version_retry_manager_contexts.trigger = (retry_manager_context_t) {
        .backoff = {
            .base_delay_ms = RMAKER_CLOUD_MANAGER_VERSION_RETRY_BASE_DELAY_MS,
            .reset_on_success = false,
            .ctx = ESP_RMAKER_BACKOFF_DEFAULT_RETRY_CONTEXT(),
        },
        .task = {
            .func = __version_retry_task,
            .priv_data = (void *) &version_retry_contexts.trigger,
        },
        .callbacks = {
            .on_failure = NULL,
        },
    };

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_cloud_manager_deinit(void)
{
    /* Stop any pending version retries */
    retry_manager_stop_context(&version_retry_manager_contexts.sched);
    retry_manager_stop_context(&version_retry_manager_contexts.trigger);

    /* Free the inbox entries and mutex */
    if (inbox.entries) {
        free(inbox.entries);
    }
    inbox.entries = NULL;
    inbox.num_entries = 0;

    if (inbox.mutex) {
        osal_semaphore_delete(inbox.mutex);
        inbox.mutex = NULL;
    }

    /* Unregister the event handler */
    esp_rmaker_error_t err = event_loop_unregister_mqtt_on_complete_handler(__cloud_manager_on_complete_event_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to unregister MQTT on complete event handler with esp_rmaker_error_t: %d", err);
        return err;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_cloud_manager_start_listening(uint32_t timeout_ms)
{
    char from_cloud_topic[MQTT_TOPIC_BUFFER_SIZE];
    int topic_len = esp_rmaker_mqtt_topic_from_cloud(from_cloud_topic, sizeof(from_cloud_topic));
    if (topic_len < 0 || (size_t)topic_len >= sizeof(from_cloud_topic)) {
        OSAL_LOGE(TAG, "Failed to build from_cloud MQTT topic");
        return ESP_RMAKER_FAIL;
    }

    /* Co-gate: self start_listening + (when bridge enabled) the two
     * bridge filter subscribes all land on the SUBSCRIBED_TO_CLOUD bit
     * through the shared ack counter. Reset gate state before issuing. */
    unsigned int expected = 1;
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    expected += BRIDGE_INTERNAL_FILTER_SUB_COUNT;
#endif
    atomic_store(&__listening_gate.subscribe_expected, expected);
    atomic_store(&__listening_gate.subscribe_completed, 0);
    atomic_store(&__listening_gate.subscribe_in_flight, true);

    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_CLOUD_MANAGER,
        .sub = MQTT_CHANNEL_SUB_CLOUD_MANAGER_START_LISTENING,
    };
    osal_err_t mqtt_err = esp_rmaker_mqtt_impl.subscribe(&channel, from_cloud_topic, strlen(from_cloud_topic), __cloud_manager_subscribe_cb, QoS1, NULL);
    if (mqtt_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to subscribe to cloud error: %d", mqtt_err);
        atomic_store(&__listening_gate.subscribe_in_flight, false);
        return ESP_RMAKER_FAIL;
    }

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    /* Issue the bridge subscribes. Acks land on the same gate. A subscribe
     * that fails to issue will never ACK, so lower the gate's expected count
     * by the shortfall - otherwise the gate could never be satisfied and the
     * node would never come online (the bridge filters are irrelevant to a
     * node with no children). */
    uint8_t bridge_issued = 0;
    if (bridge_internal_subscribe(&bridge_issued) != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Bridge subsystem subscribe issuance failed (issued %u/%u)",
                  (unsigned int)bridge_issued, BRIDGE_INTERNAL_FILTER_SUB_COUNT);
    }
    /* expected was seeded with BRIDGE_INTERNAL_FILTER_SUB_COUNT bridge acks;
     * signed so an unexpected issued > count goes negative and is skipped. */
    int shortfall = (int)BRIDGE_INTERNAL_FILTER_SUB_COUNT - (int)bridge_issued;
    if (shortfall > 0) {
        unsigned int prev_expected = atomic_fetch_sub(&__listening_gate.subscribe_expected, (unsigned int)shortfall);
        unsigned int new_expected = prev_expected - (unsigned int)shortfall;
        /* Acks may already have met the lowered target while we were issuing. */
        if (atomic_load(&__listening_gate.subscribe_completed) >= new_expected) {
            esp_rmaker_core_subscribed_to_cloud();
        }
    }
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

    esp_rmaker_error_t wait_err = esp_rmaker_network_wait_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_CLOUD, timeout_ms);
    /* Stop accepting late acks regardless of outcome. */
    atomic_store(&__listening_gate.subscribe_in_flight, false);
    return wait_err;
}

esp_rmaker_error_t esp_rmaker_cloud_manager_stop_listening(uint32_t timeout_ms)
{
    char from_cloud_topic[MQTT_TOPIC_BUFFER_SIZE];
    int topic_len = esp_rmaker_mqtt_topic_from_cloud(from_cloud_topic, sizeof(from_cloud_topic));
    if (topic_len < 0 || (size_t)topic_len >= sizeof(from_cloud_topic)) {
        OSAL_LOGE(TAG, "Failed to build from_cloud MQTT topic");
        return ESP_RMAKER_FAIL;
    }

    unsigned int expected = 1;
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    expected += BRIDGE_INTERNAL_FILTER_SUB_COUNT;
#endif
    atomic_store(&__listening_gate.unsubscribe_expected, expected);
    atomic_store(&__listening_gate.unsubscribe_completed, 0);
    atomic_store(&__listening_gate.unsubscribe_in_flight, true);

    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_CLOUD_MANAGER,
        .sub = MQTT_CHANNEL_SUB_CLOUD_MANAGER_STOP_LISTENING,
    };
    osal_err_t mqtt_err = esp_rmaker_mqtt_impl.unsubscribe(&channel, from_cloud_topic, strlen(from_cloud_topic), QoS1);
    if (mqtt_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to unsubscribe from cloud error: %d", mqtt_err);
        atomic_store(&__listening_gate.unsubscribe_in_flight, false);
        return ESP_RMAKER_FAIL;
    }

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    uint8_t bridge_issued = 0;
    (void)bridge_internal_unsubscribe(&bridge_issued);
    int shortfall = (int)BRIDGE_INTERNAL_FILTER_SUB_COUNT - (int)bridge_issued;
    if (shortfall > 0) {
        unsigned int prev_expected = atomic_fetch_sub(&__listening_gate.unsubscribe_expected, (unsigned int)shortfall);
        unsigned int new_expected = prev_expected - (unsigned int)shortfall;
        if (atomic_load(&__listening_gate.unsubscribe_completed) >= new_expected) {
            esp_rmaker_core_unsubscribed_from_cloud();
        }
    }
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

    esp_rmaker_error_t wait_err = esp_rmaker_network_wait_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_CLOUD, timeout_ms);
    atomic_store(&__listening_gate.unsubscribe_in_flight, false);
    return wait_err;
}

bool esp_rmaker_cloud_manager_is_listening(void)
{
    return esp_rmaker_network_get_bits() & RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_CLOUD;
}

/* Topic-builder strategy for ::__send_with_topic. Caller picks node-vs-bridge. */
typedef enum {
    __SEND_TOPIC_KIND_NODE_TO_CLOUD,    /**< Regular node-to-cloud topic */
#if CONFIG_RMNG_BRIDGE_ENABLED
    __SEND_TOPIC_KIND_BRIDGE_TO_CLOUD,  /**< Bridge-to-cloud topic */
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
} __send_topic_kind_t;

/* Shared core for the public ``send`` entrypoints. Builds the JSON
 * payload, registers set-response callbacks in the inbox keyed by
 * ``ctx``, then publishes to a topic chosen by ``kind``. */
static esp_rmaker_error_t __send_with_topic(const esp_rmaker_topic_ctx_t *ctx,
        __send_topic_kind_t kind,
        esp_rmaker_cloud_event_t *p_event,
        size_t event_count,
        uint32_t sub_channel)
{
    if (!p_event || !event_count) {
        OSAL_LOGE(TAG, "Invalid event or event count");
        return ESP_RMAKER_INVALID_ARG;
    }
    if (!ctx) {
        ctx = &esp_rmaker_topic_ctx_self;
    }

    /* Make JSON payload */
    int payload_size = __cloud_manager_make_json_payload(p_event, event_count, NULL, 0);
    if (payload_size < 0) {
        OSAL_LOGE(TAG, "Failed to get required JSON payload size");
        return ESP_RMAKER_INVALID_ARG;
    }
    char *payload = (char *)OSAL_CALLOC_EXTRAM(payload_size, sizeof(char));
    if (!payload) {
        OSAL_LOGE(TAG, "Failed to allocate memory for payload");
        return ESP_RMAKER_NO_MEM;
    }
    payload_size = __cloud_manager_make_json_payload(p_event, event_count, payload, payload_size);
    if (payload_size < 0) {
        OSAL_LOGE(TAG, "Failed to generate JSON payload");
        free(payload);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Register set-response callbacks in inbox. The inbox is now keyed
     * by (event_name, ctx); on first-time registration of a new tuple
     * we allocate a fresh slot (capped at inbox.num_entries). */
    for (size_t i = 0; i < event_count; i++) {
        if (!p_event[i].p_set_response_cb_context) {
            continue;
        }
        __lock_inbox();
        /* Find an existing slot matching (event_name, ctx), else a free slot. */
        size_t free_slot = inbox.num_entries;
        bool matched = false;
        for (size_t j = 0; j < inbox.num_entries; j++) {
            if (inbox.entries[j].event_name == NULL ||
                    inbox.entries[j].p_set_response_cb_context == NULL) {
                if (free_slot == inbox.num_entries) {
                    free_slot = j;
                }
                continue;
            }
            if (inbox.entries[j].ctx == ctx &&
                    strcmp(p_event[i].name, inbox.entries[j].event_name) == 0) {
                /* Overwrite existing in-flight callback for the same ctx. */
                inbox.entries[j].p_set_response_cb_context = p_event[i].p_set_response_cb_context;
                matched = true;
                break;
            }
        }
        if (!matched) {
            if (free_slot >= inbox.num_entries) {
                OSAL_LOGE(TAG, "Inbox full - dropping callback for event '%s' (ctx=%p)",
                          p_event[i].name, (const void *)ctx);
            } else {
                inbox.entries[free_slot].event_name = p_event[i].name;
                inbox.entries[free_slot].ctx = ctx;
                inbox.entries[free_slot].p_set_response_cb_context = p_event[i].p_set_response_cb_context;
            }
        }
        __unlock_inbox();
    }

    /* Send payload */
    char publish_topic[MQTT_TOPIC_BUFFER_SIZE];
    int topic_len = -1;
    switch (kind) {
    case __SEND_TOPIC_KIND_NODE_TO_CLOUD:
        topic_len = esp_rmaker_mqtt_topic_to_cloud(ctx, publish_topic, sizeof(publish_topic));
        break;
#if CONFIG_RMNG_BRIDGE_ENABLED
    case __SEND_TOPIC_KIND_BRIDGE_TO_CLOUD:
        topic_len = esp_rmaker_mqtt_topic_bridges_to_cloud(publish_topic, sizeof(publish_topic));
        break;
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
    default:
        OSAL_LOGE(TAG, "Invalid send topic kind: %d", (int)kind);
        free(payload);
        return ESP_RMAKER_INVALID_ARG;
    }
    if (topic_len < 0 || (size_t)topic_len >= sizeof(publish_topic)) {
        OSAL_LOGE(TAG, "Failed to build to_cloud MQTT topic (kind=%d)", (int)kind);
        free(payload);
        return ESP_RMAKER_FAIL;
    }
    OSAL_LOGI(TAG, "Sending to cloud using topic: %s", publish_topic);
    OSAL_LOGD(TAG, "Sending payload to cloud: %s", payload);
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_CLOUD_MANAGER,
        .sub = sub_channel,
    };
    osal_err_t mqtt_err = esp_rmaker_mqtt_impl.publish(&channel, publish_topic, strlen(publish_topic), payload, payload_size - 1, QoS1, false);
    if (mqtt_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to send payload to cloud error: %d", mqtt_err);
        free(payload);
        return ESP_RMAKER_FAIL;
    }

    free(payload);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_cloud_manager_send(const esp_rmaker_topic_ctx_t *ctx, esp_rmaker_cloud_event_t *p_event, size_t event_count, uint32_t sub_channel)
{
    return __send_with_topic(ctx, __SEND_TOPIC_KIND_NODE_TO_CLOUD, p_event, event_count, sub_channel);
}

#if CONFIG_RMNG_BRIDGE_ENABLED
esp_rmaker_error_t esp_rmaker_cloud_manager_send_bridge(esp_rmaker_cloud_event_t *p_event, size_t event_count, uint32_t sub_channel)
{
    /* Inbox keyed by self ctx - bridge-namespace publish is conceptually
     * the parent acting on its own behalf. */
    return __send_with_topic(&esp_rmaker_topic_ctx_self, __SEND_TOPIC_KIND_BRIDGE_TO_CLOUD, p_event, event_count, sub_channel);
}
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
