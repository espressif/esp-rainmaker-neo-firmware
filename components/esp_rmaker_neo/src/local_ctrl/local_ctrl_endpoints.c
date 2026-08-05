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
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* Platform common includes. */
#include "osal_mem_alloc.h"
#include "osal_log.h"

/* RMNG includes. */
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

/**
 * @brief (Re)generate the get-data cache for the given data type.
 */
static esp_rmaker_error_t __ep_data_init(__local_ctrl_ep_data_type_t data_type)
{
    esp_rmaker_local_ctrl_endpoints_free_data();

    __ep_data = OSAL_CALLOC_EXTRAM(1, sizeof(__local_ctrl_ep_data_t));
    if (__ep_data == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate local ctrl data structure");
        return ESP_RMAKER_NO_MEM;
    }
    __ep_data->data_type = data_type;

    const char *data_name = (data_type == LOCAL_CTRL_EP_DATA_TYPE_PARAMS) ? "params" : "config";
    char *raw_data = (data_type == LOCAL_CTRL_EP_DATA_TYPE_PARAMS) ?
                     data_model_node_get_node_params() : esp_rmaker_get_node_config();
    if (raw_data == NULL) {
        OSAL_LOGE(TAG, "Failed to get node %s", data_name);
        esp_rmaker_local_ctrl_endpoints_free_data();
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

    /* Initialize response */
    RmakerLocalCtrl__RMakerLocalCtrlPayload response = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__INIT;
    RmakerLocalCtrl__RespGetData resp_get_data = RMAKER_LOCAL_CTRL__RESP_GET_DATA__INIT;
    RmakerLocalCtrl__PayloadBuf payload_buf = RMAKER_LOCAL_CTRL__PAYLOAD_BUF__INIT;

    response.msg = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_MSG_TYPE__TypeRespGetData;
    response.payload_case = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__PAYLOAD_RESP_GET_DATA;
    response.respgetdata = &resp_get_data;
    resp_get_data.buf = &payload_buf;

    uint32_t requested_offset = 0;
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
    }

    is_first_fragment = (requested_offset == 0);

    if (is_first_fragment) {
        OSAL_LOGI(TAG, "Get %s handler: offset=%u", data_name, (unsigned) requested_offset);
        esp_rmaker_error_t err = __ep_data_init(data_type);
        if (err != ESP_RMAKER_OK) {
            resp_get_data.status = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__NoMemory;
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
        return ESP_RMAKER_NO_MEM;
    }
    rmaker_local_ctrl__rmaker_local_ctrl_payload__pack(&response, resp_buf);
    *outbuf = resp_buf;
    *outlen = (ssize_t) resp_len;

    /* Free the cache AFTER serialization if this was the last fragment */
    if (is_last_fragment) {
        esp_rmaker_local_ctrl_endpoints_free_data();
        OSAL_LOGI(TAG, "Get %s completed, data freed", data_name);
    }

    return ESP_RMAKER_OK;
}

/* Public function definitions *******************************************************/

void esp_rmaker_local_ctrl_endpoints_free_data(void)
{
    if (__ep_data != NULL) {
        if (__ep_data->data != NULL) {
            free(__ep_data->data);
        }
        free(__ep_data);
        __ep_data = NULL;
    }
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

    OSAL_LOGI(TAG, "Set params handler invoked");
    if (outbuf == NULL || outlen == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (inbuf == NULL || inlen <= 0) {
        OSAL_LOGE(TAG, "Invalid params");
        return __send_error_response("invalid request", outbuf, outlen);
    }

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
