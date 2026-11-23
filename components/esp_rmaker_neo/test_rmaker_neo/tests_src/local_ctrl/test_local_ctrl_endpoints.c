/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_local_ctrl_endpoints.c
 * @brief Local control endpoint protocol handlers.
 *
 * The endpoint names and the protobuf message/field numbering are a frozen wire
 * contract (docs/en/specs/local_ctrl_endpoint_protocol.md), so these cover the paths a
 * deployed client can drive: the client-pull fragmentation walk and the negative
 * paths around it. The handlers are transport-agnostic and take plain buffers, so
 * they are exercised directly with no protocomm instance.
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <stdlib.h>
#include <string.h>

#include "protobuf-c/protobuf-c.h"
#include "local_ctrl/pb-c.h"
#include "local_ctrl/endpoints.h"

#include "esp_rmaker_core.h"
#include "esp_rmaker_data_model.h"
#include "esp_rmaker_flow.h"
#include "esp_rmaker_val.h"
#include "network/state_changes.h"
#include "osal_event_loop.h"
#include "osal_mem_alloc.h"

/* Must match LOCAL_CTRL_DATA_FRAGMENT_SIZE in local_ctrl_endpoints.c: it is part of
 * the wire behaviour clients are written against, so a change here should be
 * deliberate. */
#define TEST_LC_FRAGMENT_SIZE 200

/* Shorthands for the generated enum names. */
#define LC_STATUS_SUCCESS RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__Success
#define LC_STATUS_FAIL    RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__Fail
#define LC_TYPE_PARAMS    RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_DATA_TYPE__TypeParams
#define LC_TYPE_CONFIG    RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_DATA_TYPE__TypeConfig
#define LC_MSG_CMD_GET    RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_MSG_TYPE__TypeCmdGetData
#define LC_CASE_CMD_GET   RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__PAYLOAD_CMD_GET_DATA

static esp_rmaker_node_t *s_lc_node = NULL;
static esp_rmaker_param_t *s_lc_param = NULL;
static int s_lc_writes = 0;
static bool s_lc_power = false;

/* Helpers *******************************************************/

/** Record each power write, so a test can check what set_params applied. */
static esp_rmaker_error_t __lc_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                                        const esp_rmaker_param_val_t val, void *priv_data,
                                        esp_rmaker_write_ctx_t *ctx)
{
    (void) device;
    (void) param;
    (void) priv_data;
    (void) ctx;
    s_lc_writes++;
    s_lc_power = val.val.b;
    return ESP_RMAKER_OK;
}

/** Bring up a node with one writable param, so params/config JSON is non-trivial. */
static void __lc_node_setup(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_endpoints_init());

    /* The data model reports state changes through the event loop and the state
     * tracker, so both must exist before a node can be created. */
    osal_event_loop_create_default();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_state_init());

    esp_rmaker_config_t cfg = { .enable_time_sync = false };
    s_lc_node = esp_rmaker_node_init(&cfg, "lc-test-node", "lc-test-type");
    TEST_ASSERT_NOT_NULL(s_lc_node);

    esp_rmaker_device_t *dev = esp_rmaker_device_create("lc_dev", "test_type", NULL);
    TEST_ASSERT_NOT_NULL(dev);
    s_lc_writes = 0;
    s_lc_power = false;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_cb(dev, __lc_write_cb, NULL));
    s_lc_param = esp_rmaker_param_create("power", "test_power", esp_rmaker_bool(false),
                                         PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_NOT_NULL(s_lc_param);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, s_lc_param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(s_lc_node, dev));
}

static void __lc_node_teardown(void)
{
    esp_rmaker_local_ctrl_endpoints_free_data();
    if (s_lc_node) {
        esp_rmaker_node_deinit(s_lc_node);
        s_lc_node = NULL;
    }
    s_lc_param = NULL;
    (void) esp_rmaker_state_deinit();
    osal_event_loop_delete_default();
}

/** Pack a CmdGetData request. Caller frees *out via free(). */
static void __lc_pack_cmd(RmakerLocalCtrl__RMakerLocalCtrlDataType data_type, uint32_t offset,
                          uint8_t **out, size_t *out_len)
{
    RmakerLocalCtrl__CmdGetData cmd = RMAKER_LOCAL_CTRL__CMD_GET_DATA__INIT;
    cmd.datatype = data_type;
    cmd.offset = offset;

    RmakerLocalCtrl__RMakerLocalCtrlPayload payload = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__INIT;
    payload.msg = LC_MSG_CMD_GET;
    payload.payload_case = LC_CASE_CMD_GET;
    payload.cmdgetdata = &cmd;

    size_t len = rmaker_local_ctrl__rmaker_local_ctrl_payload__get_packed_size(&payload);
    TEST_ASSERT_GREATER_THAN(0, len);
    uint8_t *buf = malloc(len);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_EQUAL(len, rmaker_local_ctrl__rmaker_local_ctrl_payload__pack(&payload, buf));
    *out = buf;
    *out_len = len;
}

/**
 * @brief Drive one get_params/get_config request and unpack the reply.
 *
 * Returns the unpacked payload (caller frees via
 * rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked), or NULL if the
 * handler produced no response buffer.
 */
static RmakerLocalCtrl__RMakerLocalCtrlPayload *__lc_request(
    bool config, RmakerLocalCtrl__RMakerLocalCtrlDataType data_type, uint32_t offset,
    esp_rmaker_error_t *handler_err)
{
    uint8_t *req = NULL;
    size_t req_len = 0;
    __lc_pack_cmd(data_type, offset, &req, &req_len);

    uint8_t *resp = NULL;
    ssize_t resp_len = 0;
    esp_rmaker_error_t err = config
                             ? esp_rmaker_local_ctrl_get_config_ep_handler(1, req, (ssize_t) req_len, &resp, &resp_len, NULL)
                             : esp_rmaker_local_ctrl_get_params_ep_handler(1, req, (ssize_t) req_len, &resp, &resp_len, NULL);
    free(req);
    if (handler_err) {
        *handler_err = err;
    }
    if (resp == NULL || resp_len <= 0) {
        free(resp);
        return NULL;
    }

    RmakerLocalCtrl__RMakerLocalCtrlPayload *unpacked =
        rmaker_local_ctrl__rmaker_local_ctrl_payload__unpack(NULL, (size_t) resp_len, resp);
    free(resp);
    return unpacked;
}

/* Tests *******************************************************/

/**
 * The whole client-pull walk: offset 0 caches and returns the first fragment, each
 * subsequent offset continues, and the reassembled bytes equal the full payload.
 * Also pins the invariants a client relies on - TotalLen stable across fragments,
 * no fragment over the limit, offsets contiguous.
 */
void test_local_ctrl_get_params_fragment_walk(void)
{
    __lc_node_setup();

    char *reassembled = NULL;
    size_t got = 0;
    uint32_t total = 0;
    uint32_t offset = 0;
    int fragments = 0;

    do {
        esp_rmaker_error_t err = ESP_RMAKER_FAIL;
        RmakerLocalCtrl__RMakerLocalCtrlPayload *resp = __lc_request(false, LC_TYPE_PARAMS, offset, &err);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
        TEST_ASSERT_NOT_NULL(resp);
        TEST_ASSERT_NOT_NULL(resp->respgetdata);
        TEST_ASSERT_EQUAL(LC_STATUS_SUCCESS, resp->respgetdata->status);
        TEST_ASSERT_NOT_NULL(resp->respgetdata->buf);

        RmakerLocalCtrl__PayloadBuf *buf = resp->respgetdata->buf;
        TEST_ASSERT_EQUAL_UINT32(offset, buf->offset);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(TEST_LC_FRAGMENT_SIZE, (uint32_t) buf->payload.len);
        if (fragments == 0) {
            total = buf->totallen;
            TEST_ASSERT_GREATER_THAN_UINT32(0, total);
            reassembled = malloc(total + 1);
            TEST_ASSERT_NOT_NULL(reassembled);
        } else {
            /* TotalLen must not move mid-transfer, or a client cannot size its buffer. */
            TEST_ASSERT_EQUAL_UINT32(total, buf->totallen);
        }

        TEST_ASSERT_LESS_OR_EQUAL_UINT32(total, offset + (uint32_t) buf->payload.len);
        memcpy(reassembled + got, buf->payload.data, buf->payload.len);
        got += buf->payload.len;
        offset += (uint32_t) buf->payload.len;
        fragments++;

        rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(resp, NULL);
        TEST_ASSERT_LESS_THAN_INT(64, fragments); /* runaway guard */
    } while (offset < total);

    TEST_ASSERT_EQUAL_UINT32(total, got);
    reassembled[got] = '\0';
    /* Params JSON, so at minimum a JSON object carrying the device we added. */
    TEST_ASSERT_EQUAL_CHAR('{', reassembled[0]);
    TEST_ASSERT_NOT_NULL(strstr(reassembled, "lc_dev"));

    free(reassembled);
    __lc_node_teardown();
}

/** get_config walks the same way and yields the device/param names. */
void test_local_ctrl_get_config_fragment_walk(void)
{
    __lc_node_setup();

    uint32_t total = 0;
    uint32_t offset = 0;
    char *reassembled = NULL;
    size_t got = 0;
    int fragments = 0;

    do {
        RmakerLocalCtrl__RMakerLocalCtrlPayload *resp = __lc_request(true, LC_TYPE_CONFIG, offset, NULL);
        TEST_ASSERT_NOT_NULL(resp);
        TEST_ASSERT_EQUAL(LC_STATUS_SUCCESS, resp->respgetdata->status);
        RmakerLocalCtrl__PayloadBuf *buf = resp->respgetdata->buf;
        if (fragments == 0) {
            total = buf->totallen;
            reassembled = malloc(total + 1);
            TEST_ASSERT_NOT_NULL(reassembled);
        }
        memcpy(reassembled + got, buf->payload.data, buf->payload.len);
        got += buf->payload.len;
        offset += (uint32_t) buf->payload.len;
        fragments++;
        rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(resp, NULL);
        TEST_ASSERT_LESS_THAN_INT(64, fragments);
    } while (offset < total);

    reassembled[got] = '\0';
    TEST_ASSERT_NOT_NULL(strstr(reassembled, "lc_dev"));
    TEST_ASSERT_NOT_NULL(strstr(reassembled, "power"));
    free(reassembled);

    __lc_node_teardown();
}

/**
 * A non-zero offset with nothing cached must fail rather than serve stale or
 * partial data: the spec makes offset 0 the only thing that (re)generates.
 */
void test_local_ctrl_get_params_offset_without_reset_fails(void)
{
    __lc_node_setup();

    RmakerLocalCtrl__RMakerLocalCtrlPayload *resp = __lc_request(false, LC_TYPE_PARAMS, 32, NULL);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_NOT_NULL(resp->respgetdata);
    TEST_ASSERT_EQUAL(LC_STATUS_FAIL, resp->respgetdata->status);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(resp, NULL);

    __lc_node_teardown();
}

/** An offset past the end of the cached payload fails instead of over-reading. */
void test_local_ctrl_get_params_offset_beyond_total_fails(void)
{
    __lc_node_setup();

    RmakerLocalCtrl__RMakerLocalCtrlPayload *first = __lc_request(false, LC_TYPE_PARAMS, 0, NULL);
    TEST_ASSERT_NOT_NULL(first);
    uint32_t total = first->respgetdata->buf->totallen;
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(first, NULL);

    RmakerLocalCtrl__RMakerLocalCtrlPayload *resp = __lc_request(false, LC_TYPE_PARAMS, total + 1, NULL);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL(LC_STATUS_FAIL, resp->respgetdata->status);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(resp, NULL);

    __lc_node_teardown();
}

/**
 * The cache holds one data type at a time. Starting a params transfer then asking
 * for config at a non-zero offset must fail rather than return params bytes
 * labelled as config.
 */
void test_local_ctrl_get_data_type_mismatch_fails(void)
{
    __lc_node_setup();

    RmakerLocalCtrl__RMakerLocalCtrlPayload *params = __lc_request(false, LC_TYPE_PARAMS, 0, NULL);
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL(LC_STATUS_SUCCESS, params->respgetdata->status);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(params, NULL);

    RmakerLocalCtrl__RMakerLocalCtrlPayload *resp = __lc_request(true, LC_TYPE_CONFIG, 8, NULL);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL(LC_STATUS_FAIL, resp->respgetdata->status);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(resp, NULL);

    __lc_node_teardown();
}

/**
 * Teardown drops the cache, so a client that was mid-transfer when the service
 * stopped cannot resume against freed data.
 */
void test_local_ctrl_free_data_invalidates_cache(void)
{
    __lc_node_setup();

    RmakerLocalCtrl__RMakerLocalCtrlPayload *first = __lc_request(false, LC_TYPE_PARAMS, 0, NULL);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL(LC_STATUS_SUCCESS, first->respgetdata->status);
    uint32_t next = (uint32_t) first->respgetdata->buf->payload.len;
    uint32_t total = first->respgetdata->buf->totallen;
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(first, NULL);

    /* Only meaningful if the payload actually spans more than one fragment. */
    if (next < total) {
        esp_rmaker_local_ctrl_endpoints_free_data();
        RmakerLocalCtrl__RMakerLocalCtrlPayload *resp = __lc_request(false, LC_TYPE_PARAMS, next, NULL);
        TEST_ASSERT_NOT_NULL(resp);
        TEST_ASSERT_EQUAL(LC_STATUS_FAIL, resp->respgetdata->status);
        rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(resp, NULL);
    }

    /* Idempotent: safe with nothing cached. */
    esp_rmaker_local_ctrl_endpoints_free_data();
    esp_rmaker_local_ctrl_endpoints_free_data();

    __lc_node_teardown();
}

/** A frame that is not a valid RMakerLocalCtrlPayload must not be treated as one. */
void test_local_ctrl_get_params_malformed_frame_rejected(void)
{
    __lc_node_setup();

    const uint8_t garbage[] = { 0xff, 0xff, 0xff, 0xff, 0x7f, 0x41, 0x42 };
    uint8_t *resp = NULL;
    ssize_t resp_len = 0;
    esp_rmaker_error_t err = esp_rmaker_local_ctrl_get_params_ep_handler(
                                 1, garbage, (ssize_t) sizeof(garbage), &resp, &resp_len, NULL);

    /* Either an in-band failure status or a handler error - but never a Success
     * response, and never a response the client would parse as data. */
    if (err == ESP_RMAKER_OK && resp != NULL && resp_len > 0) {
        RmakerLocalCtrl__RMakerLocalCtrlPayload *unpacked =
            rmaker_local_ctrl__rmaker_local_ctrl_payload__unpack(NULL, (size_t) resp_len, resp);
        if (unpacked != NULL) {
            TEST_ASSERT_NOT_EQUAL(LC_STATUS_SUCCESS, unpacked->respgetdata->status);
            rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(unpacked, NULL);
        }
    } else {
        TEST_ASSERT_NOT_EQUAL(ESP_RMAKER_OK, err);
    }
    free(resp);

    __lc_node_teardown();
}

/**
 * set_params takes raw JSON and answers raw JSON. Invalid input must still return
 * ESP_RMAKER_OK with a failure body: the handler documents that it never fails the
 * call, so a session transport (BLE) does not drop the connection on bad input.
 */
void test_local_ctrl_set_params_invalid_json_keeps_session(void)
{
    __lc_node_setup();

    const char *bad = "{ this is not json";
    uint8_t *resp = NULL;
    ssize_t resp_len = 0;
    esp_rmaker_error_t err = esp_rmaker_local_ctrl_set_params_ep_handler(
                                 1, (const uint8_t *) bad, (ssize_t) strlen(bad), &resp, &resp_len, NULL);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_GREATER_THAN_INT(0, (int) resp_len);
    TEST_ASSERT_NOT_NULL(strstr((const char *) resp, "fail"));
    free(resp);

    __lc_node_teardown();
}

/** A well-formed set_params body reports success. */
void test_local_ctrl_set_params_valid_json_succeeds(void)
{
    __lc_node_setup();

    const char *good = "{\"lc_dev\":{\"power\":true}}";
    uint8_t *resp = NULL;
    ssize_t resp_len = 0;
    esp_rmaker_error_t err = esp_rmaker_local_ctrl_set_params_ep_handler(
                                 1, (const uint8_t *) good, (ssize_t) strlen(good), &resp, &resp_len, NULL);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_NOT_NULL(strstr((const char *) resp, "success"));
    free(resp);

    __lc_node_teardown();
}

/** Before esp_rmaker_node_init() there is nothing to serve. That is a state error, and a
 * client must be able to tell it from an allocation failure. */
void test_local_ctrl_get_params_without_node_fails(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_endpoints_init());
    TEST_ASSERT_NULL(esp_rmaker_get_node());

    esp_rmaker_error_t err = ESP_RMAKER_FAIL;
    RmakerLocalCtrl__RMakerLocalCtrlPayload *resp = __lc_request(false, LC_TYPE_PARAMS, 0, &err);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_NOT_NULL(resp->respgetdata);
    TEST_ASSERT_EQUAL(LC_STATUS_FAIL, resp->respgetdata->status);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(resp, NULL);

    esp_rmaker_local_ctrl_endpoints_free_data();
}

#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_GET_FILTER

#define LC_FILTER_INVALID RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__InvalidParam

/** Add a second device "lc_aux" with the int params "a" = 1 and "b" = 2. */
static void __lc_add_aux_device(void)
{
    esp_rmaker_device_t *dev = esp_rmaker_device_create("lc_aux", "test_type", NULL);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev,
                      esp_rmaker_param_create("a", "test_a", esp_rmaker_int(1), PROP_FLAG_READ)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev,
                      esp_rmaker_param_create("b", "test_b", esp_rmaker_int(2), PROP_FLAG_READ)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(s_lc_node, dev));
}

/** Send an offset-0 request with a ParamsFilter. Return the status, and the payload text on success. */
static RmakerLocalCtrl__RMakerLocalCtrlStatus __lc_get_filtered(bool config, const char *filter, size_t filter_len,
        char *out, size_t out_size)
{
    RmakerLocalCtrl__CmdGetData cmd = RMAKER_LOCAL_CTRL__CMD_GET_DATA__INIT;
    cmd.datatype = config ? LC_TYPE_CONFIG : LC_TYPE_PARAMS;
    cmd.paramsfilter.data = (uint8_t *) filter;
    cmd.paramsfilter.len = filter_len;
    RmakerLocalCtrl__RMakerLocalCtrlPayload payload = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__INIT;
    payload.msg = LC_MSG_CMD_GET;
    payload.payload_case = LC_CASE_CMD_GET;
    payload.cmdgetdata = &cmd;

    size_t req_len = rmaker_local_ctrl__rmaker_local_ctrl_payload__get_packed_size(&payload);
    uint8_t *req = malloc(req_len);
    TEST_ASSERT_NOT_NULL(req);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__pack(&payload, req);

    uint8_t *resp = NULL;
    ssize_t resp_len = 0;
    esp_rmaker_error_t err = config
                             ? esp_rmaker_local_ctrl_get_config_ep_handler(1, req, (ssize_t) req_len, &resp, &resp_len, NULL)
                             : esp_rmaker_local_ctrl_get_params_ep_handler(1, req, (ssize_t) req_len, &resp, &resp_len, NULL);
    free(req);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(resp);
    RmakerLocalCtrl__RMakerLocalCtrlPayload *unpacked =
        rmaker_local_ctrl__rmaker_local_ctrl_payload__unpack(NULL, (size_t) resp_len, resp);
    free(resp);
    TEST_ASSERT_NOT_NULL(unpacked);
    TEST_ASSERT_NOT_NULL(unpacked->respgetdata);

    RmakerLocalCtrl__RMakerLocalCtrlStatus status = unpacked->respgetdata->status;
    out[0] = '\0';
    if (status == LC_STATUS_SUCCESS) {
        const RmakerLocalCtrl__PayloadBuf *buf = unpacked->respgetdata->buf;
        TEST_ASSERT_NOT_NULL(buf);
        TEST_ASSERT_LESS_THAN(out_size, buf->payload.len);
        memcpy(out, buf->payload.data, buf->payload.len);
        out[buf->payload.len] = '\0';
    }
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(unpacked, NULL);
    esp_rmaker_local_ctrl_endpoints_free_data();
    return status;
}

static void __lc_expect_filtered(const char *filter, const char *expected)
{
    char out[TEST_LC_FRAGMENT_SIZE + 1];
    TEST_ASSERT_EQUAL_MESSAGE(LC_STATUS_SUCCESS, __lc_get_filtered(false, filter, strlen(filter), out, sizeof(out)),
                              filter);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, out, filter);
}

/** A list selects the named params, true selects all of them, and the order follows the filter. */
void test_local_ctrl_get_params_filter_selects_params(void)
{
    __lc_node_setup();
    __lc_add_aux_device();

    __lc_expect_filtered("{\"params\":{\"lc_aux\":[\"b\"]}}", "{\"lc_aux\":{\"b\":2}}");
    __lc_expect_filtered("{\"params\":{\"lc_aux\":true,\"lc_dev\":[\"power\"]}}",
                         "{\"lc_aux\":{\"a\":1,\"b\":2},\"lc_dev\":{\"power\":false}}");

    /* get_config ignores the filter. */
    char full[TEST_LC_FRAGMENT_SIZE + 1];
    char filtered[TEST_LC_FRAGMENT_SIZE + 1];
    const char *filter = "{\"params\":{\"lc_aux\":[\"b\"]}}";
    TEST_ASSERT_EQUAL(LC_STATUS_SUCCESS, __lc_get_filtered(true, "", 0, full, sizeof(full)));
    TEST_ASSERT_EQUAL(LC_STATUS_SUCCESS, __lc_get_filtered(true, filter, strlen(filter), filtered, sizeof(filtered)));
    TEST_ASSERT_EQUAL_STRING(full, filtered);

    __lc_node_teardown();
}

/** Unknown and duplicate IDs are skipped; when none is left, the response is {}. */
void test_local_ctrl_get_params_filter_skips_unknown_and_duplicates(void)
{
    __lc_node_setup();
    __lc_add_aux_device();

    __lc_expect_filtered("{\"params\":{\"nope\":true,\"lc_aux\":[\"x\",\"b\",\"b\"],\"lc_aux\":[\"a\"]}}",
                         "{\"lc_aux\":{\"b\":2}}");
    __lc_expect_filtered("{\"params\":{\"nope\":true,\"lc_dev\":[\"x\"]}}", "{}");

    __lc_node_teardown();
}

/** A malformed or too long filter gets InvalidParam, and the next request still works. */
void test_local_ctrl_get_params_filter_malformed_rejected(void)
{
    static const char *const malformed[] = {
        "not json",
        "{\"other\":{}}",
        "{\"params\":{}}",
        "{\"params\":[]}",
        "{\"params\":{\"lc_aux\":false}}",
        "{\"params\":{\"lc_aux\":[]}}",
        "{\"params\":{\"lc_aux\":[1]}}",
        "{\"params\":{\"lc_aux\":[\"a\",[\"b\"]]}}",
        "{\"params\":{\"lc_aux\":{\"a\":true}}}",
        "{\"params\":{\"lc_aux\":[\"\"]}}",
        "{\"params\":{\"abcdefghijklmnopqrstuvwxyz0123456\":true}}",
        "{\"params\":{\"a\":true,\"b\":true,\"c\":true,\"d\":true,\"e\":true,\"f\":true,\"g\":true,\"h\":true,"
        "\"i\":true}}",
        "{\"params\":{\"lc_aux\":[\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\",\"8\",\"9\",\"10\",\"11\",\"12\",\"13\","
        "\"14\",\"15\",\"16\",\"17\"]}}",
    };

    __lc_node_setup();
    __lc_add_aux_device();

    char out[TEST_LC_FRAGMENT_SIZE + 1];
    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++) {
        TEST_ASSERT_EQUAL_MESSAGE(LC_FILTER_INVALID,
                                  __lc_get_filtered(false, malformed[i], strlen(malformed[i]), out, sizeof(out)),
                                  malformed[i]);
    }

    /* 384 bytes is accepted, 385 bytes is not. */
    static char padded[386];
    const char *body = "{\"params\":{\"lc_aux\":[\"b\"]}}";
    memset(padded, ' ', sizeof(padded) - 1);
    padded[sizeof(padded) - 1] = '\0';
    memcpy(padded, body, strlen(body));
    TEST_ASSERT_EQUAL(LC_STATUS_SUCCESS, __lc_get_filtered(false, padded, 384, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("{\"lc_aux\":{\"b\":2}}", out);
    TEST_ASSERT_EQUAL(LC_FILTER_INVALID, __lc_get_filtered(false, padded, 385, out, sizeof(out)));

    __lc_expect_filtered("{\"params\":{\"lc_dev\":true}}", "{\"lc_dev\":{\"power\":false}}");

    __lc_node_teardown();
}

#endif /* CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_GET_FILTER */

#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG

#define LC_STATUS_INVALID   RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__InvalidParam
#define LC_SET_FRAG_MAX_LEN CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG_MAX_LEN

/** Send one CmdSetData fragment to set_params and unpack the RespSetData, or NULL if none. */
static RmakerLocalCtrl__RMakerLocalCtrlPayload *__lc_set_frag(uint32_t session_id, uint32_t offset, uint32_t total,
        const char *data, size_t len, esp_rmaker_error_t *handler_err)
{
    RmakerLocalCtrl__PayloadBuf buf = RMAKER_LOCAL_CTRL__PAYLOAD_BUF__INIT;
    buf.offset = offset;
    buf.totallen = total;
    buf.payload.data = (uint8_t *) data;
    buf.payload.len = len;
    RmakerLocalCtrl__CmdSetData cmd = RMAKER_LOCAL_CTRL__CMD_SET_DATA__INIT;
    cmd.buf = &buf;
    RmakerLocalCtrl__RMakerLocalCtrlPayload payload = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__INIT;
    payload.msg = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_MSG_TYPE__TypeCmdSetData;
    payload.payload_case = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__PAYLOAD_CMD_SET_DATA;
    payload.cmdsetdata = &cmd;

    size_t req_len = rmaker_local_ctrl__rmaker_local_ctrl_payload__get_packed_size(&payload);
    uint8_t *req = malloc(req_len);
    TEST_ASSERT_NOT_NULL(req);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__pack(&payload, req);

    uint8_t *resp = NULL;
    ssize_t resp_len = 0;
    esp_rmaker_error_t err = esp_rmaker_local_ctrl_set_params_ep_handler(session_id, req, (ssize_t) req_len,
                             &resp, &resp_len, NULL);
    free(req);
    if (handler_err) {
        *handler_err = err;
    }
    if (resp == NULL || resp_len <= 0) {
        free(resp);
        return NULL;
    }
    RmakerLocalCtrl__RMakerLocalCtrlPayload *unpacked =
        rmaker_local_ctrl__rmaker_local_ctrl_payload__unpack(NULL, (size_t) resp_len, resp);
    free(resp);
    TEST_ASSERT_NOT_NULL(unpacked);
    TEST_ASSERT_EQUAL(RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__PAYLOAD_RESP_SET_DATA, unpacked->payload_case);
    TEST_ASSERT_NOT_NULL(unpacked->respsetdata);
    return unpacked;
}

/** Send one fragment and check the RespSetData status and Received count. */
static void __lc_set_frag_expect(uint32_t session_id, uint32_t offset, uint32_t total, const char *data,
                                 size_t len, RmakerLocalCtrl__RMakerLocalCtrlStatus status, uint32_t received)
{
    esp_rmaker_error_t err = ESP_RMAKER_FAIL;
    RmakerLocalCtrl__RMakerLocalCtrlPayload *resp = __lc_set_frag(session_id, offset, total, data, len, &err);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL(status, resp->respsetdata->status);
    TEST_ASSERT_EQUAL_UINT32(received, resp->respsetdata->received);
    TEST_ASSERT_EQUAL_UINT32(LC_SET_FRAG_MAX_LEN, resp->respsetdata->maxlen);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(resp, NULL);
}

/** Build a set-params JSON body of exactly `len` bytes; padding goes before the first key. */
static char *__lc_padded_json(const char *tail, size_t len)
{
    size_t tail_len = strlen(tail);
    TEST_ASSERT_GREATER_THAN(tail_len + 1, len);
    char *json = malloc(len + 1);
    TEST_ASSERT_NOT_NULL(json);
    json[0] = '{';
    memset(json + 1, ' ', len - 1 - tail_len);
    memcpy(json + len - tail_len, tail, tail_len + 1);
    return json;
}

/** A body over several fragments is stored in order and applied only by the last fragment. */
void test_local_ctrl_set_frag_applies_json(void)
{
    __lc_node_setup();

    const size_t total = 2 * TEST_LC_FRAGMENT_SIZE + 50;
    char *json = __lc_padded_json("\"lc_dev\":{\"power\":true}}", total);

    __lc_set_frag_expect(1, 0, total, json, TEST_LC_FRAGMENT_SIZE, LC_STATUS_SUCCESS, TEST_LC_FRAGMENT_SIZE);
    __lc_set_frag_expect(1, TEST_LC_FRAGMENT_SIZE, total, json + TEST_LC_FRAGMENT_SIZE, TEST_LC_FRAGMENT_SIZE,
                         LC_STATUS_SUCCESS, 2 * TEST_LC_FRAGMENT_SIZE);
    TEST_ASSERT_EQUAL(0, s_lc_writes);
    __lc_set_frag_expect(1, 2 * TEST_LC_FRAGMENT_SIZE, total, json + 2 * TEST_LC_FRAGMENT_SIZE, 50,
                         LC_STATUS_SUCCESS, total);
    TEST_ASSERT_EQUAL(1, s_lc_writes);
    TEST_ASSERT_TRUE(s_lc_power);

    /* The transfer is complete, so a continuation has nothing to continue. */
    __lc_set_frag_expect(1, TEST_LC_FRAGMENT_SIZE, total, json + TEST_LC_FRAGMENT_SIZE, TEST_LC_FRAGMENT_SIZE,
                         LC_STATUS_INVALID, 0);
    free(json);

    __lc_node_teardown();
}

/** Each bad fragment fails with Received 0 and discards the transfer, so the valid next fragment fails too. */
void test_local_ctrl_set_frag_bad_fragment_discards_transfer(void)
{
    enum { BAD_OFFSET, BAD_TOTAL, BAD_SESSION, BAD_LEN, BAD_MAX_LEN, BAD_FREED, BAD_CASES };

    __lc_node_setup();
    const size_t total = 3 * TEST_LC_FRAGMENT_SIZE;
    char *json = __lc_padded_json("\"lc_dev\":{\"power\":true}}", total);
    static char big[TEST_LC_FRAGMENT_SIZE + 1];
    memset(big, ' ', sizeof(big));

    for (int c = 0; c < BAD_CASES; c++) {
        __lc_set_frag_expect(1, 0, total, json, TEST_LC_FRAGMENT_SIZE, LC_STATUS_SUCCESS, TEST_LC_FRAGMENT_SIZE);

        const char *rest = json + TEST_LC_FRAGMENT_SIZE;
        switch (c) {
        case BAD_OFFSET:
            __lc_set_frag_expect(1, TEST_LC_FRAGMENT_SIZE + 1, total, rest, 10, LC_STATUS_INVALID, 0);
            break;
        case BAD_TOTAL:
            __lc_set_frag_expect(1, TEST_LC_FRAGMENT_SIZE, total + 1, rest, 10, LC_STATUS_INVALID, 0);
            break;
        case BAD_SESSION:
            __lc_set_frag_expect(2, TEST_LC_FRAGMENT_SIZE, total, rest, 10, LC_STATUS_INVALID, 0);
            break;
        case BAD_LEN:
            __lc_set_frag_expect(1, TEST_LC_FRAGMENT_SIZE, total, big, sizeof(big), LC_STATUS_INVALID, 0);
            break;
        case BAD_MAX_LEN:
            __lc_set_frag_expect(1, TEST_LC_FRAGMENT_SIZE, LC_SET_FRAG_MAX_LEN + 1, rest, 10, LC_STATUS_INVALID, 0);
            break;
        case BAD_FREED:
            esp_rmaker_local_ctrl_endpoints_free_data();
            break;
        default:
            TEST_FAIL();
        }
        __lc_set_frag_expect(1, TEST_LC_FRAGMENT_SIZE, total, rest, TEST_LC_FRAGMENT_SIZE, LC_STATUS_INVALID, 0);
    }
    TEST_ASSERT_EQUAL(0, s_lc_writes);
    free(json);

    __lc_node_teardown();
}

/** Closing another session keeps the transfer; closing the session that owns it frees it. */
void test_local_ctrl_set_frag_session_close_frees_transfer(void)
{
    __lc_node_setup();

    const size_t total = 3 * TEST_LC_FRAGMENT_SIZE;
    char *json = __lc_padded_json("\"lc_dev\":{\"power\":true}}", total);
    const char *rest = json + TEST_LC_FRAGMENT_SIZE;

    __lc_set_frag_expect(1, 0, total, json, TEST_LC_FRAGMENT_SIZE, LC_STATUS_SUCCESS, TEST_LC_FRAGMENT_SIZE);
    esp_rmaker_local_ctrl_endpoints_session_closed(2);
    __lc_set_frag_expect(1, TEST_LC_FRAGMENT_SIZE, total, rest, TEST_LC_FRAGMENT_SIZE, LC_STATUS_SUCCESS,
                         2 * TEST_LC_FRAGMENT_SIZE);

    esp_rmaker_local_ctrl_endpoints_session_closed(1);
    __lc_set_frag_expect(1, 2 * TEST_LC_FRAGMENT_SIZE, total, json + 2 * TEST_LC_FRAGMENT_SIZE,
                         TEST_LC_FRAGMENT_SIZE, LC_STATUS_INVALID, 0);
    TEST_ASSERT_EQUAL(0, s_lc_writes);
    free(json);

    __lc_node_teardown();
}

/** A complete body that the data model refuses answers Fail with Received 0. */
void test_local_ctrl_set_frag_bad_json_fails(void)
{
    __lc_node_setup();

    const char *bad = "{ this is not json";
    __lc_set_frag_expect(1, 0, strlen(bad), bad, strlen(bad), RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__Fail, 0);

    __lc_node_teardown();
}

/** A body that does not unpack gets a JSON failure; a message that is not CmdSetData gets InvalidParam. */
void test_local_ctrl_set_frag_malformed_request(void)
{
    __lc_node_setup();

    const char *invalid = "{\"status\":\"fail\",\"description\":\"invalid request\"}";
    const uint8_t garbage[] = { 0xff, 0xff, 0xff, 0xff, 0x7f, 0x41, 0x42 };
    uint8_t *resp = NULL;
    ssize_t resp_len = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_set_params_ep_handler(
                          1, garbage, (ssize_t) sizeof(garbage), &resp, &resp_len, NULL));
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL((ssize_t) strlen(invalid), resp_len);
    TEST_ASSERT_EQUAL_MEMORY(invalid, resp, strlen(invalid));
    free(resp);
    resp = NULL;

    uint8_t *req = NULL;
    size_t req_len = 0;
    __lc_pack_cmd(LC_TYPE_PARAMS, 0, &req, &req_len);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_set_params_ep_handler(
                          1, req, (ssize_t) req_len, &resp, &resp_len, NULL));
    free(req);
    TEST_ASSERT_NOT_NULL(resp);
    RmakerLocalCtrl__RMakerLocalCtrlPayload *unpacked =
        rmaker_local_ctrl__rmaker_local_ctrl_payload__unpack(NULL, (size_t) resp_len, resp);
    free(resp);
    TEST_ASSERT_NOT_NULL(unpacked);
    TEST_ASSERT_NOT_NULL(unpacked->respsetdata);
    TEST_ASSERT_EQUAL(LC_STATUS_INVALID, unpacked->respsetdata->status);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(unpacked, NULL);

    __lc_node_teardown();
}

#endif /* CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG */
