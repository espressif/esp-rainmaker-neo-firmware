/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Includes **********************************************************************/

/* Declarations */
#include "esp_rmaker_credentials.h"
#include "credentials/factory_part.h"

/* Standard includes */
#include <stdlib.h>
#include <string.h>

/* Platform includes */
#include "osal_storage.h"

/* Test includes */
#include "test_rmng_common_certs.h"

/* Unity includes */
#include "unity.h"

/* Constants **********************************************************************/

/* Throwaway self-signed P-256 keypair, generated solely for this test. The CN says so:
 * "rmng-unit-test-do-not-use". It is not a credential for anything -- nothing here opens a
 * connection with it, and the assertions below only compare the bytes back out of the
 * credentials layer, so any well-formed PEM would do. Present as literals rather than as
 * loaded files to keep the test self-contained.
 *
 * If a secret scanner flags the PRIVATE KEY block: it is inert test material, safe to
 * dismiss. Do not reuse it anywhere, and do not treat it as a template for real credentials
 * -- real ones come from the factory NVS partition (see esp_rmaker_credentials.h). */
#define TEST_OVERRIDE_DUMMY_CERT TEST_CERT_CN
#define TEST_OVERRIDE_CLIENT_KEY "-----BEGIN PRIVATE KEY-----\nMIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgKek1auJXbiXKOynT\n5Kao9Gp5yjKv8NQhakCoB6YEowahRANCAAR5xBe+W7t+cKHuRxvTQKsHh1wpKnOJ\nAsGJgjBOL88XuqukJJt4h0Y5O/3VL26y0XMEO1hHOvTRoKceD3foDpO8\n-----END PRIVATE KEY-----\n"

/* Private functions **************************************************/

static esp_rmaker_error_t __test_credentials_override_mqtt_host(char **p_str)
{
    *p_str = strdup("test_mqtt_host");
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __test_credentials_override_client_cert(esp_rmaker_credential_t *p_credential)
{
    p_credential->credential = (uint8_t *)strdup(TEST_OVERRIDE_DUMMY_CERT);
    p_credential->len = sizeof(TEST_OVERRIDE_DUMMY_CERT);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __test_credentials_override_client_key(esp_rmaker_credential_t *p_credential)
{
    p_credential->credential = (uint8_t *)strdup(TEST_OVERRIDE_CLIENT_KEY);
    p_credential->len = sizeof(TEST_OVERRIDE_CLIENT_KEY);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __test_credentials_override_client_id(char **p_str)
{
    *p_str = strdup("test_client_id");
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __test_credentials_override_client_username(char **p_str)
{
    *p_str = strdup("test_client_username");
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __test_credentials_override_client_password(char **p_str)
{
    *p_str = strdup("test_client_password");
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __test_credentials_override_random(esp_rmaker_credential_t *p_credential)
{
    p_credential->credential = (uint8_t *)strdup("test_random");
    p_credential->len = strlen("test_random");
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __test_credentials_override_codesign_cert(esp_rmaker_credential_t *p_credential)
{
    p_credential->credential = (uint8_t *)strdup(TEST_OVERRIDE_DUMMY_CERT);
    p_credential->len = sizeof(TEST_OVERRIDE_DUMMY_CERT);
    return ESP_RMAKER_OK;
}

static esp_rmaker_credentials_providers_t __test_credentials_providers = {
    .mqtt_host = __test_credentials_override_mqtt_host,
    .client_cert = __test_credentials_override_client_cert,
    .client_key = __test_credentials_override_client_key,
    .client_id = __test_credentials_override_client_id,
    .client_username = __test_credentials_override_client_username,
    .client_password = __test_credentials_override_client_password,
    .random = __test_credentials_override_random,
    .codesign_cert = __test_credentials_override_codesign_cert,
};

/**
 * @brief A factory partition key and the value that the test found there.
 */
typedef struct {
    const char *key;
    uint8_t *data;
    size_t len;
} __test_factory_entry_t;

static void __test_factory_write(const char *key, const void *data, size_t len)
{
    if (data != NULL) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_nvs_update_binary(RMAKER_NVS_FACTORY_PART_NAME, RMAKER_NVS_FACTORY_NAMESPACE, key, data, len));
        return;
    }
    osal_storage_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_load_nvs_handle(RMAKER_NVS_FACTORY_PART_NAME, RMAKER_NVS_FACTORY_NAMESPACE, &handle));
    osal_err_t err = osal_storage_erase(handle, key);
    TEST_ASSERT_TRUE(err == OSAL_ERR_OK || err == OSAL_ERR_NVS_KEY_NOT_FOUND);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_commit(handle));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_close(handle));
}

/**
 * @brief Call esp_rmaker_factory_part_get_client_id() with the given node ID and certificate in the factory partition.
 * @note NULL means the key is absent. The original factory partition is restored before the return.
 */
static esp_rmaker_error_t __test_factory_get_client_id(const char *node_id, const char *cert, char **p_client_id)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_factory_part_init());
    __test_factory_entry_t saved[] = {
        { .key = RMAKER_NVS_FACTORY_KEY_CLIENT_ID },
        { .key = RMAKER_NVS_FACTORY_KEY_CLIENT_CERT },
    };
    for (size_t i = 0; i < sizeof(saved) / sizeof(saved[0]); i++) {
        saved[i].data = esp_rmaker_nvs_get_binary(RMAKER_NVS_FACTORY_PART_NAME, RMAKER_NVS_FACTORY_NAMESPACE, saved[i].key, &saved[i].len);
    }

    __test_factory_write(RMAKER_NVS_FACTORY_KEY_CLIENT_ID, node_id, node_id ? strlen(node_id) : 0);
    __test_factory_write(RMAKER_NVS_FACTORY_KEY_CLIENT_CERT, cert, cert ? strlen(cert) : 0);
    /* The open factory handle can miss writes from other handles, so reopen it. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_factory_part_deinit());
    *p_client_id = NULL;
    esp_rmaker_error_t err = esp_rmaker_factory_part_get_client_id(p_client_id);

    for (size_t i = 0; i < sizeof(saved) / sizeof(saved[0]); i++) {
        __test_factory_write(saved[i].key, saved[i].data, saved[i].len);
        free(saved[i].data);
    }
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_factory_part_deinit());
    return err;
}

/* Public function definitions ****************************************************/

void test_credentials_factory_client_id_no_cn_fallback(void)
{
    char *client_id = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, __test_factory_get_client_id(NULL, TEST_OVERRIDE_DUMMY_CERT, &client_id));
    TEST_ASSERT_NULL(client_id);
}

void test_credentials_factory_client_id_node_id(void)
{
    char *client_id = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __test_factory_get_client_id("test_node_id", TEST_OVERRIDE_DUMMY_CERT, &client_id));
    TEST_ASSERT_EQUAL_STRING("test_node_id", client_id);
    free(client_id);
}

void test_credentials_factory_client_id_not_found(void)
{
    char *client_id = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, __test_factory_get_client_id(NULL, NULL, &client_id));
    TEST_ASSERT_NULL(client_id);
}

void test_credentials_override(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_override(&__test_credentials_providers));

    /* Get the MQTT connection parameters */
    osal_mqtt_conn_params_t *mqtt_conn_params;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_get_mqtt_conn_params(&mqtt_conn_params));
    TEST_ASSERT_NOT_NULL(mqtt_conn_params);
    TEST_ASSERT_EQUAL_STRING("test_mqtt_host", mqtt_conn_params->hostname);
    TEST_ASSERT_EQUAL_STRING("test_client_id", mqtt_conn_params->client_id);
    TEST_ASSERT_EQUAL_MEMORY(TEST_OVERRIDE_DUMMY_CERT, mqtt_conn_params->client_cert, mqtt_conn_params->client_cert_len);
    TEST_ASSERT_EQUAL_MEMORY(TEST_OVERRIDE_CLIENT_KEY, mqtt_conn_params->client_key, mqtt_conn_params->client_key_len);
    TEST_ASSERT_EQUAL_STRING("test_client_username", mqtt_conn_params->username);
    TEST_ASSERT_EQUAL_STRING("test_client_password", mqtt_conn_params->password);

    /* Free the MQTT connection parameters */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_free_mqtt_conn_params(mqtt_conn_params));

    /* Get random value */
    esp_rmaker_credential_t random;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_get_random(&random));
    TEST_ASSERT_NOT_NULL(random.credential);
    TEST_ASSERT_EQUAL_MEMORY("test_random", random.credential, random.len);

    /* Free the random value */
    esp_rmaker_credentials_free_credential(&random);

    /* Get codesign certificate */
    esp_rmaker_credential_t codesign_cert;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_get_codesign_cert(&codesign_cert));
    TEST_ASSERT_NOT_NULL(codesign_cert.credential);
    TEST_ASSERT_EQUAL_MEMORY(TEST_OVERRIDE_DUMMY_CERT, codesign_cert.credential, codesign_cert.len);

    /* Free the codesign certificate */
    esp_rmaker_credentials_free_credential(&codesign_cert);
}
