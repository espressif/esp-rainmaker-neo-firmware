/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file node_timeseries.c
 * @brief Timeseries implementation for RainMaker Neo.
 */

/* Includes *******************************************************/

/* Declarations */
#include "data_model_internal.h"
#include "timeseries.h"

/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* Platform common includes */
#include "osal_log.h"
#include "osal_time.h"
#include "osal_queue.h"
#include "osal_mem_alloc.h"

/* Network includes */
#include "network/common.h"
#include "network/mqtt_topics.h"
#include "network/mqtt_channels.h"

/* JSON includes */
#include "json_generator.h"

/* RMNG includes */
#include "esp_rmaker_val.h"
#include "esp_rmaker_work_queue.h"
#include "esp_rmaker_runtime_gate.h"
#include "retry/esp_rmaker_backoff.h"
#include "event_flags.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Timesync includes */
#include "osal_timesync.h"

/* Types *******************************************************/

/**
 * @brief Timeseries data.
 */
typedef struct {
    /** Whether the value is cumulative */
    bool is_cumulative;
    /** Parameter information */
    struct {
        char *path;           /* Parameter path. Dynamically allocated string, and will be freed by internal functions. */
        esp_rmaker_param_val_t val; /* Parameter value. Assumes a copy has already been made with esp_rmaker_val_copy(), and will be freed by internal functions. */
    } param;
    /** Timestamp information */
    struct {
        char *iana_tz; /* IANA timezone string. Assumes a dynamically allocated string, and will be freed by internal functions. */
        uint64_t utc_ms;   /* UTC milliseconds since epoch */
    } timestamp;
    /** Source topic ctx (non-owning pointer; must reference static
     *  storage - self ctx or a bridge child pool slot). Selects the
     *  publish topic so child-owned params go to the child's
     *  timeseries shadow. Entries whose ctx becomes invalid before
     *  publish are dropped on dequeue. */
    const esp_rmaker_topic_ctx_t *topic_ctx;
} __timeseries_data_t;

/* Preprocessor definitions *******************************************************/

/**
 * @brief Length of the timeseries data queue.
 */
#define __TIMESERIES_QUEUE_LENGTH CONFIG_RMAKER_TIMESERIES_DATA_QUEUE_LENGTH

/**
 * @brief Initial delay for the timeseries queue.
 */
#define __TIMESERIES_PUBLISH_INITIAL_DELAY_MS CONFIG_RMAKER_TIMESERIES_PUBLISH_INITIAL_DELAY_MS

/**
 * @brief Maximum delay for the timeseries publish.
 */
#define __TIMESERIES_PUBLISH_MAX_DELAY_MS CONFIG_RMAKER_TIMESERIES_PUBLISH_MAX_DELAY_MS

/**
 * @brief Exponential factor for the timeseries publish.
 */
#define __TIMESERIES_PUBLISH_EXP_FACTOR 2 /* 2x the delay */

/**
 * @brief Maximum jitter for the timeseries publish.
 */
#define __TIMESERIES_PUBLISH_MAX_JITTER_MS CONFIG_RMAKER_TIMESERIES_PUBLISH_INITIAL_DELAY_MS

/**
 * @brief Tag for logging.
 */
static const char *TAG = "rmng_node_timeseries";

/* Variables *******************************************************/

/**
 * @brief Underlying queue for timeseries data.
 */
static osal_queue_handle_t __timeseries_queue = NULL;

/**
 * @brief Retry context for the timeseries publish.
 */
static esp_rmaker_backoff_retry_context_t __timeseries_publish_retry_context;

/* Private function declarations *******************************************************/

/**
 * @brief Extract data type from esp_rmaker_val_type_t.
 * @param[in] jgen JSON generator.
 * @param[in] type The value type.
 * @return 0 on success, otherwise error code.
 */
static int __timeseries_extract_data_type(json_gen_str_t *jgen, esp_rmaker_val_type_t type);

/**
 * @brief Extract value from esp_rmaker_param_val_t union based on type.
 * @param[in] jgen JSON generator.
 * @param[in] val The parameter value.
 * @return 0 on success, otherwise error code.
 */
static int __timeseries_extract_value(json_gen_str_t *jgen, const esp_rmaker_param_val_t *val);

/**
 * @brief Generate JSON payload from linked list of timeseries data.
 * @param[in] data Timeseries data.
 * @param[in] buffer Buffer to write JSON into (NULL for size calculation).
 * @param[in] buffer_len Pointer to buffer length (input/output).
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __timeseries_generate_json(const __timeseries_data_t *data, char *buffer, size_t *buffer_len);

/**
 * @brief Convert timeseries data to a JSON string.
 * @param[in] data Timeseries data.
 * @return JSON string, or NULL if an error occurs.
 *         The caller is responsible for freeing the JSON string using free().
 */
static char *__timeseries_data_to_json(const __timeseries_data_t *data);

/**
 * @brief Push timeseries data to the queue.
 * @param[in] data Timeseries data.
 * @return ESP_RMAKER_OK on success, error on failure.
 */
static esp_rmaker_error_t __timeseries_push_data(const __timeseries_data_t *data);

/**
 * @brief Free internal data used by the timeseries data structure.
 * - Frees the IANA timezone string.
 * - Frees the parameter value if it is a string.
 * @param[in] data Pointer to the timeseries data structure.
 */
static void __timeseries_free_data_internals(const __timeseries_data_t *data);

/**
 * @brief Scheduler task to add the publish task to the work queue.
 * @param[in] unused Unused argument.
 */
static void __timeseries_publish_scheduler_task(void *unused);

/**
 * @brief Task to publish timeseries data to the MQTT broker.
 * @param[in] unused Unused argument.
 */
static void __timeseries_publish_task(void *unused);

/* Private function definitions *******************************************************/

static int __timeseries_extract_data_type(json_gen_str_t *jgen, esp_rmaker_val_type_t type)
{
    switch (type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        return json_gen_obj_set_string(jgen, "dt", "bool");
    case RMAKER_VAL_TYPE_INTEGER:
        return json_gen_obj_set_string(jgen, "dt", "int");
    case RMAKER_VAL_TYPE_FLOAT:
        return json_gen_obj_set_string(jgen, "dt", "float");
    case RMAKER_VAL_TYPE_STRING:
        return json_gen_obj_set_string(jgen, "dt", "string");
    case RMAKER_VAL_TYPE_OBJECT:
    case RMAKER_VAL_TYPE_ARRAY:
    case RMAKER_VAL_TYPE_INVALID:
    default:
        return -1;
    }
}

static int __timeseries_extract_value(json_gen_str_t *jgen, const esp_rmaker_param_val_t *val)
{
    if (!jgen || !val) {
        return -1;
    }

    switch (val->type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        return json_gen_obj_set_bool(jgen, "v", val->val.b);
    case RMAKER_VAL_TYPE_INTEGER:
        return json_gen_obj_set_int(jgen, "v", val->val.i);
    case RMAKER_VAL_TYPE_FLOAT:
        return json_gen_obj_set_float(jgen, "v", val->val.f);
    case RMAKER_VAL_TYPE_STRING:
        return json_gen_obj_set_string(jgen, "v", val->val.s);
    case RMAKER_VAL_TYPE_OBJECT:
    case RMAKER_VAL_TYPE_ARRAY:
    case RMAKER_VAL_TYPE_INVALID:
    default:
        return -1;
    }
}

static esp_rmaker_error_t __timeseries_generate_json(const __timeseries_data_t *data, char *buffer, size_t *buffer_len)
{
    if (!data) {
        *buffer_len = 0;
        return ESP_RMAKER_OK;
    }

    if (!data->param.path || !data->timestamp.iana_tz) {
        OSAL_LOGE(TAG, "Invalid data: (pointers) path=%p, iana_tz=%p", data->param.path, data->timestamp.iana_tz);
        return ESP_RMAKER_INVALID_ARG;
    }

    json_gen_str_t jstr;
    json_gen_str_start(&jstr, buffer, *buffer_len, NULL, NULL);

    if (json_gen_start_object(&jstr) != 0) {
        OSAL_LOGE(TAG, "Failed to start JSON object");
        return ESP_RMAKER_FAIL;
    }

    // Add parameter path under reporting key "k"
    if (json_gen_obj_set_string(&jstr, "k", data->param.path) != 0) {
        OSAL_LOGE(TAG, "Failed to add parameter path");
        return ESP_RMAKER_FAIL;
    }

    // Add data type
    if (__timeseries_extract_data_type(&jstr, data->param.val.type) != 0) {
        OSAL_LOGE(TAG, "Failed to add data type");
        return ESP_RMAKER_FAIL;
    }

    // Add value
    if (__timeseries_extract_value(&jstr, &data->param.val) != 0) {
        OSAL_LOGE(TAG, "Failed to add value");
        return ESP_RMAKER_FAIL;
    }

    // Add cumulative flag
    if (json_gen_obj_set_bool(&jstr, "cumulative", data->is_cumulative) != 0) {
        OSAL_LOGE(TAG, "Failed to add cumulative flag");
        return ESP_RMAKER_FAIL;
    }

    // Add timestamp
    if (json_gen_obj_set_int64(&jstr, "t", (int64_t)data->timestamp.utc_ms) != 0) {
        OSAL_LOGE(TAG, "Failed to add timestamp");
        return ESP_RMAKER_FAIL;
    }

    // Add timezone
    if (json_gen_obj_set_string(&jstr, "tz", data->timestamp.iana_tz) != 0) {
        OSAL_LOGE(TAG, "Failed to add timezone");
        return ESP_RMAKER_FAIL;
    }

    if (json_gen_end_object(&jstr) != 0) {
        OSAL_LOGE(TAG, "Failed to end item object");
        return ESP_RMAKER_FAIL;
    }

    *buffer_len = json_gen_str_end(&jstr);
    return ESP_RMAKER_OK;
}

static char *__timeseries_data_to_json(const __timeseries_data_t *data)
{
    if (!data) {
        return NULL;
    }

    char *payload = NULL;

    // First pass: get required JSON size
    size_t payload_size = 0;
    esp_rmaker_error_t err = __timeseries_generate_json(data, NULL, &payload_size);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to calculate required JSON size");
        goto timeseries_get_next_payload_fail;
    }

    // Allocate memory for payload
    payload = OSAL_CALLOC_EXTRAM(payload_size, sizeof(char));
    if (!payload) {
        OSAL_LOGE(TAG, "Failed to allocate memory for JSON payload");
        goto timeseries_get_next_payload_fail;
    }

    // Second pass: generate JSON
    err = __timeseries_generate_json(data, payload, &payload_size);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to generate JSON payload");
        goto timeseries_get_next_payload_fail;
    }

    OSAL_LOGD(TAG, "Generated JSON payload: %s", payload);
    return payload;

timeseries_get_next_payload_fail:
    if (payload) {
        free(payload);
    }
    return NULL;
}

static esp_rmaker_error_t __timeseries_push_data(const __timeseries_data_t *data)
{
    if (data == NULL) {
        OSAL_LOGE(TAG, "Invalid data pointer");
        return ESP_RMAKER_INVALID_ARG;
    }

    if (data->param.path == NULL || data->timestamp.iana_tz == NULL) {
        OSAL_LOGE(TAG, "Invalid data: (pointers) param.path=%p, timestamp.iana_tz=%p", data->param.path, data->timestamp.iana_tz);
        return ESP_RMAKER_INVALID_ARG;
    }

    if (__timeseries_queue == NULL) {
        OSAL_LOGE(TAG, "Timeseries not initialized; cannot push data");
        return ESP_RMAKER_FAIL;
    }

    // Validate data type - object and array types are not supported for timeseries
    esp_rmaker_val_type_t data_type = data->param.val.type;
    if (data_type == RMAKER_VAL_TYPE_OBJECT || data_type == RMAKER_VAL_TYPE_ARRAY) {
        OSAL_LOGE(TAG, "Object and array data types not supported for timeseries: %s",
                  data->param.path ? data->param.path : "unknown");
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_err_t err = osal_queue_send(__timeseries_queue, data, 0);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to push data to queue");
        return ESP_RMAKER_FAIL;
    }

    OSAL_LOGD(TAG, "Successfully pushed data to queue: %s", data->param.path);
    return ESP_RMAKER_OK;
}

static void __timeseries_free_data_internals(const __timeseries_data_t *data)
{
    if (!data) {
        return;
    }

    if (data->param.path) {
        free(data->param.path);
    }

    if (data->param.val.type == RMAKER_VAL_TYPE_STRING || data->param.val.type == RMAKER_VAL_TYPE_OBJECT || data->param.val.type == RMAKER_VAL_TYPE_ARRAY) {
        free(data->param.val.val.s);
    }

    if (data->timestamp.iana_tz) {
        free(data->timestamp.iana_tz);
    }
}

static void __timeseries_publish_scheduler_task(void *unused)
{
    /* Runtime gate: don't schedule a publish while stopping/stopped/resetting. */
    if (!esp_rmaker_should_do_work()) {
        return;
    }
    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(__timeseries_publish_task, NULL);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add timeseries publish task to work queue: %d", err);
    }
}

static void __timeseries_publish_task(void *unused)
{
    /* Runtime gate: bail before touching queued data if stopping/stopped. The
     * peeked/buffered data stays queued for a later resume. */
    if (!esp_rmaker_should_do_work()) {
        return;
    }

    __timeseries_data_t data;
    osal_err_t queue_err = osal_queue_receive(__timeseries_queue, &data, 0);
    if (queue_err != OSAL_ERR_OK) {
        return;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    char *json = NULL;
    do {
        // Publish JSON payload to MQTT broker. Topic resolved via the
        // entry's own topic ctx - same builder for self and bridge
        // children. Drop the entry if the topic can't be built.
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        int topic_len = esp_rmaker_mqtt_topic_timeseries_report(data.topic_ctx, topic, sizeof(topic));
        if (topic_len < 0 || (size_t)topic_len >= sizeof(topic)) {
            OSAL_LOGW(TAG, "Failed to build timeseries MQTT topic; dropping entry");
            err = ESP_RMAKER_OK; /* drop, don't retry */
            break;
        }

        // Convert timeseries data to JSON
        json = __timeseries_data_to_json(&data);
        if (!json) {
            OSAL_LOGE(TAG, "Failed to convert timeseries data to JSON");
            err = ESP_RMAKER_INVALID_ARG;
            break;
        }

        OSAL_LOGD(TAG, "Timeseries JSON payload: %s", json);

        osal_mqtt_event_loop_channel_t channel = {
            .main = MQTT_CHANNEL_MAIN_STATE_CHANGES,
            .sub = MQTT_CHANNEL_SUB_STATE_CHANGE_UPDATE_TIMESERIES,
        };
        OSAL_LOGI(TAG, "Publishing timeseries data using topic: %s", topic);
        osal_err_t status = esp_rmaker_mqtt_impl.publish(&channel, topic, strlen(topic), json, strlen(json) + 1, QoS1, false);
        if (status != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to publish timeseries data to MQTT broker: %d", status);
            err = ESP_RMAKER_FAIL;
            break;
        }
    } while (0);

    /* Free the JSON payload */
    if (json) {
        free(json);
    }

    /* Error handling */
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to publish timeseries data: %d - scheduling retry", err);
        __timeseries_push_data(&data);
        /* Do not re-arm if the SDK is stopping/stopped (runtime gate) after this
         * task was queued; buffered data stays for a later resume. */
        if (esp_rmaker_should_do_work()) {
            esp_rmaker_backoff_retry(&__timeseries_publish_retry_context, __timeseries_publish_scheduler_task, NULL);
        } else {
            OSAL_LOGD(TAG, "Timeseries publishing gated off; skipping retry scheduling");
        }
    } else {
        /* Free the data */
        __timeseries_free_data_internals(&data);

        /* Check for more data in the queue */
        queue_err = osal_queue_peek(__timeseries_queue, &data, 0);
        if (queue_err != OSAL_ERR_OK) {
            /* No more data in the queue, set the timeseries reported flag */
            esp_rmaker_event_flags_set_timeseries_reported();
            return;
        }

        /* Do not schedule the next publish if the SDK is stopping/stopped
         * (runtime gate); the peeked data remains queued for a later resume. */
        if (!esp_rmaker_should_do_work()) {
            OSAL_LOGD(TAG, "Timeseries publishing gated off; leaving buffered data queued");
            return;
        }

        /* Schedule the next publish task */
        esp_rmaker_backoff_reset(&__timeseries_publish_retry_context, __TIMESERIES_PUBLISH_INITIAL_DELAY_MS);
        esp_rmaker_backoff_fire(&__timeseries_publish_retry_context, __timeseries_publish_scheduler_task, NULL);
    }
}
/* Public function definitions *******************************************************/

esp_rmaker_error_t timeseries_init(void)
{
    if (__timeseries_queue != NULL) {
        OSAL_LOGW(TAG, "Timeseries already initialized");
        return ESP_RMAKER_OK;
    }

    __timeseries_queue = osal_queue_create_ext(__TIMESERIES_QUEUE_LENGTH, sizeof(__timeseries_data_t));
    if (__timeseries_queue == NULL) {
        OSAL_LOGE(TAG, "Failed to create timeseries queue");
        return ESP_RMAKER_FAIL;
    }

    /* Initialize the retry context */
    __timeseries_publish_retry_context = (esp_rmaker_backoff_retry_context_t) {
        .handle = NULL,
        .delay_ctx = {
            .delay_ms = {
                .current = __TIMESERIES_PUBLISH_INITIAL_DELAY_MS,
                .max = __TIMESERIES_PUBLISH_MAX_DELAY_MS,
            },
            .params = {
                .exp_factor = __TIMESERIES_PUBLISH_EXP_FACTOR,
                .max_jitter_ms = __TIMESERIES_PUBLISH_MAX_JITTER_MS,
            },
        },
    };

    OSAL_LOGI(TAG, "Timeseries initialized successfully");
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t timeseries_stop(void)
{
    if (__timeseries_queue == NULL) {
        return ESP_RMAKER_OK;
    }
    esp_rmaker_backoff_reset(&__timeseries_publish_retry_context, __TIMESERIES_PUBLISH_INITIAL_DELAY_MS);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t timeseries_deinit(void)
{
    if (__timeseries_queue == NULL) {
        OSAL_LOGW(TAG, "Timeseries not initialized; cannot deinitialize");
        return ESP_RMAKER_OK;
    }

    /* Cancel any scheduled retries */
    esp_rmaker_backoff_reset(&__timeseries_publish_retry_context, __TIMESERIES_PUBLISH_INITIAL_DELAY_MS);

    /* Drain the queue */
    __timeseries_data_t data;
    while (osal_queue_receive(__timeseries_queue, &data, 0) == OSAL_ERR_OK) {
        __timeseries_free_data_internals(&data);
    }

    /* Delete the queue */
    osal_queue_delete(__timeseries_queue);
    __timeseries_queue = NULL;

    OSAL_LOGI(TAG, "Timeseries deinitialized successfully");
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t timeseries_push_data_new(const esp_rmaker_topic_ctx_t *topic_ctx, const esp_rmaker_state_update_id_t update_id, const esp_rmaker_param_val_t *p_val, uint64_t timestamp_ms, bool is_cumulative)
{
    if (!p_val || !update_id) {
        OSAL_LOGE(TAG, "Invalid parameter value pointer or update ID");
        return ESP_RMAKER_INVALID_ARG;
    }

    char *path = data_model_update_id_to_path(update_id);
    if (!path) {
        OSAL_LOGE(TAG, "Failed to get parameter path");
        return ESP_RMAKER_INVALID_ARG;
    }

    char *iana_tz = osal_timesync_get_timezone();
    if (!iana_tz) {
        OSAL_LOGE(TAG, "Failed to get timezone IANA string");
        free(path);
        return ESP_RMAKER_NO_MEM;
    }

    esp_rmaker_param_val_t val;
    esp_rmaker_error_t err = esp_rmaker_val_copy(p_val, &val);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to copy parameter value");
        free(path);
        free(iana_tz);
        return err;
    }

    __timeseries_data_t data = {
        .is_cumulative = is_cumulative,
        .param = {
            .path = path,
            .val = val,
        },
        .timestamp = {
            .utc_ms = timestamp_ms,
            .iana_tz = iana_tz,
        },
        .topic_ctx = topic_ctx ? topic_ctx :&esp_rmaker_topic_ctx_self,
    };

    err = __timeseries_push_data(&data);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to push timeseries data: %d", err);
        __timeseries_free_data_internals(&data);
        return err;
    }

    return esp_rmaker_backoff_fire(&__timeseries_publish_retry_context, __timeseries_publish_scheduler_task, NULL);
}
