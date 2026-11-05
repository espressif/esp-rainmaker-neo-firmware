/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/* The credential-provider endpoint, video role alias and region come from the
 * deployment's server config (getServerConfig cloud event). The temporary
 * credentials come from the rmaker_aws_sign component, on the SDK's osal port,
 * with the node certificate from the RainMaker Neo credentials provider
 * (factory partition). */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"

#include "esp_rmaker_aws_sign.h"
#include "esp_rmaker_aws_sign_port_osal.h"
#include "esp_rmaker_credentials.h"
#include "esp_rmaker_server_config.h"
#include "rmaker_aws_credentials.h"

static const char *TAG = "rmaker_aws_creds";

/* The deployment's values, set once by rmaker_camera_resolve_deployment() and
 * never freed. */
static char *s_cred_endpoint;
static char *s_role_alias;
static char *s_region;

/* Take a value from the deployment's server config. For role aliases, which may
 * list several versions, the last one is the newest. */
static esp_err_t server_config_value(esp_rmaker_server_config_key_t key, bool last_of_list, uint32_t wait_ms,
                                     char **p_value)
{
    char *value = NULL;
    esp_rmaker_error_t err = esp_rmaker_server_config_get(key, &value, wait_ms);
    if (err == ESP_RMAKER_TIMEOUT) {
        return ESP_ERR_TIMEOUT;
    }
    if (err != ESP_RMAKER_OK || !value || !value[0]) {
        free(value);
        return ESP_ERR_NOT_FOUND;
    }
    if (last_of_list) {
        char *last = strrchr(value, ',');
        if (last && last[1]) {
            memmove(value, last + 1, strlen(last + 1) + 1);
        }
    }
    *p_value = value;
    return ESP_OK;
}

esp_err_t rmaker_camera_resolve_deployment(uint32_t wait_ms)
{
    if (s_cred_endpoint) {
        return ESP_OK;
    }
    /* All three come in the same response, so only the first lookup waits. */
    char *endpoint = NULL, *alias = NULL, *region = NULL;
    esp_err_t err = server_config_value(ESP_RMAKER_SERVER_CONFIG_CREDENTIAL_PROVIDER_ENDPOINT, false, wait_ms, &endpoint);
    if (err == ESP_OK) {
        err = server_config_value(ESP_RMAKER_SERVER_CONFIG_NODE_VIDEO_ROLE_ALIASES, true, 0, &alias);
    }
    if (err == ESP_OK) {
        err = server_config_value(ESP_RMAKER_SERVER_CONFIG_STACK_REGION, false, 0, &region);
    }
    if (err != ESP_OK) {
        free(endpoint);
        free(alias);
        free(region);
        return err;
    }
    s_role_alias = alias;
    s_region = region;
    s_cred_endpoint = endpoint;
    ESP_LOGI(TAG, "Credential endpoint: %s, video role alias: %s, region: %s", s_cred_endpoint, s_role_alias, s_region);
    return ESP_OK;
}

const char *rmaker_camera_get_role_alias(void)
{
    return s_role_alias;
}

const char *rmaker_camera_get_aws_region(void)
{
    return s_region;
}

/* The signer holds the node identity for the IoT Credential Provider: the
 * credential host, the thing name and the client certificate. s_creds holds
 * the credentials of the video role alias. Both are used by one fetch at a
 * time. */
static esp_rmaker_aws_sign_handle_t s_signer;
static esp_rmaker_aws_sign_creds_t s_creds;

/* The credentials provider hands out the node cert/key as either NUL-terminated
 * PEM or DER. rmaker_aws_sign treats a zero length as NUL-terminated PEM, and a
 * non-zero one as the exact buffer size (required for DER). */
static size_t pem_or_der_len(const uint8_t *buf, size_t len)
{
    return (len >= 10 && strncmp((const char *)buf, "-----BEGIN", 10) == 0) ? 0 : len;
}

/* Another library, such as node file storage, may have installed the port
 * first. The signer then uses that one. */
static esp_err_t install_port(void)
{
    if (esp_rmaker_aws_sign_port_is_installed()) {
        return ESP_OK;
    }
    const esp_rmaker_aws_sign_port_config_t *port = esp_rmaker_aws_sign_port_osal_get();
    return port ? esp_rmaker_aws_sign_port_install(port) : ESP_FAIL;
}

static esp_err_t create_signer(void)
{
    esp_err_t err = install_port();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install the AWS signing port: %s", esp_err_to_name(err));
        return err;
    }

    esp_rmaker_credential_t cert = {0};
    esp_rmaker_credential_t key = {0};
    char *thing_name = NULL;
    err = ESP_FAIL;
    if (esp_rmaker_credentials_get_client_cert(&cert) != ESP_RMAKER_OK ||
            esp_rmaker_credentials_get_private_key(&key) != ESP_RMAKER_OK ||
            esp_rmaker_credentials_get_thing_name(&thing_name) != ESP_RMAKER_OK) {
        ESP_LOGE(TAG, "Failed to get the node certificate, key or thing name");
        goto out;
    }

    const esp_rmaker_aws_sign_config_t cfg = {
        .cred_host = s_cred_endpoint,
        .thing_name = thing_name,
        .client_cert = (const char *)cert.credential,
        .client_cert_len = pem_or_der_len(cert.credential, cert.len),
        .client_key = (const char *)key.credential,
        .client_key_len = pem_or_der_len(key.credential, key.len),
        .region = s_region,
    };
    err = esp_rmaker_aws_sign_create(&cfg, &s_signer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the AWS signer: %s", esp_err_to_name(err));
        goto out;
    }

out:
    esp_rmaker_credentials_free_credential(&cert);
    esp_rmaker_credentials_free_credential(&key);
    free(thing_name);
    return err;
}

esp_err_t rmaker_camera_refresh_credentials(const esp_rmaker_aws_sign_creds_t **creds)
{
    if (!creds) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_cred_endpoint) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_signer) {
        esp_err_t err = create_signer();
        if (err != ESP_OK) {
            return err;
        }
    }

    /* Fetches only when the credentials are missing or near expiry. */
    esp_err_t err = esp_rmaker_aws_sign_creds_refresh(s_signer, s_role_alias, &s_creds);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AWS credentials: %s", esp_err_to_name(err));
        return err;
    }
    *creds = &s_creds;
    return ESP_OK;
}
