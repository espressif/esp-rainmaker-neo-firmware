/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file notify.c
 * @brief Direct notification functions.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "network/notify.h"

/* Standard C headers */
#include <stddef.h>
#include <string.h>

/* MQTT includes */
#include "osal_mqtt_prototypes.h"
#include "network/common.h"
#include "network/mqtt_topics.h"
#include "network/mqtt_channels.h"

/* Node (per-node topic ctx) */
#include "esp_rmaker_node.h"
#include "node_internal.h"

/* Update ID -> owning node resolution */
#include "data_model_internal.h"

/* Event loop includes */
#include "event_loop.h"

/* Platform common includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_semaphore.h"

/* Event flags includes */
#include "event_flags.h"

/* Preprocessor definitions *********************************************************/

/**
 * @brief Minimum size of an empty notify payload.
 *
 * i.e., {"notify":{}} (14 characters, including the null terminator)
 */
#define RMAKER_NOTIFY_PAYLOAD_MIN_SIZE 14

/* Variables **********************************************************************/

/* Tag for logging */
static const char *TAG = "rmng_net_notify";

/* Binary semaphore given when a notify publish completes (QoS1 PUBACK), used by
 * esp_rmaker_notify_send_sync to block until the publish has flushed. */
static osal_semaphore_handle_t __notify_flush_sem = NULL;

/* Private function declarations *******************************************************/

/**
 * @brief MQTT event handler for the notify.
 * @param[in] event_handler_arg The argument to pass to the event handler.
 * @param[in] event_base The event base to send the event to.
 * @param[in] event_id The event id to send the event to.
 * @param[in] event_data The data to send with the event.
 */
static void __notify_mqtt_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief Make JSON payload for the notification.
 * @param[in] p_notification Pointer to the notification.
 * @param[in] payload Pointer to the payload. If NULL, then only the required size is reported to payload_size.
 * @param[in] payload_size Pointer to the size of the payload.
 * @param[in] is_sizing Whether this function call is to size the payload.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __notify_make_json_payload(esp_rmaker_notification_t *p_notification, char *payload, size_t *payload_size, bool is_sizing);

/**
 * @brief Report the push-notification payload: {"push": true}.
 */
static esp_rmaker_error_t __notify_push_payload_fn(json_gen_str_t *jptr, void *data, bool is_sizing);

/* Private function definitions *******************************************************/

static void __notify_mqtt_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    osal_mqtt_event_loop_data_on_complete_t *mqtt_data = (osal_mqtt_event_loop_data_on_complete_t *)event_data;
    osal_mqtt_event_loop_channel_t channel = mqtt_data->channel;
    if (channel.main != MQTT_CHANNEL_MAIN_NOTIFY) {
        return;
    }

    osal_err_t status = mqtt_data->status;
    const char *status_str = status == OSAL_ERR_OK ? "SUCCESS" : "FAILED";

    switch (channel.sub) {
    case MQTT_CHANNEL_SUB_NOTIFY_SEND:
        OSAL_LOGD(TAG, "Notify published: %s", status_str);
        esp_rmaker_event_flags_set_notification_sent();
        if (__notify_flush_sem != NULL) {
            (void) osal_semaphore_give(__notify_flush_sem);
        }
        break;
    default:
        break;
    }
}

static esp_rmaker_error_t __notify_make_json_payload(esp_rmaker_notification_t *p_notification, char *payload, size_t *payload_size, bool is_sizing)
{
    if (p_notification == NULL || p_notification->report_payload_fn == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err;
    json_gen_str_t jpayload;
    json_gen_str_start(&jpayload, payload, *payload_size, NULL, NULL);
    if (json_gen_start_object(&jpayload) != 0) {
        err = ESP_RMAKER_FAIL;
        goto __notify_make_json_payload_end;
    }

    /* Add 'notify' object */
    if (json_gen_push_object(&jpayload, "notify") != 0) {
        err = ESP_RMAKER_FAIL;
        goto __notify_make_json_payload_end;
    }

    /* Add payload */
    err = p_notification->report_payload_fn(&jpayload, p_notification->data, is_sizing);
    if (err != ESP_RMAKER_OK) {
        goto __notify_make_json_payload_end;
    }

    /* Pop 'notify' object */
    if (json_gen_pop_object(&jpayload) != 0) {
        err = ESP_RMAKER_FAIL;
        goto __notify_make_json_payload_end;
    }

    /* End JSON object */
    if (json_gen_end_object(&jpayload) != 0) {
        err = ESP_RMAKER_FAIL;
        goto __notify_make_json_payload_end;
    }

__notify_make_json_payload_end:
    *payload_size = json_gen_str_end(&jpayload);
    return err;
}

static esp_rmaker_error_t __notify_push_payload_fn(json_gen_str_t *jptr, void *data, bool is_sizing)
{
    (void) data;
    (void) is_sizing;
    if (jptr == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return json_gen_obj_set_bool(jptr, "push", true) == 0 ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

/* Public function definitions *******************************************************/

esp_rmaker_error_t esp_rmaker_notify_init(void)
{
    if (__notify_flush_sem == NULL) {
        __notify_flush_sem = osal_semaphore_create_binary();
        if (__notify_flush_sem == NULL) {
            OSAL_LOGE(TAG, "Failed to create notify flush semaphore");
            return ESP_RMAKER_NO_MEM;
        }
    }
    esp_rmaker_error_t err;
    err = event_loop_register_mqtt_on_complete_handler(__notify_mqtt_event_handler);
    return err;
}

esp_rmaker_error_t esp_rmaker_notify_deinit(void)
{
    esp_rmaker_error_t err;
    err = event_loop_unregister_mqtt_on_complete_handler(__notify_mqtt_event_handler);
    if (__notify_flush_sem != NULL) {
        osal_semaphore_delete(__notify_flush_sem);
        __notify_flush_sem = NULL;
    }
    return err;
}

/* Core send: builds the payload and publishes to the notify topic of the
 * Thing identified by ``ctx``. */
static esp_rmaker_error_t __notify_send_with_ctx(const esp_rmaker_topic_ctx_t *ctx, esp_rmaker_notification_t *p_notification)
{
    if (p_notification == NULL || p_notification->report_payload_fn == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err;

    /* Get required JSON payload size */
    char *payload = NULL;
    size_t payload_size = 0;
    err = __notify_make_json_payload(p_notification, NULL, &payload_size, true);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    if (payload_size <= RMAKER_NOTIFY_PAYLOAD_MIN_SIZE) {
        OSAL_LOGE(TAG, "Notify payload size %d does not meet minimum size %d", (int)payload_size, (int)RMAKER_NOTIFY_PAYLOAD_MIN_SIZE);
        return ESP_RMAKER_INVALID_ARG;
    }
    OSAL_LOGD(TAG, "Notify payload size: %d", (int)payload_size);

    /* Allocate memory for payload */
    payload = (char *)OSAL_CALLOC_EXTRAM(payload_size, sizeof(char));
    if (payload == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for notify payload");
        return ESP_RMAKER_NO_MEM;
    }

    /* Make JSON payload */
    err = __notify_make_json_payload(p_notification, payload, &payload_size, false);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to make JSON payload: %d", err);
        goto esp_rmaker_notify_send_end;
    }

    /* Send payload */
    char publish_topic[MQTT_TOPIC_BUFFER_SIZE];
    int topic_len = esp_rmaker_mqtt_topic_notify_for_node(ctx, publish_topic, sizeof(publish_topic));
    if (topic_len < 0 || (size_t)topic_len >= sizeof(publish_topic)) {
        OSAL_LOGE(TAG, "Failed to build notify MQTT topic");
        err = ESP_RMAKER_FAIL;
        goto esp_rmaker_notify_send_end;
    }
    OSAL_LOGI(TAG, "Sending notify using topic: %s", publish_topic);
    OSAL_LOGD(TAG, "Sending payload to notify: %s", payload);
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_NOTIFY,
        .sub = MQTT_CHANNEL_SUB_NOTIFY_SEND,
    };
    osal_err_t mqtt_err = esp_rmaker_mqtt_impl.publish(&channel, publish_topic, strlen(publish_topic), payload, payload_size - 1, QoS1, false);
    if (mqtt_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to send payload to cloud: %d", mqtt_err);
        err = ESP_RMAKER_FAIL;
        goto esp_rmaker_notify_send_end;
    }

esp_rmaker_notify_send_end:
    free(payload);
    return err;
}

esp_rmaker_error_t esp_rmaker_notify_send(esp_rmaker_notification_t *p_notification)
{
    return __notify_send_with_ctx(&esp_rmaker_topic_ctx_self, p_notification);
}

esp_rmaker_error_t esp_rmaker_notify_send_for_node(const esp_rmaker_node_t *node, esp_rmaker_notification_t *p_notification)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __notify_send_with_ctx(esp_rmaker_node_topic_ctx(node), p_notification);
}

esp_rmaker_error_t esp_rmaker_notify_send_push(esp_rmaker_state_update_id_t update_id)
{
    esp_rmaker_notification_t notification = {
        .report_payload_fn = __notify_push_payload_fn,
        .data = NULL,
    };
    /* Route the push to the node owning the param that changed (NULL -> self), so that an
     * alert on a bridge child does not go out on the parent node's notify topic. */
    const esp_rmaker_node_t *node = data_model_state_update_id_to_node(update_id);
    if (!node) {
        node = esp_rmaker_get_node();
    }
    return esp_rmaker_notify_send_for_node(node, &notification);
}

esp_rmaker_error_t esp_rmaker_notify_send_sync(esp_rmaker_notification_t *p_notification, uint32_t timeout_ms)
{
    if (__notify_flush_sem == NULL) {
        OSAL_LOGE(TAG, "Notify not initialized; cannot send synchronously");
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Clear any stale completion from a prior notification so the wait below tracks this
     * publish only (non-blocking; binary sem holds at most one). */
    (void) osal_semaphore_take(__notify_flush_sem, 0);

    esp_rmaker_error_t err = __notify_send_with_ctx(&esp_rmaker_topic_ctx_self, p_notification);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Block until the QoS1 PUBACK is delivered (or timeout). */
    osal_err_t sem_err = osal_semaphore_take(__notify_flush_sem, osal_ticks_from_ms(timeout_ms));
    if (sem_err != OSAL_ERR_OK) {
        OSAL_LOGW(TAG, "Notify publish not confirmed within %u ms", (unsigned) timeout_ms);
        return ESP_RMAKER_TIMEOUT;
    }
    return ESP_RMAKER_OK;
}
