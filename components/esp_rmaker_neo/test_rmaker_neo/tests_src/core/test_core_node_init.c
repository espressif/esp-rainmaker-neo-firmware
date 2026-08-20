/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_core_node_init.c
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <string.h>

#include "esp_rmaker_core.h"
#include "esp_rmaker_credentials.h"
#include "chal_resp/impl.h"
#include "prov_helpers_internal.h"

/* Credential providers of an unclaimed node: nothing has been written yet. */
static esp_rmaker_error_t __missing_credential(esp_rmaker_credential_t *p_credential)
{
    (void)p_credential;
    return ESP_RMAKER_NOT_FOUND;
}

static esp_rmaker_error_t __missing_string(char **p_str)
{
    (void)p_str;
    return ESP_RMAKER_NOT_FOUND;
}

static int s_mqtt_host_reads;

static esp_rmaker_error_t __counting_mqtt_host(char **p_str)
{
    s_mqtt_host_reads++;
    *p_str = strdup("mqtt.invalid");
    return *p_str ? ESP_RMAKER_OK : ESP_RMAKER_NO_MEM;
}

void test_core_node_init_unclaimed_succeeds(void)
{
    const esp_rmaker_credentials_providers_t unclaimed = {
        .mqtt_host = __missing_string,
        .client_cert = __missing_credential,
        .client_key = __missing_credential,
        .client_id = __missing_string,
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_override(&unclaimed));

    esp_rmaker_config_t config = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "test", "type");
    TEST_ASSERT_NOT_NULL_MESSAGE(node, "node init must succeed without cloud credentials");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_deinit(node));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_reset_to_default());
}

static bool __prov_registry_has(const char *name)
{
    for (size_t slot = 0; slot < PROV_MAX_ENDPOINTS; slot++) {
        const prov_endpoint_t *endpoint = __prov_endpoint_get(slot);
        if (endpoint && strcmp(endpoint->name, name) == 0) {
            return true;
        }
    }
    return false;
}

void test_core_node_init_registers_chal_resp(void)
{
    esp_rmaker_config_t config = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "test", "type");
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_TRUE_MESSAGE(__prov_registry_has(RMAKER_CHAL_RESP_ENDPOINT_NAME), "ch_resp endpoint not registered by node init");

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_deinit(node));
    TEST_ASSERT_FALSE_MESSAGE(__prov_registry_has(RMAKER_CHAL_RESP_ENDPOINT_NAME), "ch_resp endpoint still registered after node deinit");
}

void test_core_node_init_defers_mqtt_init(void)
{
    const esp_rmaker_credentials_providers_t counting = {
        .mqtt_host = __counting_mqtt_host,
    };
    s_mqtt_host_reads = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_override(&counting));

    esp_rmaker_config_t config = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "test", "type");
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_MESSAGE(0, s_mqtt_host_reads, "node init must not read the MQTT connection parameters");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_deinit(node));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_reset_to_default());
}
