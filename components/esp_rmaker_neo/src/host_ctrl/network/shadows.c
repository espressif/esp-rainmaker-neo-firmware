/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file shadows.c
 * @brief Indexed and named shadow operations
 */

/* Includes *********************************************************************/

/* Declarations */
#include "network/shadows.h"

/* Standard C includes */
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

/* Network includes */
#include "network/common.h"
#include "network/mqtt_topics.h"
#include "network/mqtt_channels.h"
#include "constants/network.h"

/* Local configuration includes */
#include "local_config.h"

/* Event loop includes */
#include "event_loop.h"

/* Platform common includes */
#include "osal_event_group.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* JSON includes */
#include "json_parser.h"

/* Constants ********************************************************************/

/**
 * @brief The type of shadow.
 */
typedef enum {
    __SHADOWS_TYPE_INDEXED = 0,
    __SHADOWS_TYPE_NAMED = 1,
} __shadows_type_t;

/* Private variables *************************************************************/

/**
 * @brief The tag for logging.
 */
static const char *TAG = "rmng_hc_shadows";

/**
 * @brief The empty payload for the indexed and named shadows.
 */
static char *empty_payload = "{}";

/**
 * @brief The reported document cache for the indexed and named shadows.
 */
static struct {
    char *indexed; /* Reported document cache for the indexed shadow. */
    char *named; /* Reported document cache for the named shadow. */
} __shadows_reported_documents_t = {
    .indexed = NULL,
    .named = NULL,
};

/* Private function declarations *************************************************/

/**
 * @brief Get the MQTT topic for the get operation of the indexed shadow.
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
static int __mqtt_topic_params_indexed_shadow_get(char *buffer, size_t buffer_size);

/**
 * @brief Get the MQTT topic for the get operation of the named shadow.
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
static int __mqtt_topic_params_named_shadow_get(char *buffer, size_t buffer_size);

/**
 * @brief The on-complete callback for MQTT commands sent by this module.
 * @param[in] event_handler_arg The argument to pass to the event handler.
 * @param[in] event_base The event base to register the event handler to.
 * @param[in] event_id The event id to register the event handler to.
 * @param[in] event_data The data to send with the event.
 */
static void __mqtt_on_complete_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief The callback triggered for publishes received on get/accepted topics of the indexed shadow.
 * @param[in] topic The topic of the message.
 * @param[in] topic_len The length of the topic.
 * @param[in] payload The payload of the message.
 * @param[in] payload_len The length of the payload.
 * @param[in] priv_data The private data passed during subscription.
 */
static void __mqtt_on_accepted_cb(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data);

/**
 * @brief Manage the subscription to the get/accepted topic of the shadow.
 * @param[in] should_subscribe Whether to subscribe or unsubscribe.
 * @param[in] type The type of shadow.
 * @param[in] timeout_ms The timeout in milliseconds.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __mqtt_manage_get_accepted_subscription(bool should_subscribe, __shadows_type_t type, uint32_t timeout_ms);

/**
 * @brief Publish a request to get the reported document of the shadow.
 * @param[in] type The type of shadow.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __mqtt_publish_get_request(__shadows_type_t type);

/**
 * @brief Get the reported document of the shadow.
 * @param[in] type The type of shadow.
 * @param[in] timeout_ms The timeout in milliseconds.
 * @return pointer to the reported document on success. NULL on failure.
 * @note The reported document is a JSON string. Must be freed by the caller.
 */
static char *__shadows_get_reported_document(__shadows_type_t type, uint32_t timeout_ms);

/* Private function definitions **************************************************/

int __mqtt_topic_params_indexed_shadow_get(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

    char *thing_name = NULL;
    esp_rmaker_error_t err = esp_rmaker_credentials_get_thing_name(&thing_name);
    if (err != ESP_RMAKER_OK) {
        return -1;
    }

    int ret = snprintf(buffer, buffer_size, "$aws/things/%s/shadow/name/iparams/get", thing_name);
    free(thing_name);
    return ret;
}

int __mqtt_topic_params_named_shadow_get(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

    char *thing_name = NULL;
    esp_rmaker_error_t err = esp_rmaker_credentials_get_thing_name(&thing_name);
    if (err != ESP_RMAKER_OK) {
        return -1;
    }

    char *group_info_str = esp_rmaker_local_config_get_group_info_str();
    if (group_info_str == NULL) {
        free(thing_name);
        return -1;
    }

    int ret = snprintf(buffer, buffer_size, "$aws/things/%s/shadow/name/params-%s/get", thing_name, group_info_str);
    free(thing_name);
    free(group_info_str);
    return ret;
}

static void __mqtt_on_complete_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    osal_mqtt_event_loop_data_on_complete_t *mqtt_data = (osal_mqtt_event_loop_data_on_complete_t *)event_data;

    /* Check if the channel is for indexed shadow */
    osal_mqtt_event_loop_channel_t channel = mqtt_data->channel;
    if (channel.main != MQTT_CHANNEL_MAIN_SHADOWS) {
        return;
    }

    osal_err_t status = mqtt_data->status;
    const char *status_str = status == OSAL_ERR_OK ? "SUCCESS" : "FAILED";

    switch (channel.sub) {
    case MQTT_CHANNEL_SUB_INDEXED_SHADOW_SUBSCRIBE:
        OSAL_LOGD(TAG, "MQTT subscribe complete: %s", status_str);
        if (status == OSAL_ERR_OK) {
            esp_rmaker_network_clear_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_UNSUBSCRIBED);
            esp_rmaker_network_set_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_SUBSCRIBED);
        }
        OSAL_LOGI(TAG, "Indexed shadow subscribed to get/accepted topic: %s", status_str);
        break;
    case MQTT_CHANNEL_SUB_INDEXED_SHADOW_UNSUBSCRIBE:
        OSAL_LOGD(TAG, "MQTT unsubscribe complete: %s", status_str);
        if (status == OSAL_ERR_OK) {
            esp_rmaker_network_clear_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_SUBSCRIBED);
            esp_rmaker_network_set_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_UNSUBSCRIBED);
        }
        OSAL_LOGI(TAG, "Indexed shadow unsubscribed from get/accepted topic: %s", status_str);
        break;
    case MQTT_CHANNEL_SUB_INDEXED_SHADOW_GET:
        OSAL_LOGD(TAG, "MQTT indexed shadow get complete: %s", status_str);
        break;
    case MQTT_CHANNEL_SUB_NAMED_SHADOW_SUBSCRIBE:
        OSAL_LOGD(TAG, "MQTT subscribe complete: %s", status_str);
        if (status == OSAL_ERR_OK) {
            esp_rmaker_network_clear_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_UNSUBSCRIBED);
            esp_rmaker_network_set_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_SUBSCRIBED);
        }
        OSAL_LOGI(TAG, "Named shadow subscribed to get/accepted topic: %s", status_str);
        break;
    case MQTT_CHANNEL_SUB_NAMED_SHADOW_UNSUBSCRIBE:
        OSAL_LOGD(TAG, "MQTT unsubscribe complete: %s", status_str);
        if (status == OSAL_ERR_OK) {
            esp_rmaker_network_clear_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_SUBSCRIBED);
            esp_rmaker_network_set_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_UNSUBSCRIBED);
        }
        OSAL_LOGI(TAG, "Named shadow unsubscribed from get/accepted topic: %s", status_str);
        break;
    case MQTT_CHANNEL_SUB_NAMED_SHADOW_GET:
        OSAL_LOGD(TAG, "MQTT named shadow get complete: %s", status_str);
        break;
    default:
        break;
    }
}


static void __mqtt_on_accepted_cb(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data)
{
    OSAL_LOGD(TAG, "MQTT accepted: %s", topic);
    OSAL_LOGD(TAG, "Payload: %.*s", (int) payload_len, (const char *) payload);
    /* Get the reported document */
    jparse_ctx_t jctx;
    json_parse_start(&jctx, (const char *) payload, payload_len);
    if (json_obj_get_object(&jctx, "state") != 0) {
        OSAL_LOGE(TAG, "Failed to get state object from reported document");
        return;
    }

    /* Read the 'reported' object directly into a string */
    int reported_len = 0;
    if (json_obj_get_object_strlen(&jctx, "reported", &reported_len) != 0) {
        OSAL_LOGE(TAG, "Failed to get reported object string length from reported document");
        return;
    }

    /* Add 1 for the null terminator */
    reported_len++;

    /* Allocate a buffer for the reported document */
    char *reported_str = (char *) OSAL_CALLOC_EXTRAM(reported_len, sizeof(char));
    if (reported_str == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate buffer for reported document");
        return;
    }

    /* Read the 'reported' object directly into the buffer */
    if (json_obj_get_object_str(&jctx, "reported", reported_str, reported_len) != 0) {
        OSAL_LOGE(TAG, "Failed to get reported object string from reported document");
        free(reported_str);
        return;
    }

    json_parse_end(&jctx);

    OSAL_LOGD(TAG, "Received reported document: %s", reported_str);


    /* Determine the type of shadow and set the reported document and event group bit */
    __shadows_type_t type = (__shadows_type_t)(uintptr_t) priv_data;
    char **p_reported_document = NULL;
    osal_event_group_bits_t event_group_bit = 0;
    switch (type) {
    case __SHADOWS_TYPE_INDEXED:
        p_reported_document = &__shadows_reported_documents_t.indexed;
        event_group_bit = RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_REPORTED;
        break;
    case __SHADOWS_TYPE_NAMED:
        p_reported_document = &__shadows_reported_documents_t.named;
        event_group_bit = RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_REPORTED;
        break;
    default:
        OSAL_LOGE(TAG, "Invalid shadow type: %d", type);
        free(reported_str);
        return;
    }

    /* Set the reported document */
    if (*p_reported_document != NULL) {
        free(*p_reported_document);
    }
    *p_reported_document = reported_str;

    /* Set the bit */
    esp_rmaker_network_set_bits(event_group_bit);
}


static esp_rmaker_error_t __mqtt_manage_get_accepted_subscription(bool should_subscribe, __shadows_type_t type, uint32_t timeout_ms)
{
    /* Determine the variables based on shadow type */
    esp_rmaker_mqtt_topic_fn_t topic_fn = NULL;
    mqtt_channel_sub_shadows_t sub_channel_sub, sub_channel_unsub;
    osal_event_group_bits_t event_group_bit_wait;
    switch (type) {
    case __SHADOWS_TYPE_INDEXED:
        topic_fn = __mqtt_topic_params_indexed_shadow_get;
        sub_channel_sub = MQTT_CHANNEL_SUB_INDEXED_SHADOW_SUBSCRIBE;
        sub_channel_unsub = MQTT_CHANNEL_SUB_INDEXED_SHADOW_UNSUBSCRIBE;
        event_group_bit_wait = should_subscribe
                               ? RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_SUBSCRIBED
                               : RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_UNSUBSCRIBED;
        break;
    case __SHADOWS_TYPE_NAMED:
        topic_fn = __mqtt_topic_params_named_shadow_get;
        sub_channel_sub = MQTT_CHANNEL_SUB_NAMED_SHADOW_SUBSCRIBE;
        sub_channel_unsub = MQTT_CHANNEL_SUB_NAMED_SHADOW_UNSUBSCRIBE;
        event_group_bit_wait = should_subscribe
                               ? RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_SUBSCRIBED
                               : RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_UNSUBSCRIBED;
        break;
    default:
        OSAL_LOGE(TAG, "Invalid shadow type: %d", type);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Get the topic for the get/accepted topic */
    char mqtt_topic[MQTT_TOPIC_BUFFER_SIZE];
    int topic_len = esp_rmaker_mqtt_topic_append_accepted(topic_fn, mqtt_topic, sizeof(mqtt_topic));
    if (topic_len < 0 || (size_t)topic_len >= sizeof(mqtt_topic)) {
        OSAL_LOGE(TAG, "Failed to get MQTT topic for get/accepted topic");
        return ESP_RMAKER_FAIL;
    }

    osal_err_t status;
    if (should_subscribe) {
        /* Subscribe to the topic */
        OSAL_LOGI(TAG, "Subscribing to get/accepted topic: %s", mqtt_topic);
        osal_mqtt_event_loop_channel_t channel = {
            .main = MQTT_CHANNEL_MAIN_SHADOWS,
            .sub = sub_channel_sub,
        };
        status = esp_rmaker_mqtt_impl.subscribe(&channel, mqtt_topic, topic_len, __mqtt_on_accepted_cb, QoS1, (void *)(uintptr_t) type);
        if (status != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to subscribe to get/accepted topic error: %d", status);
            return ESP_RMAKER_FAIL;
        }
    } else {
        /* Unsubscribe from the topic */
        OSAL_LOGI(TAG, "Unsubscribing from get/accepted topic: %s", mqtt_topic);
        osal_mqtt_event_loop_channel_t channel = {
            .main = MQTT_CHANNEL_MAIN_SHADOWS,
            .sub = sub_channel_unsub,
        };
        status = esp_rmaker_mqtt_impl.unsubscribe(&channel, mqtt_topic, topic_len, QoS1);
        if (status != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to unsubscribe from get/accepted topic error: %d", status);
            return ESP_RMAKER_FAIL;
        }
    }

    /* Wait for the subscription/unsubscription to be complete */
    esp_rmaker_error_t err = esp_rmaker_network_wait_bits(event_group_bit_wait, timeout_ms);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Timed out waiting for subscription/unsubscription to be complete with esp_rmaker_error_t: %d", err);
        return err;
    }

    return ESP_RMAKER_OK;
}


static esp_rmaker_error_t __mqtt_publish_get_request(__shadows_type_t type)
{
    OSAL_LOGD(TAG, "Publishing get request to shadow of type: %d", type);

    /* Determine variables based on shadow type */
    esp_rmaker_mqtt_topic_fn_t topic_fn = NULL;
    mqtt_channel_sub_shadows_t sub_channel_get;
    switch (type) {
    case __SHADOWS_TYPE_INDEXED:
        topic_fn = __mqtt_topic_params_indexed_shadow_get;
        sub_channel_get = MQTT_CHANNEL_SUB_INDEXED_SHADOW_GET;
        break;
    case __SHADOWS_TYPE_NAMED:
        topic_fn = __mqtt_topic_params_named_shadow_get;
        sub_channel_get = MQTT_CHANNEL_SUB_NAMED_SHADOW_GET;
        break;
    default:
        OSAL_LOGE(TAG, "Invalid shadow type: %d", type);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Get the topic for the get topic */
    char mqtt_topic[MQTT_TOPIC_BUFFER_SIZE];
    int topic_len = topic_fn(mqtt_topic, sizeof(mqtt_topic));
    if (topic_len < 0 || (size_t)topic_len >= sizeof(mqtt_topic)) {
        OSAL_LOGE(TAG, "Failed to get MQTT topic for shadow get topic");
        return ESP_RMAKER_FAIL;
    }

    /* Publish a request to get the reported document */
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_SHADOWS,
        .sub = sub_channel_get,
    };
    osal_err_t status = esp_rmaker_mqtt_impl.publish(&channel, mqtt_topic, topic_len, empty_payload, strlen(empty_payload), QoS1, false);
    if (status != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to publish get request to shadow of type: %d error: %d", type, status);
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

static char *__shadows_get_reported_document(__shadows_type_t type, uint32_t timeout_ms)
{
    /* Get variables based on shadow type */
    osal_event_group_bits_t subscribed_bit, reported_bit;
    char **p_reported_document = NULL;
    switch (type) {
    case __SHADOWS_TYPE_INDEXED:
        subscribed_bit = RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_SUBSCRIBED;
        reported_bit = RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_REPORTED;
        p_reported_document = &__shadows_reported_documents_t.indexed;
        break;
    case __SHADOWS_TYPE_NAMED:
        subscribed_bit = RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_SUBSCRIBED;
        reported_bit = RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_REPORTED;
        p_reported_document = &__shadows_reported_documents_t.named;
        break;
    default:
        OSAL_LOGE(TAG, "Invalid shadow type: %d", type);
        return NULL;
    }

    if (!(esp_rmaker_network_get_bits() & subscribed_bit)) {
        OSAL_LOGE(TAG, "Not subscribed to get/accepted topic of shadow of type: %d", type);
        return NULL;
    }

    esp_rmaker_network_clear_bits(reported_bit);

    /* Publish a request to get the reported document */
    esp_rmaker_error_t err;
    err = __mqtt_publish_get_request(type);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to publish get request to shadow of type: %d with esp_rmaker_error_t: %d", type, err);
        return NULL;
    }

    /* Wait for the reported document to be available */
    err = esp_rmaker_network_wait_bits(reported_bit, timeout_ms);
    if (err != ESP_RMAKER_OK) {
        /* Timed out */
        OSAL_LOGE(TAG, "Timed out waiting for shadow of type: %d reported document", type);
        return NULL;
    }
    esp_rmaker_network_clear_bits(reported_bit);

    /* Return the reported document and set it to NULL */
    char *to_report = *p_reported_document;
    *p_reported_document = NULL;
    return to_report;
}

/* Public function definitions **************************************************/

esp_rmaker_error_t esp_rmaker_shadows_init(void)
{
    /* Register the event handler */
    esp_rmaker_error_t err;
    err = event_loop_register_mqtt_on_complete_handler(__mqtt_on_complete_event_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to register MQTT on complete event handler with esp_rmaker_error_t: %d", err);
        return err;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_shadows_deinit(void)
{
    /* Unregister the event handler */
    esp_rmaker_error_t err;
    err = event_loop_unregister_mqtt_on_complete_handler(__mqtt_on_complete_event_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to unregister MQTT on complete event handler with esp_rmaker_error_t: %d", err);
        return err;
    }

    /* Free the reported documents */
    if (__shadows_reported_documents_t.indexed) {
        free(__shadows_reported_documents_t.indexed);
        __shadows_reported_documents_t.indexed = NULL;
    }
    if (__shadows_reported_documents_t.named) {
        free(__shadows_reported_documents_t.named);
        __shadows_reported_documents_t.named = NULL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_indexed_shadow_subscribe_get_accepted(uint32_t timeout_ms)
{

    return __mqtt_manage_get_accepted_subscription(true, __SHADOWS_TYPE_INDEXED, timeout_ms);
}

esp_rmaker_error_t esp_rmaker_indexed_shadow_unsubscribe_get_accepted(uint32_t timeout_ms)
{
    return __mqtt_manage_get_accepted_subscription(false, __SHADOWS_TYPE_INDEXED, timeout_ms);
}

esp_rmaker_error_t esp_rmaker_named_shadow_subscribe_get_accepted(uint32_t timeout_ms)
{
    return __mqtt_manage_get_accepted_subscription(true, __SHADOWS_TYPE_NAMED, timeout_ms);
}

esp_rmaker_error_t esp_rmaker_named_shadow_unsubscribe_get_accepted(uint32_t timeout_ms)
{
    return __mqtt_manage_get_accepted_subscription(false, __SHADOWS_TYPE_NAMED, timeout_ms);
}

char *esp_rmaker_indexed_shadow_get_reported(uint32_t timeout_ms)
{
    return __shadows_get_reported_document(__SHADOWS_TYPE_INDEXED, timeout_ms);
}

char *esp_rmaker_named_shadow_get_reported(uint32_t timeout_ms)
{
    return __shadows_get_reported_document(__SHADOWS_TYPE_NAMED, timeout_ms);
}
