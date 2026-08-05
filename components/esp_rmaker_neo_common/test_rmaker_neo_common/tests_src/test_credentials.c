/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Includes **********************************************************************/

/* Declarations */
#include "esp_rmaker_credentials.h"

/* Standard includes */
#include <string.h>

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
#define TEST_OVERRIDE_DUMMY_CERT "-----BEGIN CERTIFICATE-----\nMIIBnjCCAUWgAwIBAgIUOTYoQNMLZHsx3iPyN39RYG8oSRswCgYIKoZIzj0EAwIw\nJDEiMCAGA1UEAwwZcm1uZy11bml0LXRlc3QtZG8tbm90LXVzZTAgFw0yNjA3MzEx\nMTQ3NTJaGA8yMTI2MDcwNzExNDc1MlowJDEiMCAGA1UEAwwZcm1uZy11bml0LXRl\nc3QtZG8tbm90LXVzZTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABHnEF75bu35w\noe5HG9NAqweHXCkqc4kCwYmCME4vzxe6q6Qkm3iHRjk7/dUvbrLRcwQ7WEc69NGg\npx4Pd+gOk7yjUzBRMB0GA1UdDgQWBBRBfFfVvr/aFrpwwwMfik3V786EezAfBgNV\nHSMEGDAWgBRBfFfVvr/aFrpwwwMfik3V786EezAPBgNVHRMBAf8EBTADAQH/MAoG\nCCqGSM49BAMCA0cAMEQCIA48t8EJotP4GTyKQ2bav/muY1OYoD8kfYe+w2ko8/4d\nAiA5frtr4uL2TAdJbayY7e9+/9m/rBiwGEHdO9i8Mx+KbA==\n-----END CERTIFICATE-----\n"
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

/* Public function definitions ****************************************************/

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
