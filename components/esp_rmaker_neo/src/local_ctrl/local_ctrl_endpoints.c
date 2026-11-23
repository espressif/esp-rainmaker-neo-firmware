/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file local_ctrl/local_ctrl_endpoints.c
 * @brief Local control endpoint protocol handlers (get_params / set_params / get_config)
 *
 * Implements the local control endpoint protocol
 * (see docs/en/specs/local_ctrl_endpoint_protocol.md); the wire format is the
 * protocol contract - treat it as frozen.
 */

/* Includes *******************************************************/

/* Declaration includes. */
#include "local_ctrl/endpoints.h"
#include "local_ctrl/pb-c.h"

/* Standard includes. */
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* Platform common includes. */
#include "osal_mem_alloc.h"
#include "osal_log.h"
#include "osal_semaphore.h"

/* RainMaker Neo includes. */
#include "node_internal.h"
#include "data_model_internal.h"
#include "esp_rmaker_state.h"

/* Constants *******************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "rmng_local_ctrl_ep";

/**
 * @brief Maximum payload bytes per get-data response fragment.
 * @note Chosen to fit a single BLE GATT read alongside the protobuf framing;
 *       must not be increased without checking client expectations.
 */
#define LOCAL_CTRL_DATA_FRAGMENT_SIZE 200

#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_GET_FILTER
/** Largest CmdGetData.ParamsFilter, sized to fit one BLE characteristic write. */
#define LOCAL_CTRL_PARAMS_FILTER_MAX_JSON_LEN 384
/** Largest device, service or param name in a ParamsFilter. */
#define LOCAL_CTRL_PARAMS_FILTER_MAX_NAME_LEN 32
#endif

/* Types *******************************************************/

/** Data type served by the shared get-data handler. */
typedef enum {
    LOCAL_CTRL_EP_DATA_TYPE_PARAMS = 0,
    LOCAL_CTRL_EP_DATA_TYPE_CONFIG = 1,
} __local_ctrl_ep_data_type_t;

/** Cached payload during a fragmented get-data transfer. */
typedef struct {
    char *data;
    size_t data_len;
    __local_ctrl_ep_data_type_t data_type;
} __local_ctrl_ep_data_t;

/* Variables *******************************************************/

/**
 * @brief Fragmentation cache: (re)generated on an offset-0 request, sliced by
 *        subsequent requests, freed after the last fragment is served.
 * @note One transfer at a time (no per-session state), matching the reference
 *       implementation. Transports serialize handler invocations.
 */
static __local_ctrl_ep_data_t *__ep_data = NULL;

#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG
/** Reassembly buffer of one fragmented set_params transfer. */
typedef struct {
    uint8_t *data;
    uint32_t total_len;
    uint32_t received;
    uint32_t session_id;
} __local_ctrl_ep_set_frag_t;

static __local_ctrl_ep_set_frag_t *__set_frag = NULL;
#endif

/** Guards ::__ep_data and ::__set_frag: the handlers run on the transport task, the free on the caller's.
 * Created once and kept, since a handler may be blocked on it while a service is torn down. */
static osal_semaphore_handle_t __ep_lock = NULL;

/* Private function definitions *******************************************************/

/**
 * @brief Send a JSON error response: {"status":"fail","description":"<reason>"}.
 * @return ESP_RMAKER_OK always, so session-oriented transports keep the connection alive.
 */
static esp_rmaker_error_t __send_error_response(const char *reason, uint8_t **outbuf, ssize_t *outlen)
{
    static const char *fmt = "{\"status\":\"fail\",\"description\":\"%s\"}";
    const char *msg = reason ? reason : "unknown error";

    /* Measure, then format straight into the allocation: keeps a variable-length JSON
     * body off this handler's stack, and a long reason cannot be truncated. */
    int resp_len = snprintf(NULL, 0, fmt, msg);
    uint8_t *resp_buf = (resp_len < 0) ? NULL : OSAL_MALLOC_EXTRAM((size_t) resp_len + 1);
    if (resp_buf == NULL) {
        /* Last resort - if we can't even allocate the error message, return an empty response */
        *outbuf = NULL;
        *outlen = 0;
        return ESP_RMAKER_OK;
    }

    snprintf((char *) resp_buf, (size_t) resp_len + 1, fmt, msg);
    *outbuf = resp_buf;
    *outlen = (ssize_t) resp_len;
    return ESP_RMAKER_OK;
}

static void __ep_data_free_locked(void);

#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_GET_FILTER
/** Copy the decoded string of a token into a new buffer. */
static esp_rmaker_error_t __filter_copy_string(const jparse_ctx_t *jctx, json_tok_t *tok, char **out)
{
    jparse_ctx_t str_ctx = { .js = jctx->js, .tokens = tok };
    int len = 0;
    if (json_root_get_strlen(&str_ctx, &len) != 0 || len <= 0 || len > LOCAL_CTRL_PARAMS_FILTER_MAX_NAME_LEN) {
        return ESP_RMAKER_INVALID_ARG;
    }
    char *str = OSAL_CALLOC_EXTRAM(1, (size_t) len + 1);
    if (str == NULL) {
        return ESP_RMAKER_NO_MEM;
    }
    if (json_root_get_string(&str_ctx, str, len + 1) != 0) {
        free(str);
        return ESP_RMAKER_INVALID_ARG;
    }
    *out = str;
    return ESP_RMAKER_OK;
}

static void __filter_free(data_model_params_filter_t *filters, size_t filter_count)
{
    for (size_t i = 0; i < filter_count; i++) {
        free(filters[i].id);
        for (size_t p = 0; p < filters[i].param_count; p++) {
            free(filters[i].param_ids[p]);
        }
        free(filters[i].param_ids);
    }
    free(filters);
}

/** Parse {"params":{"<id>":["<param id>",...] or true,...}} into filter entries. */
static esp_rmaker_error_t __filter_parse(const uint8_t *json, size_t json_len,
        data_model_params_filter_t **filters, size_t *filter_count)
{
    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, (const char *) json, (int) json_len) != 0) {
        return ESP_RMAKER_INVALID_ARG;
    }
    json_tok_t *params = json_obj_get_val_tok(&jctx, "params", JSMN_OBJECT);
    if (params == NULL || params->size <= 0 || params->size > DATA_MODEL_PARAMS_FILTER_MAX_DEVICES) {
        json_parse_end(&jctx);
        return ESP_RMAKER_INVALID_ARG;
    }
    size_t count = (size_t) params->size;
    data_model_params_filter_t *parsed = OSAL_CALLOC_EXTRAM(count, sizeof(*parsed));
    if (parsed == NULL) {
        json_parse_end(&jctx);
        return ESP_RMAKER_NO_MEM;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    json_tok_t *key = params + 1;
    for (size_t i = 0; i < count && err == ESP_RMAKER_OK; i++) {
        err = __filter_copy_string(&jctx, key, &parsed[i].id);
        if (err != ESP_RMAKER_OK) {
            break;
        }
        json_tok_t *value = key + 1;
        if (value->type == JSMN_PRIMITIVE) {
            jparse_ctx_t value_ctx = { .js = jctx.js, .tokens = value };
            bool all_params = false;
            if (json_root_get_bool(&value_ctx, &all_params) != 0 || !all_params) {
                err = ESP_RMAKER_INVALID_ARG;
            }
            key = value + 1;
            continue;
        }
        if (value->type != JSMN_ARRAY || value->size <= 0 || value->size > DATA_MODEL_PARAMS_FILTER_MAX_PARAMS) {
            err = ESP_RMAKER_INVALID_ARG;
            break;
        }
        parsed[i].param_ids = OSAL_CALLOC_EXTRAM((size_t) value->size, sizeof(char *));
        if (parsed[i].param_ids == NULL) {
            err = ESP_RMAKER_NO_MEM;
            break;
        }
        parsed[i].param_count = (size_t) value->size;
        for (int p = 0; p < value->size && err == ESP_RMAKER_OK; p++) {
            json_tok_t *elem = value + 1 + p;
            err = (elem->type == JSMN_STRING) ? __filter_copy_string(&jctx, elem, &parsed[i].param_ids[p]) :
                  ESP_RMAKER_INVALID_ARG;
        }
        /* Each accepted array element is one string token. */
        key = value + 1 + value->size;
    }
    json_parse_end(&jctx);

    if (err != ESP_RMAKER_OK) {
        __filter_free(parsed, count);
        return err;
    }
    *filters = parsed;
    *filter_count = count;
    return ESP_RMAKER_OK;
}

/** Get the node params selected by a ParamsFilter. */
static esp_rmaker_error_t __filtered_params(const uint8_t *filter, size_t filter_len, char **out)
{
    data_model_params_filter_t *filters = NULL;
    size_t filter_count = 0;
    esp_rmaker_error_t err = __filter_parse(filter, filter_len, &filters, &filter_count);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Invalid get_params filter: %d", (int) err);
        return err;
    }
    *out = data_model_node_get_filtered_node_params(filters, filter_count);
    __filter_free(filters, filter_count);
    return (*out != NULL) ? ESP_RMAKER_OK : ESP_RMAKER_NO_MEM;
}
#endif /* CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_GET_FILTER */

/**
 * @brief (Re)generate the get-data cache for the given data type. The caller holds ::__ep_lock.
 * @note A non-empty filter selects the params; a malformed filter returns ESP_RMAKER_INVALID_ARG.
 */
static esp_rmaker_error_t __ep_data_init(__local_ctrl_ep_data_type_t data_type, const uint8_t *filter,
        size_t filter_len)
{
    __ep_data_free_locked();

    if (esp_rmaker_get_node() == NULL) {
        OSAL_LOGE(TAG, "No node to serve; call esp_rmaker_node_init() first");
        return ESP_RMAKER_INVALID_STATE;
    }

    __ep_data = OSAL_CALLOC_EXTRAM(1, sizeof(__local_ctrl_ep_data_t));
    if (__ep_data == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate local ctrl data structure");
        return ESP_RMAKER_NO_MEM;
    }
    __ep_data->data_type = data_type;

    const char *data_name = (data_type == LOCAL_CTRL_EP_DATA_TYPE_PARAMS) ? "params" : "config";
    char *raw_data = NULL;
#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_GET_FILTER
    if (filter_len > 0) {
        esp_rmaker_error_t err = __filtered_params(filter, filter_len, &raw_data);
        if (err != ESP_RMAKER_OK) {
            __ep_data_free_locked();
            return err;
        }
    } else
#else
    (void) filter;
    (void) filter_len;
#endif
    {
        raw_data = (data_type == LOCAL_CTRL_EP_DATA_TYPE_PARAMS) ?
                   data_model_node_get_node_params() : esp_rmaker_get_node_config();
    }
    if (raw_data == NULL) {
        OSAL_LOGE(TAG, "Failed to get node %s", data_name);
        __ep_data_free_locked();
        return ESP_RMAKER_NO_MEM;
    }

    __ep_data->data = raw_data;
    __ep_data->data_len = strlen(raw_data);
    OSAL_LOGI(TAG, "Get %s response (len=%d)", data_name, (int) __ep_data->data_len);
    return ESP_RMAKER_OK;
}

/**
 * @brief Shared get-data handler (params or config) with client-pull fragmentation.
 *
 * An offset-0 request (re)generates the payload cache; every request is answered
 * with up to LOCAL_CTRL_DATA_FRAGMENT_SIZE bytes at the requested offset plus the
 * total length. The cache is freed after the last fragment is served.
 */
static esp_rmaker_error_t __get_data_handler(const uint8_t *inbuf, ssize_t inlen,
        uint8_t **outbuf, ssize_t *outlen,
        __local_ctrl_ep_data_type_t data_type)
{
    if (outbuf == NULL || outlen == NULL) {
        OSAL_LOGE(TAG, "Invalid params");
        return ESP_RMAKER_INVALID_ARG;
    }

    const char *data_name = (data_type == LOCAL_CTRL_EP_DATA_TYPE_PARAMS) ? "params" : "config";

    /* Parse the received protobuf message */
    RmakerLocalCtrl__RMakerLocalCtrlPayload *msg = NULL;
    if (inbuf != NULL && inlen > 0) {
        msg = rmaker_local_ctrl__rmaker_local_ctrl_payload__unpack(NULL, (size_t) inlen, inbuf);
        if (msg == NULL) {
            OSAL_LOGE(TAG, "Failed to unpack message");
            return ESP_RMAKER_INVALID_ARG;
        }
    }

    /* Held through serialization: the response points into the cache. */
    if (__ep_lock == NULL || osal_semaphore_take(__ep_lock, OSAL_MAX_DELAY) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Get-data cache lock unavailable; call esp_rmaker_local_ctrl_endpoints_init() first");
        if (msg != NULL) {
            rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(msg, NULL);
        }
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Initialize response */
    RmakerLocalCtrl__RMakerLocalCtrlPayload response = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__INIT;
    RmakerLocalCtrl__RespGetData resp_get_data = RMAKER_LOCAL_CTRL__RESP_GET_DATA__INIT;
    RmakerLocalCtrl__PayloadBuf payload_buf = RMAKER_LOCAL_CTRL__PAYLOAD_BUF__INIT;

    response.msg = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_MSG_TYPE__TypeRespGetData;
    response.payload_case = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__PAYLOAD_RESP_GET_DATA;
    response.respgetdata = &resp_get_data;
    resp_get_data.buf = &payload_buf;

    uint32_t requested_offset = 0;
    const uint8_t *filter = NULL;
    size_t filter_len = 0;
    bool is_last_fragment = false;
    bool is_first_fragment = false;

    if (msg != NULL && msg->payload_case == RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__PAYLOAD_CMD_GET_DATA &&
            msg->cmdgetdata != NULL) {
        /* Verify the requested data type matches the endpoint */
        RmakerLocalCtrl__RMakerLocalCtrlDataType expected_type =
            (data_type == LOCAL_CTRL_EP_DATA_TYPE_PARAMS) ?
            RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_DATA_TYPE__TypeParams :
            RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_DATA_TYPE__TypeConfig;
        if (msg->cmdgetdata->datatype != expected_type) {
            OSAL_LOGE(TAG, "Data type mismatch in request");
            resp_get_data.status = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__InvalidParam;
            goto send_response;
        }
        requested_offset = msg->cmdgetdata->offset;
        /* Timestamp / HasTimestamp are reserved schema fields and ignored. */
        if (data_type == LOCAL_CTRL_EP_DATA_TYPE_PARAMS && requested_offset == 0 &&
                msg->cmdgetdata->paramsfilter.len > 0) {
#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_GET_FILTER
            filter = msg->cmdgetdata->paramsfilter.data;
            filter_len = msg->cmdgetdata->paramsfilter.len;
            if (filter_len > LOCAL_CTRL_PARAMS_FILTER_MAX_JSON_LEN) {
                OSAL_LOGE(TAG, "ParamsFilter too long (%u)", (unsigned) filter_len);
                resp_get_data.status = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__InvalidParam;
                goto send_response;
            }
#else
            OSAL_LOGW(TAG, "ParamsFilter ignored; CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_GET_FILTER is disabled");
#endif
        }
    }

    is_first_fragment = (requested_offset == 0);

    if (is_first_fragment) {
        OSAL_LOGI(TAG, "Get %s handler: offset=%u", data_name, (unsigned) requested_offset);
        esp_rmaker_error_t err = __ep_data_init(data_type, filter, filter_len);
        if (err != ESP_RMAKER_OK) {
            resp_get_data.status = (err == ESP_RMAKER_INVALID_STATE) ?
                                   RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__Fail :
                                   (err == ESP_RMAKER_INVALID_ARG) ?
                                   RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__InvalidParam :
                                   RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__NoMemory;
            goto send_response;
        }
    } else {
        OSAL_LOGD(TAG, "Get %s handler: offset=%u", data_name, (unsigned) requested_offset);
    }

    /* Check the cache is available and for the same data type */
    if (__ep_data == NULL || __ep_data->data == NULL || __ep_data->data_type != data_type) {
        OSAL_LOGE(TAG, "%s data not initialized (request must start at offset 0)", data_name);
        resp_get_data.status = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__Fail;
        goto send_response;
    }

    /* Validate requested offset */
    if (requested_offset > __ep_data->data_len) {
        OSAL_LOGE(TAG, "Requested offset %u exceeds %s length %d",
                  (unsigned) requested_offset, data_name, (int) __ep_data->data_len);
        resp_get_data.status = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__InvalidParam;
        goto send_response;
    }

    /* Slice the fragment */
    size_t remaining = __ep_data->data_len - requested_offset;
    size_t fragment_size = (remaining > LOCAL_CTRL_DATA_FRAGMENT_SIZE) ? LOCAL_CTRL_DATA_FRAGMENT_SIZE : remaining;

    payload_buf.offset = requested_offset;
    payload_buf.totallen = (uint32_t) __ep_data->data_len;
    payload_buf.payload.data = (uint8_t *)(__ep_data->data + requested_offset);
    payload_buf.payload.len = fragment_size;
    resp_get_data.status = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__Success;

    is_last_fragment = (requested_offset + fragment_size >= __ep_data->data_len);
    if (is_first_fragment || is_last_fragment) {
        OSAL_LOGI(TAG, "Sending %s%s%s fragment: offset=%u, len=%d, total=%d",
                  is_first_fragment ? "first " : "", is_last_fragment ? "last " : "",
                  data_name, (unsigned) requested_offset, (int) fragment_size, (int) __ep_data->data_len);
    }

send_response:
    if (msg != NULL) {
        rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(msg, NULL);
    }

    /* Serialize the response */
    size_t resp_len = rmaker_local_ctrl__rmaker_local_ctrl_payload__get_packed_size(&response);
    uint8_t *resp_buf = OSAL_MALLOC_EXTRAM(resp_len);
    if (resp_buf == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for response");
        osal_semaphore_give(__ep_lock);
        return ESP_RMAKER_NO_MEM;
    }
    rmaker_local_ctrl__rmaker_local_ctrl_payload__pack(&response, resp_buf);
    *outbuf = resp_buf;
    *outlen = (ssize_t) resp_len;

    /* Free the cache AFTER serialization if this was the last fragment */
    if (is_last_fragment) {
        __ep_data_free_locked();
        OSAL_LOGI(TAG, "Get %s completed, data freed", data_name);
    }

    osal_semaphore_give(__ep_lock);
    return ESP_RMAKER_OK;
}

#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG
/** Free the set_params reassembly buffer. The caller holds ::__ep_lock. */
static void __set_frag_free_locked(void)
{
    if (__set_frag != NULL) {
        free(__set_frag->data);
        free(__set_frag);
        __set_frag = NULL;
    }
}

/** Pack a RespSetData response. */
static esp_rmaker_error_t __set_frag_send(RmakerLocalCtrl__RMakerLocalCtrlStatus status, uint32_t received,
        uint8_t **outbuf, ssize_t *outlen)
{
    RmakerLocalCtrl__RMakerLocalCtrlPayload response = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__INIT;
    RmakerLocalCtrl__RespSetData resp_set_data = RMAKER_LOCAL_CTRL__RESP_SET_DATA__INIT;

    resp_set_data.status = status;
    resp_set_data.received = received;
    resp_set_data.maxlen = CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG_MAX_LEN;
    response.msg = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_MSG_TYPE__TypeRespSetData;
    response.payload_case = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__PAYLOAD_RESP_SET_DATA;
    response.respsetdata = &resp_set_data;

    size_t resp_len = rmaker_local_ctrl__rmaker_local_ctrl_payload__get_packed_size(&response);
    uint8_t *resp_buf = OSAL_MALLOC_EXTRAM(resp_len);
    if (resp_buf == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for set_params fragment response");
        return ESP_RMAKER_NO_MEM;
    }
    rmaker_local_ctrl__rmaker_local_ctrl_payload__pack(&response, resp_buf);
    *outbuf = resp_buf;
    *outlen = (ssize_t) resp_len;
    return ESP_RMAKER_OK;
}

/** Store one CmdSetData fragment, and apply the JSON when the transfer is complete. */
static esp_rmaker_error_t __set_frag_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
        uint8_t **outbuf, ssize_t *outlen)
{
    RmakerLocalCtrl__RMakerLocalCtrlPayload *msg =
        rmaker_local_ctrl__rmaker_local_ctrl_payload__unpack(NULL, (size_t) inlen, inbuf);
    if (msg == NULL) {
        OSAL_LOGE(TAG, "Failed to unpack set_params fragment");
        return __send_error_response("invalid request", outbuf, outlen);
    }

    if (__ep_lock == NULL || osal_semaphore_take(__ep_lock, OSAL_MAX_DELAY) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Set-data lock unavailable; call esp_rmaker_local_ctrl_endpoints_init() first");
        rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(msg, NULL);
        return ESP_RMAKER_INVALID_STATE;
    }

    RmakerLocalCtrl__RMakerLocalCtrlStatus status = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__InvalidParam;
    uint32_t received = 0;
    char *json = NULL;
    uint32_t json_len = 0;
    const RmakerLocalCtrl__PayloadBuf *frag = NULL;
    uint32_t offset = 0;
    uint32_t total = 0;
    size_t len = 0;

    if (msg->payload_case != RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__PAYLOAD_CMD_SET_DATA ||
            msg->cmdsetdata == NULL || msg->cmdsetdata->buf == NULL) {
        OSAL_LOGE(TAG, "set_params fragment request is not CmdSetData");
        __set_frag_free_locked();
        goto unlock;
    }

    frag = msg->cmdsetdata->buf;
    offset = frag->offset;
    total = frag->totallen;
    len = frag->payload.len;
    if (len == 0 || len > LOCAL_CTRL_DATA_FRAGMENT_SIZE || frag->payload.data == NULL || total == 0 ||
            total > CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG_MAX_LEN || offset > total ||
            len > (size_t)(total - offset)) {
        OSAL_LOGE(TAG, "Invalid set_params fragment: offset=%u, len=%u, total=%u",
                  (unsigned) offset, (unsigned) len, (unsigned) total);
        __set_frag_free_locked();
        goto unlock;
    }

    if (offset == 0) {
        __set_frag_free_locked();
        __set_frag = OSAL_CALLOC_EXTRAM(1, sizeof(*__set_frag));
        uint8_t *data = (__set_frag != NULL) ? OSAL_CALLOC_EXTRAM(1, (size_t) total + 1) : NULL;
        if (data == NULL) {
            OSAL_LOGE(TAG, "Failed to allocate memory for a %u byte set_params transfer", (unsigned) total);
            __set_frag_free_locked();
            status = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__NoMemory;
            goto unlock;
        }
        __set_frag->data = data;
        __set_frag->total_len = total;
        __set_frag->session_id = session_id;
        OSAL_LOGI(TAG, "Set params transfer started: total=%u", (unsigned) total);
    } else if (__set_frag == NULL || __set_frag->session_id != session_id ||
               __set_frag->total_len != total || __set_frag->received != offset) {
        OSAL_LOGE(TAG, "set_params fragment does not continue the transfer: offset=%u", (unsigned) offset);
        __set_frag_free_locked();
        goto unlock;
    }

    memcpy(__set_frag->data + offset, frag->payload.data, len);
    __set_frag->received = offset + (uint32_t) len;
    received = __set_frag->received;
    status = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__Success;
    OSAL_LOGD(TAG, "Set params fragment stored: %u/%u", (unsigned) received, (unsigned) total);

    if (received == total) {
        /* Detach the NUL-terminated buffer, so the app callbacks run without the lock. */
        json = (char *) __set_frag->data;
        json_len = total;
        __set_frag->data = NULL;
        __set_frag_free_locked();
    }

unlock:
    osal_semaphore_give(__ep_lock);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(msg, NULL);

    if (json != NULL) {
        esp_rmaker_error_t err = data_model_state_handle_update_payload_json(
                                     esp_rmaker_get_node(), json, json_len, ESP_RMAKER_REQ_SRC_LOCAL);
        free(json);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to handle fragmented set params: %d", (int) err);
            status = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__Fail;
            received = 0;
        } else {
            OSAL_LOGI(TAG, "Fragmented set params applied: %u bytes", (unsigned) json_len);
        }
    }

    return __set_frag_send(status, received, outbuf, outlen);
}
#endif /* CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG */

/* Public function definitions *******************************************************/

/** Free the cache. The caller holds ::__ep_lock. */
static void __ep_data_free_locked(void)
{
    if (__ep_data != NULL) {
        if (__ep_data->data != NULL) {
            free(__ep_data->data);
        }
        free(__ep_data);
        __ep_data = NULL;
    }
}

esp_rmaker_error_t esp_rmaker_local_ctrl_endpoints_init(void)
{
    if (__ep_lock == NULL) {
        __ep_lock = osal_semaphore_create_mutex();
        if (__ep_lock == NULL) {
            OSAL_LOGE(TAG, "Failed to create the get-data cache lock");
            return ESP_RMAKER_NO_MEM;
        }
    }
    return ESP_RMAKER_OK;
}

void esp_rmaker_local_ctrl_endpoints_free_data(void)
{
    /* Without the lock no handler has ever run, so there is nothing cached. */
    if (__ep_lock == NULL || osal_semaphore_take(__ep_lock, OSAL_MAX_DELAY) != OSAL_ERR_OK) {
        return;
    }
    __ep_data_free_locked();
#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG
    __set_frag_free_locked();
#endif
    osal_semaphore_give(__ep_lock);
}

void esp_rmaker_local_ctrl_endpoints_session_closed(uint32_t session_id)
{
#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG
    if (__ep_lock == NULL || osal_semaphore_take(__ep_lock, OSAL_MAX_DELAY) != OSAL_ERR_OK) {
        return;
    }
    if (__set_frag != NULL && __set_frag->session_id == session_id) {
        OSAL_LOGI(TAG, "Session closed, set_params transfer discarded");
        __set_frag_free_locked();
    }
    osal_semaphore_give(__ep_lock);
#else
    (void) session_id;
#endif
}

esp_rmaker_error_t esp_rmaker_local_ctrl_get_params_ep_handler(
    uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
    uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    (void) session_id;
    (void) priv_data;
    return __get_data_handler(inbuf, inlen, outbuf, outlen, LOCAL_CTRL_EP_DATA_TYPE_PARAMS);
}

esp_rmaker_error_t esp_rmaker_local_ctrl_get_config_ep_handler(
    uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
    uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    (void) session_id;
    (void) priv_data;
    return __get_data_handler(inbuf, inlen, outbuf, outlen, LOCAL_CTRL_EP_DATA_TYPE_CONFIG);
}

esp_rmaker_error_t esp_rmaker_local_ctrl_set_params_ep_handler(
    uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
    uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    (void) session_id;
    (void) priv_data;

    if (outbuf == NULL || outlen == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (inbuf == NULL || inlen <= 0) {
        OSAL_LOGE(TAG, "Invalid params");
        return __send_error_response("invalid request", outbuf, outlen);
    }

#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG
    /* A body that does not start with '{' is a CmdSetData fragment. */
    ssize_t first = 0;
    while (first < inlen && isspace((unsigned char) inbuf[first])) {
        first++;
    }
    if (first == inlen || inbuf[first] != '{') {
        return __set_frag_handler(session_id, inbuf, inlen, outbuf, outlen);
    }
#endif

    OSAL_LOGI(TAG, "Set params handler invoked");

    /* NUL-terminate the payload for the JSON parser */
    char *payload = OSAL_CALLOC_EXTRAM((size_t) inlen + 1, sizeof(char));
    if (payload == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for payload");
        return __send_error_response("no memory", outbuf, outlen);
    }
    memcpy(payload, inbuf, (size_t) inlen);

    esp_rmaker_error_t err = data_model_state_handle_update_payload_json(
                                 esp_rmaker_get_node(), payload, (size_t) inlen, ESP_RMAKER_REQ_SRC_LOCAL);
    free(payload);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to handle set params: %d", (int) err);
        return __send_error_response("failed to set params", outbuf, outlen);
    }

    /* Allocate a JSON success response */
    const char *success_msg = "{\"status\":\"success\"}";
    size_t resp_len = strlen(success_msg);
    uint8_t *resp_buf = OSAL_MALLOC_EXTRAM(resp_len);
    if (resp_buf == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for response");
        return __send_error_response("no memory", outbuf, outlen);
    }
    memcpy(resp_buf, success_msg, resp_len);
    *outbuf = resp_buf;
    *outlen = (ssize_t) resp_len;

    OSAL_LOGI(TAG, "Set params handler completed successfully");
    return ESP_RMAKER_OK;
}
