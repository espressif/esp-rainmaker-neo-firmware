/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_prov_helpers.c
 *
 * The registry keeps endpoints and capabilities apart: a feature declares its capability once,
 * however many endpoints serve it. Every capability declared under one label merges into a
 * single app info entry, because the provisioning backend appends rather than replaces, so a
 * label that reaches it twice yields a duplicate key and loses capabilities.
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include "prov_helpers_internal.h"

#define TEST_MAX_CAPABILITIES (PROV_MAX_CAPABILITIES + 1)

/** The registry subscribes to the provisioning events, so it needs the default loop. */
static void __loop_create(void)
{
    osal_event_loop_create_default();
}

static void __loop_delete(void)
{
    osal_event_loop_delete_default();
}

static osal_err_t __stub_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                 uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    (void)session_id; (void)inbuf; (void)inlen; (void)outbuf; (void)outlen; (void)priv_data;
    return OSAL_ERR_OK;
}

static const prov_endpoint_t ep_a = { .name = "ep_a", .handler = __stub_handler };
static const prov_endpoint_t ep_b = { .name = "ep_b", .handler = __stub_handler };

static const prov_capability_t cap_extra_a = { .label = PROV_APP_LABEL_RMAKER_EXTRA, .capability = "cap_a" };
static const prov_capability_t cap_extra_b = { .label = PROV_APP_LABEL_RMAKER_EXTRA, .capability = "cap_b" };
static const prov_capability_t cap_rmaker_c = { .label = PROV_APP_LABEL_RMAKER, .capability = "cap_c" };

void test_prov_capabilities_merge_under_one_label(void)
{
    __loop_create();

    const char *names[TEST_MAX_CAPABILITIES];

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&cap_extra_b));

    TEST_ASSERT_EQUAL(2, __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));
    TEST_ASSERT_EQUAL_STRING("cap_a", names[0]);
    TEST_ASSERT_EQUAL_STRING("cap_b", names[1]);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&cap_extra_b));
    __loop_delete();
}

void test_prov_labels_stay_separate(void)
{
    __loop_create();

    const char *names[TEST_MAX_CAPABILITIES];

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&cap_rmaker_c));

    TEST_ASSERT_EQUAL(1, __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));
    TEST_ASSERT_EQUAL_STRING("cap_a", names[0]);

    TEST_ASSERT_EQUAL(1, __prov_app_info_collect(PROV_APP_LABEL_RMAKER, names, TEST_MAX_CAPABILITIES));
    TEST_ASSERT_EQUAL_STRING("cap_c", names[0]);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&cap_rmaker_c));
    __loop_delete();
}

void test_prov_add_rejects_incomplete_endpoints(void)
{
    __loop_create();

    static const prov_endpoint_t no_name = { .handler = __stub_handler };
    static const prov_endpoint_t no_handler = { .name = "ep" };

    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, prov_add_endpoint(NULL));
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, prov_add_endpoint(&no_name));
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, prov_add_endpoint(&no_handler));
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, prov_del_endpoint(NULL));
    __loop_delete();
}

void test_prov_add_rejects_incomplete_capabilities(void)
{
    __loop_create();

    static const prov_capability_t no_capability = { .label = PROV_APP_LABEL_RMAKER };
    static const prov_capability_t no_label = { .capability = "cap" };
    static const prov_capability_t bad_label = { .label = PROV_APP_LABEL_MAX, .capability = "cap" };

    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, prov_add_capability(NULL));
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, prov_add_capability(&no_capability));
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, prov_add_capability(&no_label));
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, prov_add_capability(&bad_label));
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, prov_del_capability(NULL));
    __loop_delete();
}

void test_prov_add_same_endpoint_overrides(void)
{
    __loop_create();

    const char *names[TEST_MAX_CAPABILITIES];

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_endpoint(&ep_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_endpoint(&ep_a));
    TEST_ASSERT_EQUAL_PTR(&ep_a, __prov_endpoint_get(0));
    TEST_ASSERT_NULL(__prov_endpoint_get(1));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(1, __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));

    /* One add, one entry: the second delete finds nothing, which is the state asked for. */
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_endpoint(&ep_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_endpoint(&ep_a));
    TEST_ASSERT_NULL(__prov_endpoint_get(0));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(0, __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));
    __loop_delete();
}

/** Endpoint creation order decides the BLE characteristic UUIDs, and capability order is the
 * app info order. Slots never move, so an entry that is removed and added again lands back in
 * its original position. */
void test_prov_del_preserves_order(void)
{
    __loop_create();

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_endpoint(&ep_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_endpoint(&ep_b));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_endpoint(&ep_a));

    /* The survivor keeps its slot rather than shifting down. */
    TEST_ASSERT_NULL(__prov_endpoint_get(0));
    TEST_ASSERT_EQUAL_PTR(&ep_b, __prov_endpoint_get(1));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_endpoint(&ep_a));
    TEST_ASSERT_EQUAL_PTR(&ep_a, __prov_endpoint_get(0));
    TEST_ASSERT_EQUAL_PTR(&ep_b, __prov_endpoint_get(1));

    const char *names[TEST_MAX_CAPABILITIES];

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&cap_extra_b));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(1, __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));
    TEST_ASSERT_EQUAL_STRING("cap_b", names[0]);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(2, __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));
    TEST_ASSERT_EQUAL_STRING("cap_a", names[0]);
    TEST_ASSERT_EQUAL_STRING("cap_b", names[1]);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_endpoint(&ep_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_endpoint(&ep_b));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&cap_extra_b));
    __loop_delete();
}

void test_prov_add_reports_a_full_registry(void)
{
    __loop_create();

    static prov_endpoint_t endpoints[PROV_MAX_ENDPOINTS];
    for (size_t i = 0; i < PROV_MAX_ENDPOINTS; i++) {
        endpoints[i] = (prov_endpoint_t) {
            .name = "filler", .handler = __stub_handler
        };
        TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_endpoint(&endpoints[i]));
    }
    TEST_ASSERT_EQUAL(OSAL_ERR_NO_MEM, prov_add_endpoint(&ep_a));

    static prov_capability_t capabilities[PROV_MAX_CAPABILITIES];
    for (size_t i = 0; i < PROV_MAX_CAPABILITIES; i++) {
        capabilities[i] = (prov_capability_t) {
            .label = PROV_APP_LABEL_RMAKER_EXTRA, .capability = "filler"
        };
        TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&capabilities[i]));
    }
    TEST_ASSERT_EQUAL(OSAL_ERR_NO_MEM, prov_add_capability(&cap_rmaker_c));

    for (size_t i = 0; i < PROV_MAX_ENDPOINTS; i++) {
        TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_endpoint(&endpoints[i]));
    }
    for (size_t i = 0; i < PROV_MAX_CAPABILITIES; i++) {
        TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&capabilities[i]));
    }
    __loop_delete();
}

/** Removing the last endpoint and capability drops the registry's event registrations, so a
 * later add has to put them back. Either kind of entry keeps them alive on its own. */
void test_prov_readd_after_drain(void)
{
    __loop_create();

    const char *names[TEST_MAX_CAPABILITIES];

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_endpoint(&ep_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_endpoint(&ep_a));
    TEST_ASSERT_EQUAL(1, __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(0, __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_capability(&cap_extra_a));
    TEST_ASSERT_EQUAL(1, __prov_app_info_collect(PROV_APP_LABEL_RMAKER_EXTRA, names, TEST_MAX_CAPABILITIES));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_add_endpoint(&ep_a));
    TEST_ASSERT_EQUAL_PTR(&ep_a, __prov_endpoint_get(0));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_endpoint(&ep_a));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, prov_del_capability(&cap_extra_a));
    __loop_delete();
}
