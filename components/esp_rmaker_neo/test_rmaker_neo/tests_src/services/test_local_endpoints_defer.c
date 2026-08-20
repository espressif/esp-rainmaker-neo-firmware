/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_local_endpoints_defer.c
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <stdlib.h>
#include <string.h>

#include "esp_rmaker_core.h"
#include "node_internal.h"
#include "services/local_endpoints.h"

static bool __node_config_has_local_ctrl(void)
{
    char *config = esp_rmaker_get_node_config();
    TEST_ASSERT_NOT_NULL(config);
    bool found = strstr(config, ESP_RMAKER_SERVICE_LOCAL_CONTROL) != NULL;
    free(config);
    return found;
}

static void __test_teardown(esp_rmaker_node_t *node)
{
    (void) esp_rmaker_chal_resp_service_disable();
    (void) esp_rmaker_local_ctrl_service_disable();
    if (node != NULL) {
        (void) esp_rmaker_node_deinit(node);
    }
}

void test_local_endpoints_enable_before_start_defers(void)
{
    esp_rmaker_config_t config = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "test", "type");
    if (TEST_PROTECT()) {
        TEST_ASSERT_NOT_NULL(node);

        /* Before start: the request is recorded, nothing is started or added to the node. */
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_service_enable());
        TEST_ASSERT_FALSE(__node_config_has_local_ctrl());
        TEST_ASSERT_FALSE(esp_rmaker_chal_resp_service_is_enabled());
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_chal_resp_service_enable());
        TEST_ASSERT_TRUE(esp_rmaker_chal_resp_service_is_enabled());

        /* A disable before start only drops the request. */
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_chal_resp_service_disable());
        TEST_ASSERT_FALSE(esp_rmaker_chal_resp_service_is_enabled());

        /* The start task hook starts what is still requested. */
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_endpoints_on_start());
        TEST_ASSERT_TRUE(__node_config_has_local_ctrl());

        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_service_disable());
        TEST_ASSERT_FALSE(__node_config_has_local_ctrl());
    }
    __test_teardown(node);
}
