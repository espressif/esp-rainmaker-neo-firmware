/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_rest.c
 * @brief Certificate-signed REST calls to the group data APIs.
 *
 * Every call is signed with SigV4 credentials that the IoT Credential Provider issues for the device
 * certificate under the controller role alias. The cloud identifies the controller from them alone.
 * The API base URL and the credential-provider endpoint come from the cloud's server config, so the
 * signer is created on the first call, once that has arrived.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osal_log.h"
#include "osal_semaphore.h"

#include "esp_rmaker_credentials.h"
#include "esp_rmaker_node.h"
#include "esp_rmaker_server_config.h"

#include "esp_rmaker_aws_sign.h"
#include "esp_rmaker_aws_sign_port_osal.h"

#include "controller_internal.h"
#include "sdkconfig.h"

#define CONTROLLER_ROLE_ALIAS        CONFIG_ESP_RMAKER_NEO_CONTROLLER_ROLE_ALIAS
#define CONTROLLER_SERVER_CONFIG_MS  CONFIG_ESP_RMAKER_NEO_CONTROLLER_SERVER_CONFIG_TIMEOUT_MS
#define CONTROLLER_PATH_MAX_LEN      256

static const char *TAG = "rmng_controller";

/* Created on the first call; the lock also serializes the shared credentials. */
static osal_semaphore_handle_t s_lock;
static esp_rmaker_aws_sign_handle_t s_signer;
static esp_rmaker_aws_sign_creds_t s_creds;
static char s_host[CONTROLLER_HOST_MAX_LEN];
static char s_base_path[CONTROLLER_BASE_PATH_MAX_LEN];

/* --- Endpoints and signer ------------------------------------------------------------------ */

static esp_rmaker_error_t server_config_value(esp_rmaker_server_config_key_t key, const char *what, char **out)
{
    esp_rmaker_error_t err = esp_rmaker_server_config_get(key, out, CONTROLLER_SERVER_CONFIG_MS);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "No %s in the server config: %s", what, osal_err_strerror(err));
    }
    return err;
}

/* The signer holds the device identity for the IoT Credential Provider: the credential host, the
 * thing name and the client certificate it authenticates with. */
static esp_rmaker_error_t create_signer(const char *cred_host)
{
    if (!esp_rmaker_aws_sign_port_is_installed()) {
        const esp_rmaker_aws_sign_port_config_t *port = esp_rmaker_aws_sign_port_osal_get();
        if (!port || esp_rmaker_aws_sign_port_install(port) != ESP_OK) {
            OSAL_LOGE(TAG, "Failed to install the AWS signing port");
            return ESP_RMAKER_FAIL;
        }
    }

    esp_rmaker_credential_t cert = {0};
    esp_rmaker_credential_t key = {0};
    esp_rmaker_error_t err = esp_rmaker_credentials_get_client_cert(&cert);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get the client certificate: %d", err);
        return err;
    }
    err = esp_rmaker_credentials_get_private_key(&key);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get the private key: %d", err);
        esp_rmaker_credentials_free_credential(&cert);
        return err;
    }

    const esp_rmaker_aws_sign_config_t cfg = {
        .cred_host = cred_host,
        .thing_name = g_controller_state.thing_name,
        .client_cert = (const char *)cert.credential,
        .client_cert_len = cert.len,
        .client_key = (const char *)key.credential,
        .client_key_len = key.len,
    };
    esp_err_t aws_err = esp_rmaker_aws_sign_create(&cfg, &s_signer);
    esp_rmaker_credentials_free_credential(&cert);
    esp_rmaker_credentials_free_credential(&key);
    if (aws_err != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to create the AWS signer: 0x%x", aws_err);
        s_signer = NULL;
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

/* Resolve the API host and the signer from the server config, once. Called with s_lock held. */
static esp_rmaker_error_t ensure_ready(void)
{
    if (s_signer) {
        return ESP_RMAKER_OK;
    }
    char *base_url = NULL;
    char *cred_host = NULL;
    esp_rmaker_error_t err = server_config_value(ESP_RMAKER_SERVER_CONFIG_API_GATEWAY_URL, "API base URL", &base_url);
    if (err == ESP_RMAKER_OK) {
        err = server_config_value(ESP_RMAKER_SERVER_CONFIG_CREDENTIAL_PROVIDER_ENDPOINT,
                                  "credential provider endpoint", &cred_host);
    }
    if (err == ESP_RMAKER_OK) {
        controller_parse_base_url(base_url, s_host, sizeof(s_host), s_base_path, sizeof(s_base_path));
        if (s_host[0] == '\0') {
            OSAL_LOGE(TAG, "API base URL '%s' has no host", base_url);
            err = ESP_RMAKER_INVALID_ARG;
        }
    }
    if (err == ESP_RMAKER_OK) {
        err = create_signer(cred_host);
    }
    if (err == ESP_RMAKER_OK) {
        OSAL_LOGI(TAG, "REST API %s%s, credentials from %s", s_host, s_base_path, cred_host);
    }
    free(base_url);
    free(cred_host);
    return err;
}

/* --- Requests ------------------------------------------------------------------------------ */

static esp_rmaker_error_t send_get(const char *path, esp_rmaker_aws_sign_response_t *resp)
{
    const esp_rmaker_aws_sign_request_t req = {
        .method = "GET",
        .host = s_host,
        .path = path,
        .service = "execute-api",
        .role_alias = CONTROLLER_ROLE_ALIAS,
        .creds = &s_creds,
    };
    esp_err_t err = esp_rmaker_aws_sign_send_request(s_signer, &req, resp);
    if (err != ESP_OK) {
        OSAL_LOGE(TAG, "GET %s failed: 0x%x", path, err);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t controller_rest_status_to_error(const char *path, int status)
{
    if (status / 100 == 2) {
        return ESP_RMAKER_OK;
    }
    OSAL_LOGE(TAG, "GET %s returned HTTP %d", path, status);
    switch (status) {
    case 404:
        return ESP_RMAKER_NOT_FOUND;
    case 401:
    case 403:
        return ESP_RMAKER_INVALID_STATE;
    default:
        return ESP_RMAKER_FAIL;
    }
}

esp_rmaker_error_t controller_rest_init(void)
{
    if (!s_lock) {
        s_lock = osal_semaphore_create_mutex();
    }
    return s_lock ? ESP_RMAKER_OK : ESP_RMAKER_NO_MEM;
}

#if CONFIG_ESP_RMAKER_NEO_TESTING
static controller_test_rest_get_t s_test_rest_get;

void controller_test_set_rest_get(controller_test_rest_get_t fn)
{
    s_test_rest_get = fn;
}
#endif

esp_rmaker_error_t controller_rest_get(const char *path_suffix, esp_rmaker_aws_sign_response_t *resp)
{
    if (!path_suffix || path_suffix[0] != '/' || !resp) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (!g_controller_state.enabled || !s_lock) {
        return ESP_RMAKER_NOT_INITIALIZED;
    }
#if CONFIG_ESP_RMAKER_NEO_TESTING
    if (s_test_rest_get) {
        return s_test_rest_get(path_suffix, resp);
    }
#endif

    if (osal_semaphore_take(s_lock, OSAL_MAX_DELAY) != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    char path[CONTROLLER_PATH_MAX_LEN];
    esp_rmaker_error_t err = ensure_ready();
    if (err == ESP_RMAKER_OK &&
            snprintf(path, sizeof(path), "%s%s", s_base_path, path_suffix) >= (int)sizeof(path)) {
        err = ESP_RMAKER_INVALID_ARG;
    }
    if (err == ESP_RMAKER_OK) {
        err = send_get(path, resp);
    }
    /* The signer refreshes the credentials ahead of their expiry by the device clock, so this is a
     * clock that drifted. Fresh credentials fix it; a replay with the same ones would not. */
    if (err == ESP_RMAKER_OK && resp->status_code == 403 &&
            controller_is_expired_credentials((const char *)resp->body, resp->body_len)) {
        OSAL_LOGW(TAG, "Credentials expired; fetching new ones");
        esp_rmaker_aws_sign_response_free(resp);
        esp_rmaker_aws_sign_creds_free(&s_creds);
        err = send_get(path, resp);
    }
    osal_semaphore_give(s_lock);
    return err;
}

void controller_rest_deinit(void)
{
    esp_rmaker_aws_sign_creds_free(&s_creds);
    if (s_signer) {
        esp_rmaker_aws_sign_destroy(s_signer);
        s_signer = NULL;
    }
    if (s_lock) {
        osal_semaphore_delete(s_lock);
        s_lock = NULL;
    }
    s_host[0] = '\0';
    s_base_path[0] = '\0';
}

/** One escape-hatch request, staged so the signed call runs on the controller task. */
typedef struct {
    const char *path_suffix;
    esp_rmaker_controller_data_cb_t cb;
    void *user_ctx;
} api_get_t;

static esp_rmaker_error_t __api_get_job(void *arg)
{
    api_get_t *req = (api_get_t *)arg;
    esp_rmaker_aws_sign_response_t resp = {0};
    esp_rmaker_error_t err = controller_rest_get(req->path_suffix, &resp);
    if (err == ESP_RMAKER_OK) {
        err = controller_rest_status_to_error(req->path_suffix, resp.status_code);
    }
    if (err == ESP_RMAKER_OK && req->cb) {
        err = req->cb(req->user_ctx, (const char *)resp.body, resp.body_len);
    }
    esp_rmaker_aws_sign_response_free(&resp);
    return err;
}

esp_rmaker_error_t esp_rmaker_controller_api_get(const char *path_suffix, esp_rmaker_controller_data_cb_t cb,
        void *user_ctx)
{
    if (!path_suffix) {
        return ESP_RMAKER_INVALID_ARG;
    }
    /* The callback runs on the controller task, which is where the response buffer lives. */
    api_get_t req = {
        .path_suffix = path_suffix,
        .cb = cb,
        .user_ctx = user_ctx,
    };
    return controller_task_run_sync(__api_get_job, &req);
}

/* --- Group ------------------------------------------------------------------------------- */

esp_rmaker_error_t controller_group_id(char **group_id)
{
    esp_rmaker_error_t err = esp_rmaker_get_stored_group_id(group_id);
    if (err == ESP_RMAKER_NOT_FOUND) {
        OSAL_LOGW(TAG, "This node is in no group yet");
    }
    return err;
}
