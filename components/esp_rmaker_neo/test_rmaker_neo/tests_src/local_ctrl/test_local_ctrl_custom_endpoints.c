/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_local_ctrl_custom_endpoints.c
 * @brief Lifecycle of application endpoints on the Local Control service.
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <stdio.h>

#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_services.h"
#include "osal_event_loop.h"

#define TEST_EP_NAME     "test_ep"
#define TEST_EP_MAX      4

static int __test_ep_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf,
                             ssize_t *outlen, void *priv_data)
{
    (void) session_id;
    (void) inbuf;
    (void) inlen;
    (void) priv_data;
    *outbuf = NULL;
    *outlen = 0;
    return 0;
}

static esp_rmaker_node_t *__test_setup(void)
{
    osal_event_loop_create_default();
    esp_rmaker_config_t config = { .enable_time_sync = false };
    return esp_rmaker_node_init(&config, "tnode", "ttype");
}

static void __test_teardown(esp_rmaker_node_t *node)
{
    char name[16];
    for (int i = 0; i <= TEST_EP_MAX; i++) {
        snprintf(name, sizeof(name), TEST_EP_NAME "%d", i);
        (void) esp_rmaker_local_ctrl_remove_endpoint(name);
    }
    (void) esp_rmaker_local_ctrl_remove_endpoint(TEST_EP_NAME);
    (void) esp_rmaker_chal_resp_service_disable();
    (void) esp_rmaker_local_ctrl_service_disable();
    if (node != NULL) {
        esp_rmaker_node_clear_stored_values(node);
        esp_rmaker_node_deinit(node);
    }
    osal_event_loop_delete_default();
}

void test_local_ctrl_custom_endpoint_requires_running_service(void)
{
    esp_rmaker_node_t *node = __test_setup();
    if (TEST_PROTECT()) {
        TEST_ASSERT_NOT_NULL(node);
        TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE,
                          esp_rmaker_local_ctrl_add_endpoint(TEST_EP_NAME, __test_ep_handler, NULL));
        TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_local_ctrl_remove_endpoint(TEST_EP_NAME));
    }
    __test_teardown(node);
}

void test_local_ctrl_custom_endpoint_add_remove(void)
{
    esp_rmaker_node_t *node = __test_setup();
    if (TEST_PROTECT()) {
        TEST_ASSERT_NOT_NULL(node);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_service_enable());

        TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_ctrl_add_endpoint(NULL, __test_ep_handler, NULL));
        TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_ctrl_add_endpoint("", __test_ep_handler, NULL));
        TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_ctrl_add_endpoint(TEST_EP_NAME, NULL, NULL));

        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_add_endpoint(TEST_EP_NAME, __test_ep_handler, NULL));
        TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_local_ctrl_add_endpoint(TEST_EP_NAME, __test_ep_handler, NULL));
        TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_local_ctrl_add_endpoint("get_params", __test_ep_handler, NULL));

        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_remove_endpoint(TEST_EP_NAME));
        TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_local_ctrl_remove_endpoint(TEST_EP_NAME));
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_add_endpoint(TEST_EP_NAME, __test_ep_handler, NULL));
    }
    __test_teardown(node);
}

void test_local_ctrl_custom_endpoint_table_full(void)
{
    esp_rmaker_node_t *node = __test_setup();
    if (TEST_PROTECT()) {
        TEST_ASSERT_NOT_NULL(node);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_service_enable());

        char name[16];
        for (int i = 0; i < TEST_EP_MAX; i++) {
            snprintf(name, sizeof(name), TEST_EP_NAME "%d", i);
            TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_add_endpoint(name, __test_ep_handler, NULL));
        }
        snprintf(name, sizeof(name), TEST_EP_NAME "%d", TEST_EP_MAX);
        TEST_ASSERT_EQUAL(ESP_RMAKER_NO_MEM, esp_rmaker_local_ctrl_add_endpoint(name, __test_ep_handler, NULL));
    }
    __test_teardown(node);
}

void test_local_ctrl_custom_endpoint_removed_on_disable(void)
{
    esp_rmaker_node_t *node = __test_setup();
    if (TEST_PROTECT()) {
        TEST_ASSERT_NOT_NULL(node);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_service_enable());
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_add_endpoint(TEST_EP_NAME, __test_ep_handler, NULL));

        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_service_disable());
        TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_local_ctrl_remove_endpoint(TEST_EP_NAME));
        TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE,
                          esp_rmaker_local_ctrl_add_endpoint(TEST_EP_NAME, __test_ep_handler, NULL));

        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_service_enable());
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_add_endpoint(TEST_EP_NAME, __test_ep_handler, NULL));
    }
    __test_teardown(node);
}

void test_local_ctrl_custom_endpoint_removed_while_chal_resp_runs(void)
{
    esp_rmaker_node_t *node = __test_setup();
    if (TEST_PROTECT()) {
        TEST_ASSERT_NOT_NULL(node);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_chal_resp_service_enable());
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_service_enable());
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_add_endpoint(TEST_EP_NAME, __test_ep_handler, NULL));

        /* The protocomm instance stays up for chal-resp, so a leftover endpoint fails the re-add */
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_service_disable());
        TEST_ASSERT_TRUE(esp_rmaker_chal_resp_service_is_enabled());
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_service_enable());
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_add_endpoint(TEST_EP_NAME, __test_ep_handler, NULL));
    }
    __test_teardown(node);
}
