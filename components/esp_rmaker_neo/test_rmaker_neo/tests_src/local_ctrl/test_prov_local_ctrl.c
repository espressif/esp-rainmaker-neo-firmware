/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_prov_local_ctrl.c
 * @brief Local control endpoints on the provisioning service.
 *
 * The handlers themselves are covered by test_local_ctrl_endpoints.c. These cover the
 * attachment: what the registry advertises, which endpoints it holds, and that the end of a
 * provisioning session frees a half-served get-data cache.
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <stdlib.h>
#include <string.h>

#include "protobuf-c/protobuf-c.h"
#include "local_ctrl/pb-c.h"
#include "local_ctrl/endpoints.h"
#include "local_ctrl/prov.h"
#include "prov_helpers_internal.h"

#include "esp_rmaker_core.h"
#include "esp_rmaker_data_model.h"
#include "esp_rmaker_flow.h"
#include "esp_rmaker_val.h"
#include "esp_rmaker_state.h"
#include "network/state_changes.h"
#include "osal_event_loop.h"

#define TEST_MAX_CAPABILITIES (PROV_MAX_CAPABILITIES + 1)

static esp_rmaker_node_t *s_node = NULL;

/* Helpers *******************************************************/

/** Bring up a node whose params JSON spans several fragments. */
static void __node_setup(void)
{
    osal_event_loop_create_default();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_state_init());

    esp_rmaker_config_t cfg = { .enable_time_sync = false };
    s_node = esp_rmaker_node_init(&cfg, "prov-lc-node", "prov-lc-type");
    TEST_ASSERT_NOT_NULL(s_node);

    esp_rmaker_device_t *dev = esp_rmaker_device_create("prov_lc_dev", "test_type", NULL);
    TEST_ASSERT_NOT_NULL(dev);

    /* Longer than one 200-byte fragment on its own. */
    static char long_text[2 * 200 + 1];
    memset(long_text, 'x', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';
    esp_rmaker_param_t *text = esp_rmaker_param_create("text", "test_text", esp_rmaker_str(long_text),
                               PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, text));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(s_node, dev));
}

static void __node_teardown(void)
{
    esp_rmaker_local_ctrl_endpoints_free_data();
    if (s_node) {
        esp_rmaker_node_deinit(s_node);
        s_node = NULL;
    }
    (void) esp_rmaker_state_deinit();
    osal_event_loop_delete_default();
}

static const prov_endpoint_t *__endpoint_find(const char *name)
{
    for (size_t i = 0; i < PROV_MAX_ENDPOINTS; i++) {
        const prov_endpoint_t *endpoint = __prov_endpoint_get(i);
        if (endpoint != NULL && strcmp(endpoint->name, name) == 0) {
            return endpoint;
        }
    }
    return NULL;
}

/** Drive one get_params request through the registered endpoint and unpack the reply. */
static RmakerLocalCtrl__RMakerLocalCtrlPayload *__get_params(const prov_endpoint_t *endpoint, uint32_t offset)
{
    RmakerLocalCtrl__CmdGetData cmd = RMAKER_LOCAL_CTRL__CMD_GET_DATA__INIT;
    cmd.datatype = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_DATA_TYPE__TypeParams;
    cmd.offset = offset;

    RmakerLocalCtrl__RMakerLocalCtrlPayload payload = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__INIT;
    payload.msg = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_MSG_TYPE__TypeCmdGetData;
    payload.payload_case = RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_PAYLOAD__PAYLOAD_CMD_GET_DATA;
    payload.cmdgetdata = &cmd;

    size_t req_len = rmaker_local_ctrl__rmaker_local_ctrl_payload__get_packed_size(&payload);
    uint8_t *req = malloc(req_len);
    TEST_ASSERT_NOT_NULL(req);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__pack(&payload, req);

    uint8_t *resp = NULL;
    ssize_t resp_len = 0;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, endpoint->handler(1, req, (ssize_t) req_len, &resp, &resp_len,
                      endpoint->priv_data));
    free(req);
    TEST_ASSERT_NOT_NULL(resp);

    RmakerLocalCtrl__RMakerLocalCtrlPayload *unpacked =
        rmaker_local_ctrl__rmaker_local_ctrl_payload__unpack(NULL, (size_t) resp_len, resp);
    free(resp);
    TEST_ASSERT_NOT_NULL(unpacked);
    TEST_ASSERT_NOT_NULL(unpacked->respgetdata);
    return unpacked;
}

/* Tests *******************************************************/

#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG
#define TEST_LC_CAPABILITIES 2
#else
#define TEST_LC_CAPABILITIES 1
#endif

/** Three endpoints, and `local_ctrl` (plus `set_frag` when enabled) under `rmaker_extra`: the same
 * list the local endpoints service advertises, so a client reads it the same way on both transports. */
void test_prov_local_ctrl_advertises_capabilities(void)
{
    osal_event_loop_create_default();

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_prov_local_ctrl_init());

    const char *names[TEST_MAX_CAPABILITIES];
    TEST_ASSERT_EQUAL(TEST_LC_CAPABILITIES,
                      __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));
    TEST_ASSERT_EQUAL_STRING(RMAKER_PROV_LOCAL_CTRL_CAPABILITY, names[0]);
#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG
    TEST_ASSERT_EQUAL_STRING(RMAKER_LOCAL_CTRL_SET_FRAG_CAPABILITY, names[1]);
#endif
    TEST_ASSERT_EQUAL(0, __prov_app_info_collect(PROV_APP_LABEL_RMAKER, names, TEST_MAX_CAPABILITIES));

    /* Registration order is the wire order. */
    TEST_ASSERT_EQUAL_STRING(RMAKER_LOCAL_CTRL_GET_PARAMS_ENDPOINT, __prov_endpoint_get(0)->name);
    TEST_ASSERT_EQUAL_STRING(RMAKER_LOCAL_CTRL_SET_PARAMS_ENDPOINT, __prov_endpoint_get(1)->name);
    TEST_ASSERT_EQUAL_STRING(RMAKER_LOCAL_CTRL_GET_CONFIG_ENDPOINT, __prov_endpoint_get(2)->name);
    TEST_ASSERT_NULL(__prov_endpoint_get(3));

    /* Adding again is a no-op, not a second set. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_prov_local_ctrl_init());
    TEST_ASSERT_NULL(__prov_endpoint_get(3));
    TEST_ASSERT_EQUAL(TEST_LC_CAPABILITIES,
                      __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_prov_local_ctrl_deinit());
    osal_event_loop_delete_default();
}

/** A session can end between two fragments. The registry's prov_end hook must drop the cache,
 * so the next session starts clean and a stale offset is refused. */
void test_prov_local_ctrl_prov_end_frees_cache(void)
{
    __node_setup();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_prov_local_ctrl_init());

    const prov_endpoint_t *get_params = __endpoint_find(RMAKER_LOCAL_CTRL_GET_PARAMS_ENDPOINT);
    TEST_ASSERT_NOT_NULL(get_params);
    TEST_ASSERT_NOT_NULL(get_params->on_prov_end);

    RmakerLocalCtrl__RMakerLocalCtrlPayload *first = __get_params(get_params, 0);
    TEST_ASSERT_EQUAL(RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__Success, first->respgetdata->status);
    TEST_ASSERT_NOT_NULL(first->respgetdata->buf);
    uint32_t next = (uint32_t) first->respgetdata->buf->payload.len;
    uint32_t total = first->respgetdata->buf->totallen;
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(first, NULL);
    TEST_ASSERT_LESS_THAN_UINT32(total, next); /* The node above spans several fragments. */

    get_params->on_prov_end(get_params->priv_data);

    RmakerLocalCtrl__RMakerLocalCtrlPayload *stale = __get_params(get_params, next);
    TEST_ASSERT_EQUAL(RMAKER_LOCAL_CTRL__RMAKER_LOCAL_CTRL_STATUS__Fail, stale->respgetdata->status);
    rmaker_local_ctrl__rmaker_local_ctrl_payload__free_unpacked(stale, NULL);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_prov_local_ctrl_deinit());
    __node_teardown();
}

void test_prov_local_ctrl_deinit_removes_endpoints(void)
{
    osal_event_loop_create_default();

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_prov_local_ctrl_init());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_prov_local_ctrl_deinit());

    const char *names[TEST_MAX_CAPABILITIES];
    TEST_ASSERT_EQUAL(0, __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));
    TEST_ASSERT_NULL(__endpoint_find(RMAKER_LOCAL_CTRL_GET_PARAMS_ENDPOINT));
    TEST_ASSERT_NULL(__endpoint_find(RMAKER_LOCAL_CTRL_SET_PARAMS_ENDPOINT));
    TEST_ASSERT_NULL(__endpoint_find(RMAKER_LOCAL_CTRL_GET_CONFIG_ENDPOINT));

    /* Removing again is a success: the end state is the same. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_prov_local_ctrl_deinit());
    osal_event_loop_delete_default();
}
