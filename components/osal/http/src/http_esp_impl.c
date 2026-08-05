/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * ESP-IDF HTTP client implementation of osal_http_impl.h.
 */

#include "osal_http_impl.h"
#include "osal_http_config.h"

/* Standard includes */
#include <string.h>
#include <stdbool.h>

/* Platform common includes */
#include "osal_mem_alloc.h"

/* ESP-IDF includes */
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_crt_bundle.h"

/** Logging tag */
static const char *TAG = "osal_http_esp";

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0)
/** Header key cache node for IDF < 5.4 */
typedef struct header_key_node {
    char *key;
    struct header_key_node *next;
} header_key_node_t;
#endif /* ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0) */

/** Internal HTTP client handle structure */
typedef struct {
    esp_http_client_handle_t esp_client;
    osal_http_read_callback_t read_callback;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0)
    header_key_node_t *current_keys; /* Cache of header keys for delete_all_headers workaround */
#endif /* ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0) */
} http_esp_handle_t;

/** Static function declarations */
static esp_err_t event_handler(esp_http_client_event_t *event);
static esp_http_client_method_t convert_method(osal_http_method_t method);
static esp_http_client_transport_t convert_transport_type(osal_http_transport_t transport_type);
static osal_err_t convert_esp_err(esp_err_t err);

/** Implementation functions */

osal_err_t http_esp_init(const osal_http_config_t *config, osal_http_handle_t *handle)
{
    esp_http_client_config_t esp_config = {0};
    esp_http_client_handle_t esp_client;

    if (config == NULL || handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return OSAL_ERR_INVALID_ARG;
    }

    /* Make handle */
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)calloc(1, sizeof(http_esp_handle_t));
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for handle");
        return OSAL_ERR_NO_MEM;
    }

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0)
    /* Initialize header keys cache for delete_all_headers workaround */
    esp_handle->current_keys = NULL;
#endif /* ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0) */

    /* Convert common config to ESP config */
    if (config->url) {
        esp_config.url = config->url;
    } else {
        esp_config.host = config->host;
        esp_config.port = config->port;
        esp_config.path = config->path;
    }

    esp_config.method = convert_method(config->method);
    /* This interface does not support authentication */
    esp_config.auth_type = HTTP_AUTH_TYPE_NONE;

    /* Use ESP certificate bundle for server verification (no cert_pem in common config) */
    if (config->transport_type == OSAL_HTTP_TRANSPORT_OVER_SSL) {
        esp_config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    if (config->client_cert) {
        esp_config.client_cert_pem = config->client_cert;
        /* len==0 => PEM, derive strlen()+1 (incl null); len>0 => DER/explicit, use verbatim */
        esp_config.client_cert_len = config->client_cert_len
                                     ? config->client_cert_len
                                     : strlen(config->client_cert) + 1;
    }
    if (config->client_key) {
        esp_config.client_key_pem = config->client_key;
        esp_config.client_key_len = config->client_key_len
                                    ? config->client_key_len
                                    : strlen(config->client_key) + 1;
    }

    esp_config.transport_type = convert_transport_type(config->transport_type);

    esp_config.timeout_ms = config->timeout_ms ? config->timeout_ms : configHTTP_COMMON_TIMEOUT_MS;

    esp_config.buffer_size_tx = config->buffer_size_tx ? config->buffer_size_tx : configHTTP_COMMON_BUFFER_SIZE;
    esp_config.buffer_size = config->buffer_size_rx ? config->buffer_size_rx : configHTTP_COMMON_BUFFER_SIZE;

    esp_config.keep_alive_enable = config->keep_alive_enable;
    esp_config.keep_alive_idle = config->keep_alive_idle;
    esp_config.keep_alive_interval = config->keep_alive_interval;
    esp_config.keep_alive_count = config->keep_alive_count;

    esp_config.skip_cert_common_name_check = config->skip_cert_common_name_check;
    if (config->common_name) {
        esp_config.common_name = config->common_name;
    }

    /* Add event handler and user data */
    esp_config.event_handler = event_handler;
    esp_config.user_data = esp_handle;

    /* Initialize ESP HTTP client */
    esp_client = esp_http_client_init(&esp_config);
    if (esp_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize ESP HTTP client");
        free(esp_handle);
        return OSAL_ERR_FAIL;
    }
    esp_handle->esp_client = esp_client;
    *handle = (osal_http_handle_t)esp_handle;

    ESP_LOGI(TAG, "HTTP client initialized successfully");
    return OSAL_ERR_OK;
}

osal_err_t http_esp_cleanup(osal_http_handle_t handle)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)handle;
    esp_http_client_handle_t esp_client = esp_handle->esp_client;
    if (esp_handle == NULL || esp_client == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    esp_http_client_cleanup(esp_client);

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0)
    /* Free header keys cache */
    header_key_node_t *current = esp_handle->current_keys;
    while (current != NULL) {
        header_key_node_t *next = current->next;
        free(current->key);
        free(current);
        current = next;
    }
#endif /* ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0) */

    /* Free handle */
    free(esp_handle);

    ESP_LOGI(TAG, "HTTP client cleaned up");
    return OSAL_ERR_OK;
}

osal_err_t http_esp_set_url(osal_http_handle_t handle, const char *url)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)handle;
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }
    esp_http_client_handle_t esp_client = esp_handle->esp_client;
    if (esp_client == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_http_client_set_url(esp_client, url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP set URL failed: (%d) %s", err, esp_err_to_name(err));
        return convert_esp_err(err);
    }

    return OSAL_ERR_OK;
}

osal_err_t http_esp_set_method(osal_http_handle_t handle, osal_http_method_t method)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)handle;
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }
    esp_http_client_handle_t esp_client = esp_handle->esp_client;
    if (esp_client == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    esp_http_client_method_t esp_method = convert_method(method);
    esp_err_t err = esp_http_client_set_method(esp_client, esp_method);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP set method failed: (%d) %s", err, esp_err_to_name(err));
        return convert_esp_err(err);
    }

    return OSAL_ERR_OK;
}

osal_err_t http_esp_set_header(osal_http_handle_t handle, const char *key, const char *value)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)handle;
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }
    esp_http_client_handle_t esp_client = esp_handle->esp_client;
    if (esp_client == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_http_client_set_header(esp_client, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP set header failed: (%d) %s", err, esp_err_to_name(err));
        return convert_esp_err(err);
    }

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0)
    /* Cache the header key for delete_all_headers workaround */
    if (key != NULL) {
        /* Check if key already exists in cache */
        header_key_node_t *current = esp_handle->current_keys;
        bool key_exists = false;
        while (current != NULL) {
            if (strcmp(current->key, key) == 0) {
                key_exists = true;
                break;
            }
            current = current->next;
        }

        /* Add key to cache if it doesn't exist */
        if (!key_exists) {
            header_key_node_t *new_node = (header_key_node_t *)malloc(sizeof(header_key_node_t));
            if (new_node == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for header key cache");
                return OSAL_ERR_NO_MEM;
            }
            new_node->key = OSAL_STRDUP_EXTRAM(key);
            if (new_node->key == NULL) {
                free(new_node);
                ESP_LOGE(TAG, "Failed to allocate memory for header key copy");
                return OSAL_ERR_NO_MEM;
            }
            new_node->next = esp_handle->current_keys;
            esp_handle->current_keys = new_node;
        }
    }
#endif /* ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0) */

    return OSAL_ERR_OK;
}

osal_err_t http_esp_delete_header(osal_http_handle_t handle, const char *key)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)handle;
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }
    esp_http_client_handle_t esp_client = esp_handle->esp_client;
    if (esp_client == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_http_client_delete_header(esp_client, key);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP delete header failed: (%d) %s", err, esp_err_to_name(err));
        return convert_esp_err(err);
    }

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0)
    /* Remove key from cache for delete_all_headers workaround */
    if (key != NULL) {
        header_key_node_t *current = esp_handle->current_keys;
        header_key_node_t *prev = NULL;
        while (current != NULL) {
            if (strcmp(current->key, key) == 0) {
                /* Found the key, remove it from the list */
                if (prev == NULL) {
                    /* Head of list */
                    esp_handle->current_keys = current->next;
                } else {
                    prev->next = current->next;
                }
                free(current->key);
                free(current);
                break;
            }
            prev = current;
            current = current->next;
        }
    }
#endif /* ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0) */

    return OSAL_ERR_OK;
}

osal_err_t http_esp_delete_all_headers(osal_http_handle_t handle)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)handle;
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }
    esp_http_client_handle_t esp_client = esp_handle->esp_client;
    if (esp_client == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
    /* Use native ESP-IDF function for IDF >= 5.4 */
    esp_err_t err = esp_http_client_delete_all_headers(esp_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP delete all headers failed: (%d) %s", err, esp_err_to_name(err));
        return convert_esp_err(err);
    }
#else
    /* Workaround for IDF < 5.4: delete headers using cached keys */
    header_key_node_t *current = esp_handle->current_keys;
    while (current != NULL) {
        /* Call ESP HTTP client delete header directly */
        esp_http_client_delete_header(esp_client, current->key);

        /* Free the cached key and node directly */
        header_key_node_t *next = current->next;
        free(current->key);
        free(current);
        current = next;
    }

    /* Clear the cache */
    esp_handle->current_keys = NULL;
#endif /* ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 0) */

    return OSAL_ERR_OK;
}

osal_err_t http_esp_set_request_body(osal_http_handle_t handle, const uint8_t *body, size_t body_size, const char *content_type)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)handle;
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }
    esp_http_client_handle_t esp_client = esp_handle->esp_client;
    if (esp_client == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    osal_err_t header_status = http_esp_set_header(handle, "Content-Type", content_type);
    if (header_status != OSAL_ERR_OK) {
        return header_status;
    }

    esp_err_t err = esp_http_client_set_post_field(esp_client, (const char *)body, body_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP set request body failed: (%d) %s", err, esp_err_to_name(err));
        return convert_esp_err(err);
    }
    return OSAL_ERR_OK;
}
osal_err_t http_esp_set_read_callback(osal_http_handle_t handle, osal_http_read_callback_t read_callback)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)handle;
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }
    esp_handle->read_callback = read_callback;
    return OSAL_ERR_OK;
}

osal_err_t http_esp_perform(osal_http_handle_t handle)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)handle;
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }
    esp_http_client_handle_t esp_client = esp_handle->esp_client;
    if (esp_client == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    /* Perform the request */
    esp_err_t err = esp_http_client_perform(esp_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP perform failed: (%d) %s", err, esp_err_to_name(err));
        return convert_esp_err(err);
    }
    return OSAL_ERR_OK;
}

int http_esp_get_status_code(osal_http_handle_t handle)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)handle;
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }
    esp_http_client_handle_t esp_client = esp_handle->esp_client;
    if (esp_client == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }

    return esp_http_client_get_status_code(esp_client);
}

int64_t http_esp_get_content_length(osal_http_handle_t handle)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)handle;
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }
    esp_http_client_handle_t esp_client = esp_handle->esp_client;
    if (esp_client == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }

    return esp_http_client_get_content_length(esp_client);
}

/** Static helper functions */

static esp_err_t event_handler(esp_http_client_event_t *event)
{
    http_esp_handle_t *esp_handle = (http_esp_handle_t *)event->user_data;
    if (esp_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_ERR_INVALID_STATE;
    }

    switch (event->event_id) {
    /* Data received */
    case HTTP_EVENT_ON_DATA:
        if (esp_handle->read_callback) {
            int64_t content_length = esp_http_client_get_content_length(event->client);
            int bytes_read = esp_handle->read_callback((uint8_t *)event->data, event->data_len, content_length);
            if (bytes_read != event->data_len) {
                return ESP_ERR_INVALID_STATE; /* Abort the operation */
            }
            return ESP_OK; /* Continue the operation */
        }
        break;
    default:
        break;
    }

    return ESP_OK;
}

static esp_http_client_method_t convert_method(osal_http_method_t method)
{
    switch (method) {
    case OSAL_HTTP_METHOD_GET:     return HTTP_METHOD_GET;
    case OSAL_HTTP_METHOD_POST:    return HTTP_METHOD_POST;
    case OSAL_HTTP_METHOD_PUT:     return HTTP_METHOD_PUT;
    case OSAL_HTTP_METHOD_PATCH:   return HTTP_METHOD_PATCH;
    case OSAL_HTTP_METHOD_DELETE:  return HTTP_METHOD_DELETE;
    case OSAL_HTTP_METHOD_HEAD:    return HTTP_METHOD_HEAD;
    case OSAL_HTTP_METHOD_OPTIONS: return HTTP_METHOD_OPTIONS;
    default:                         return HTTP_METHOD_GET;
    }
}

static esp_http_client_transport_t convert_transport_type(osal_http_transport_t transport_type)
{
    switch (transport_type) {
    case OSAL_HTTP_TRANSPORT_OVER_TCP:  return HTTP_TRANSPORT_OVER_TCP;
    case OSAL_HTTP_TRANSPORT_OVER_SSL:  return HTTP_TRANSPORT_OVER_SSL;
    default:                              return HTTP_TRANSPORT_UNKNOWN;
    }
}

static osal_err_t convert_esp_err(esp_err_t err)
{
    switch (err) {
    case ESP_OK: return OSAL_ERR_OK;
    case ESP_ERR_INVALID_ARG: return OSAL_ERR_INVALID_ARG;
    case ESP_ERR_INVALID_STATE: return OSAL_ERR_INVALID_STATE;
    case ESP_ERR_NO_MEM: return OSAL_ERR_NO_MEM;
    case ESP_ERR_TIMEOUT: return OSAL_ERR_TIMEOUT;
    case ESP_FAIL: return OSAL_ERR_FAIL;
    default: return OSAL_ERR_FAIL;
    }
}

/** Setup function */

osal_err_t osal_http_impl_setup(osal_http_impl_t *http_impl)
{
    if (http_impl == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    http_impl->init = http_esp_init;
    http_impl->cleanup = http_esp_cleanup;

    http_impl->set_url = http_esp_set_url;
    http_impl->set_method = http_esp_set_method;
    http_impl->set_header = http_esp_set_header;
    http_impl->delete_header = http_esp_delete_header;
    http_impl->delete_all_headers = http_esp_delete_all_headers;
    http_impl->set_request_body = http_esp_set_request_body;
    http_impl->set_read_callback = http_esp_set_read_callback;

    http_impl->perform = http_esp_perform;
    http_impl->get_status_code = http_esp_get_status_code;
    http_impl->get_content_length = http_esp_get_content_length;

    ESP_LOGI(TAG, "ESP HTTP implementation setup complete");
    http_impl->setup_done = true;
    return OSAL_ERR_OK;
}
