/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * libcurl-based HTTP client implementation.
 * Uses libcurl for secure, standards-compliant HTTP operations.
 */

#include "osal_http_impl.h"
#include "osal_http_config.h"

/* libcurl includes */
#include <curl/curl.h>
#include <curl/curlver.h> /* For LIBCURL_VERSION_NUM */

/* Standard includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

/* Platform common log */
#include "osal_log.h"
#include "osal_ca_bundle.h"

/** Logging tag */
static const char *TAG = "osal_http_curl";

/** Maximum parameters */
#define MAX_URL_SIZE 2048
#define MAX_HEADERS 50

/** Header entry structure */
typedef struct {
    char *key;
    char *value;
} header_entry_t;

/** Internal HTTP client handle structure */
typedef struct osal_http_handle {
    /* libcurl handle */
    CURL *curl;

    /* Request configuration */
    char url[MAX_URL_SIZE];

    /* SSL/TLS configuration (server CA from ca-bundle-posix) */
    const char *client_cert_pem;
    const char *client_key_pem;
    int skip_cert_common_name_check;
    const char *common_name;

    /* Response state */
    int status_code;
    int64_t content_length;

    /* Read callback for response data */
    osal_http_read_callback_t read_callback;

    /* Headers for API compatibility */
    header_entry_t headers[MAX_HEADERS];
    int header_count;

    /* Configuration */
    long timeout_ms;
} http_libcurl_handle_t;

/** Static function declarations */
static osal_err_t add_header(http_libcurl_handle_t *handle, const char *key, const char *value);
static struct curl_slist *build_curl_headers(http_libcurl_handle_t *handle);
static void free_header_entry(header_entry_t *entry);
static header_entry_t *find_header(http_libcurl_handle_t *handle, const char *key);
static osal_err_t execute_request(http_libcurl_handle_t *handle);
static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata);
static osal_err_t curl_to_common_error(CURLcode res);

/** Implementation functions */

// Forward declarations
static osal_err_t http_libcurl_set_method(osal_http_handle_t handle_in, osal_http_method_t method);

/**
 * Initialize libcurl HTTP client
 */
/* Set a client cert or key blob for mTLS (DER or NULL-terminated PEM).
 * Convention: len==0 => PEM (derive strlen). Type sniffed from content:
 * "-----BEGIN" prefix => PEM, otherwise DER. CURL_BLOB_COPY => curl owns copy. */
static void http_libcurl_set_ssl_blob(CURL *curl, const char *data, size_t len,
                                      CURLoption type_opt, CURLoption blob_opt)
{
    if (!data) {
        return;
    }
    if (len == 0) {
        len = strlen(data);
    }
    int is_pem = (len >= 10) && (memcmp(data, "-----BEGIN", 10) == 0);
    struct curl_blob blob = {
        .data = (void *)data,
        .len = len,
        .flags = CURL_BLOB_COPY,
    };
    curl_easy_setopt(curl, type_opt, is_pem ? "PEM" : "DER");
    curl_easy_setopt(curl, blob_opt, &blob);
}

osal_err_t http_libcurl_init(const osal_http_config_t *config, osal_http_handle_t *handle_out)
{
    http_libcurl_handle_t *handle = NULL;
    CURL *curl = NULL;

    if (config == NULL || handle_out == NULL) {
        OSAL_LOGE(TAG, "Invalid parameters");
        return OSAL_ERR_INVALID_ARG;
    }

    /* Allocate handle */
    handle = (http_libcurl_handle_t *)calloc(1, sizeof(http_libcurl_handle_t));
    if (handle == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate handle");
        return OSAL_ERR_NO_MEM;
    }

    /* Initialize libcurl */
    curl = curl_easy_init();
    if (curl == NULL) {
        OSAL_LOGE(TAG, "Failed to initialize libcurl");
        free(handle);
        return OSAL_ERR_FAIL;
    }

    handle->curl = curl;

    /* Initialize defaults */
    handle->timeout_ms = (long)(config->timeout_ms ? config->timeout_ms : configHTTP_COMMON_TIMEOUT_MS);
    handle->skip_cert_common_name_check = config->skip_cert_common_name_check;
    handle->read_callback = NULL;
    // Basic auth not supported

    /* Set URL if provided */
    if (config->url) {
        strncpy(handle->url, config->url, sizeof(handle->url) - 1);
        curl_easy_setopt(curl, CURLOPT_URL, handle->url);
    } else if (config->host) {
        /* Construct URL from components */
        char url[sizeof(handle->url)];
        int port = config->port ? config->port : 80;
        const char *scheme = (config->transport_type == OSAL_HTTP_TRANSPORT_OVER_SSL) ? "https" : "http";

        if (config->path && config->path[0] == '/') {
            snprintf(url, sizeof(url), "%s://%s:%d%s", scheme, config->host, port, config->path);
        } else {
            snprintf(url, sizeof(url), "%s://%s:%d/%s", scheme, config->host, port, config->path ? config->path : "");
        }

        strncpy(handle->url, url, sizeof(handle->url) - 1);
        curl_easy_setopt(curl, CURLOPT_URL, handle->url);
    }

    /* Configure libcurl */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, handle->timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); /* Thread-safe */

    /* Set callbacks */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, handle);
    /* No header callback needed - we only need status and content-length */

    /* Buffer sizing - TX */
    long buffer_size_tx = config->buffer_size_tx ? config->buffer_size_tx : configHTTP_COMMON_BUFFER_SIZE;
    if (buffer_size_tx < 16 * 1024) {
        OSAL_LOGW(TAG, "TX buffer size too small, setting to 16KB");
        buffer_size_tx = 16 * 1024;
    }
    if (buffer_size_tx > 2 * 1024 * 1024) {
        OSAL_LOGW(TAG, "TX buffer size too large, setting to 2MB");
        buffer_size_tx = 2 * 1024 * 1024;
    }
    curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE, buffer_size_tx);

    /* Buffer sizing - RX */
    long buffer_size_rx = config->buffer_size_rx ? config->buffer_size_rx : configHTTP_COMMON_BUFFER_SIZE;
    if (buffer_size_rx < 1024) {
        OSAL_LOGW(TAG, "RX buffer size too small, setting to 1024");
        buffer_size_rx = 1024;
    }
    if (buffer_size_rx > CURL_MAX_READ_SIZE) {
        OSAL_LOGW(TAG, "RX buffer size too large, setting to %" PRIu64, (uint64_t)CURL_MAX_READ_SIZE);
        buffer_size_rx = CURL_MAX_READ_SIZE;
    }
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, buffer_size_rx);

    /* Use CA bundle from ca-bundle-posix for server verification */
    if (config->transport_type == OSAL_HTTP_TRANSPORT_OVER_SSL) {
        const unsigned char *cacrt_start = NULL;
        const unsigned char *cacrt_end = NULL;
        osal_ca_bundle_get(&cacrt_start, &cacrt_end);
        size_t cacrt_size = (size_t)(cacrt_end - cacrt_start);
        if (cacrt_size > 0) {
            struct curl_blob cert_blob;
            cert_blob.data = (void *)cacrt_start;
            cert_blob.len = cacrt_size;
            cert_blob.flags = CURL_BLOB_NOCOPY;
            curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &cert_blob);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        } else {
            OSAL_LOGW(TAG, "CA bundle empty, SSL verification disabled");
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        }
    }

    /* Client cert + key for mTLS */
    http_libcurl_set_ssl_blob(curl, config->client_cert, config->client_cert_len,
                              CURLOPT_SSLCERTTYPE, CURLOPT_SSLCERT_BLOB);
    http_libcurl_set_ssl_blob(curl, config->client_key, config->client_key_len,
                              CURLOPT_SSLKEYTYPE, CURLOPT_SSLKEY_BLOB);

    /* Set method */
    http_libcurl_set_method((osal_http_handle_t)handle, config->method);

    *handle_out = (osal_http_handle_t)handle;
    OSAL_LOGI(TAG, "libcurl HTTP client initialized successfully");
    return OSAL_ERR_OK;
}

/**
 * Cleanup libcurl HTTP client
 */
osal_err_t http_libcurl_cleanup(osal_http_handle_t handle_in)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)handle_in;

    if (handle == NULL) {
        OSAL_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    /* Cleanup libcurl */
    if (handle->curl) {
        curl_easy_cleanup(handle->curl);
    }

    /* Free allocated memory */
    if (handle->client_cert_pem) {
        free((void *)handle->client_cert_pem);
    }
    if (handle->client_key_pem) {
        free((void *)handle->client_key_pem);
    }
    if (handle->common_name) {
        free((void *)handle->common_name);
    }

    for (int i = 0; i < handle->header_count; i++) {
        free_header_entry(&handle->headers[i]);
    }

    free(handle);

    OSAL_LOGI(TAG, "libcurl HTTP client cleaned up");
    return OSAL_ERR_OK;
}

/**
 * Set URL for libcurl HTTP client
 */
osal_err_t http_libcurl_set_url(osal_http_handle_t handle_in, const char *url)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)handle_in;

    if (handle == NULL || url == NULL) {
        OSAL_LOGE(TAG, "Invalid parameters");
        return OSAL_ERR_INVALID_ARG;
    }

    strncpy(handle->url, url, sizeof(handle->url) - 1);
    curl_easy_setopt(handle->curl, CURLOPT_URL, handle->url);

    return OSAL_ERR_OK;
}

/**
 * Set method for libcurl HTTP client
 */
osal_err_t http_libcurl_set_method(osal_http_handle_t handle_in, osal_http_method_t method)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)handle_in;

    if (handle == NULL) {
        OSAL_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    long opt_httpget = 0L;
    long opt_post = 0L;
    long opt_upload = 0L;
    long opt_no_body = 0L;
    char *opt_customrequest = NULL;

    /* Convert method to libcurl */
    switch (method) {
    case OSAL_HTTP_METHOD_GET:
        OSAL_LOGD(TAG, "Setting HTTP GET method");
        opt_httpget = 1L;
        break;
    case OSAL_HTTP_METHOD_POST:
        OSAL_LOGD(TAG, "Setting HTTP POST method");
        opt_post = 1L;
        break;
    case OSAL_HTTP_METHOD_PUT:
        OSAL_LOGD(TAG, "Setting HTTP PUT method");
        opt_customrequest = "PUT";
        break;
    case OSAL_HTTP_METHOD_PATCH:
        OSAL_LOGD(TAG, "Setting HTTP PATCH method");
        opt_customrequest = "PATCH";
        break;
    case OSAL_HTTP_METHOD_DELETE:
        OSAL_LOGD(TAG, "Setting HTTP DELETE method");
        opt_customrequest = "DELETE";
        break;
    case OSAL_HTTP_METHOD_HEAD:
        OSAL_LOGD(TAG, "Setting HTTP HEAD method");
        opt_no_body = 1L;
        break;
    case OSAL_HTTP_METHOD_OPTIONS:
        OSAL_LOGD(TAG, "Setting HTTP OPTIONS method");
        opt_customrequest = "OPTIONS";
        break;
    default:
        OSAL_LOGD(TAG, "Setting HTTP GET method (default)");
        opt_httpget = 1L;
        break;
    }

    /* Set all options */
    curl_easy_setopt(handle->curl, CURLOPT_HTTPGET, opt_httpget);
    curl_easy_setopt(handle->curl, CURLOPT_POST, opt_post);
    curl_easy_setopt(handle->curl, CURLOPT_UPLOAD, opt_upload);
    curl_easy_setopt(handle->curl, CURLOPT_NOBODY, opt_no_body);
    curl_easy_setopt(handle->curl, CURLOPT_CUSTOMREQUEST, opt_customrequest);

    return OSAL_ERR_OK;
}

/**
 * Set header for libcurl HTTP client
 */
osal_err_t http_libcurl_set_header(osal_http_handle_t handle_in, const char *key, const char *value)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)handle_in;

    if (handle == NULL || key == NULL || value == NULL) {
        OSAL_LOGE(TAG, "Invalid parameters");
        return OSAL_ERR_INVALID_ARG;
    }

    /* Add to API-compatible header list */
    osal_err_t status = add_header(handle, key, value);
    if (status != OSAL_ERR_OK) {
        return status;
    }

    return OSAL_ERR_OK;
}

/**
 * Delete header for libcurl HTTP client
 */
osal_err_t http_libcurl_delete_header(osal_http_handle_t handle_in, const char *key)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)handle_in;

    if (handle == NULL || key == NULL) {
        OSAL_LOGE(TAG, "Invalid parameters");
        return OSAL_ERR_INVALID_ARG;
    }

    /* Find and remove from header list */
    header_entry_t *entry = find_header(handle, key);
    if (entry) {
        free_header_entry(entry);
        /* Shift remaining headers */
        int index = entry - handle->headers;
        for (int i = index; i < handle->header_count - 1; i++) {
            handle->headers[i] = handle->headers[i + 1];
        }
        handle->header_count--;
    }

    return OSAL_ERR_OK;
}

/**
 * Delete all headers for libcurl HTTP client
 */
osal_err_t http_libcurl_delete_all_headers(osal_http_handle_t handle_in)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)handle_in;

    if (handle == NULL) {
        OSAL_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    /* Free all headers */
    for (int i = 0; i < handle->header_count; i++) {
        free_header_entry(&handle->headers[i]);
    }
    handle->header_count = 0;

    return OSAL_ERR_OK;
}

/**
 * Set request body for libcurl HTTP client
 */
osal_err_t http_libcurl_set_request_body(osal_http_handle_t handle_in, const uint8_t *body, size_t body_size, const char *content_type)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)handle_in;

    if (handle == NULL || (body_size > 0 && body == NULL)) {
        OSAL_LOGE(TAG, "Invalid parameters");
        return OSAL_ERR_INVALID_ARG;
    }

    /* Set Content-Type header if provided */
    if (content_type) {
        osal_err_t status = http_libcurl_set_header(handle_in, "Content-Type", content_type);
        if (status != OSAL_ERR_OK) {
            return status;
        }
    }

    /* Set request body */
    curl_easy_setopt(handle->curl, CURLOPT_POSTFIELDSIZE, body_size);
    curl_easy_setopt(handle->curl, CURLOPT_COPYPOSTFIELDS, (char *)body);

    return OSAL_ERR_OK;
}

/**
 * Set read callback for libcurl HTTP client
 */
osal_err_t http_libcurl_set_read_callback(osal_http_handle_t handle_in, osal_http_read_callback_t read_callback)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)handle_in;

    if (handle == NULL) {
        OSAL_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    handle->read_callback = read_callback;
    return OSAL_ERR_OK;
}

/**
 * Perform HTTP request synchronously for libcurl HTTP client
 */
osal_err_t http_libcurl_perform(osal_http_handle_t handle_in)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)handle_in;

    if (handle == NULL) {
        OSAL_LOGE(TAG, "Invalid handle");
        return OSAL_ERR_INVALID_ARG;
    }

    /* Set curl headers */
    struct curl_slist *curl_headers = build_curl_headers(handle);
    curl_easy_setopt(handle->curl, CURLOPT_HTTPHEADER, curl_headers);

    /* Execute the request */
    CURLcode res = curl_easy_perform(handle->curl);
    /* Free curl headers */
    if (curl_headers) {
        curl_slist_free_all(curl_headers);
    }

    if (res != CURLE_OK) {
        OSAL_LOGE(TAG, "curl_easy_perform failed: %s", curl_easy_strerror(res));
        return curl_to_common_error(res);
    }
    /* Get status code */
    long response_code;
    curl_easy_getinfo(handle->curl, CURLINFO_RESPONSE_CODE, &response_code);
    handle->status_code = (int)response_code;

    /* Get content length */
    curl_off_t content_length;
    res = curl_easy_getinfo(handle->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
    if (res == CURLE_OK) {
        if (content_length < 0) {
            handle->content_length = 0;
        } else {
            handle->content_length = (int64_t)content_length;
        }
    }

    OSAL_LOGD(TAG, "Request completed, status: %d, content-length: %" PRId64,
              handle->status_code, handle->content_length);

    return OSAL_ERR_OK;
}



/**
 * Get status code for libcurl HTTP client
 */
int http_libcurl_get_status_code(osal_http_handle_t handle_in)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)handle_in;

    if (handle == NULL) {
        OSAL_LOGE(TAG, "Invalid handle");
        return -1;
    }

    return handle->status_code;
}

/**
 * Get content length for libcurl HTTP client
 */
int64_t http_libcurl_get_content_length(osal_http_handle_t handle_in)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)handle_in;

    if (handle == NULL) {
        OSAL_LOGE(TAG, "Invalid handle");
        return -1;
    }

    return handle->content_length;
}

/* Static helper functions */

/**
 * libcurl write callback - receives response body and calls read callback
 */
static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    http_libcurl_handle_t *handle = (http_libcurl_handle_t *)userdata;
    size_t total_size = size * nmemb;

    /* Call the read callback if set */
    if (handle->read_callback) {
        int bytes_processed = handle->read_callback((uint8_t *)ptr, total_size, handle->content_length);
        if (bytes_processed != (int)total_size) {
            /* Callback didn't process all data - this is an error */
            OSAL_LOGE(TAG, "Read callback failed to process all data: expected %zu, got %d", total_size, bytes_processed);
            return 0; /* Signal error to libcurl */
        }
    }

    return total_size;
}


/**
 * Convert libcurl error to http_common status
 */
static osal_err_t curl_to_common_error(CURLcode res)
{
    switch (res) {
    case CURLE_OK:
        return OSAL_ERR_OK;
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
        return OSAL_ERR_HTTP_CONNECTION_FAILED;
    case CURLE_OPERATION_TIMEDOUT:
        return OSAL_ERR_TIMEOUT;
    case CURLE_OUT_OF_MEMORY:
        return OSAL_ERR_NO_MEM;
    case CURLE_BAD_FUNCTION_ARGUMENT:
        return OSAL_ERR_INVALID_ARG;
    default:
        return OSAL_ERR_FAIL;
    }
}

/**
 * Add header to handle
 */
static osal_err_t add_header(http_libcurl_handle_t *handle, const char *key, const char *value)
{
    if (handle->header_count >= MAX_HEADERS) {
        OSAL_LOGE(TAG, "Too many headers");
        return OSAL_ERR_FAIL;
    }

    /* Check if header already exists, replace if so */
    header_entry_t *existing = find_header(handle, key);
    if (existing) {
        free(existing->value);
        size_t value_len = strlen(value) + 1;
        existing->value = (char *)malloc(value_len);
        if (!existing->value) {
            return OSAL_ERR_NO_MEM;
        }
        memcpy(existing->value, value, value_len);
        return OSAL_ERR_OK;
    }

    /* Add new header */
    size_t key_len = strlen(key) + 1;
    size_t value_len = strlen(value) + 1;

    handle->headers[handle->header_count].key = (char *)malloc(key_len);
    handle->headers[handle->header_count].value = (char *)malloc(value_len);

    if (!handle->headers[handle->header_count].key || !handle->headers[handle->header_count].value) {
        free_header_entry(&handle->headers[handle->header_count]);
        return OSAL_ERR_NO_MEM;
    }

    memcpy(handle->headers[handle->header_count].key, key, key_len);
    memcpy(handle->headers[handle->header_count].value, value, value_len);

    handle->header_count++;
    return OSAL_ERR_OK;
}

/**
 * Build curl headers list
 */
static struct curl_slist *build_curl_headers(http_libcurl_handle_t *handle)
{
    struct curl_slist *curl_headers = NULL;
    for (int i = 0; i < handle->header_count; i++) {
        char header_line[1024];
        snprintf(header_line, sizeof(header_line), "%s: %s", handle->headers[i].key, handle->headers[i].value);
        struct curl_slist *new_list = curl_slist_append(curl_headers, header_line);
        if (!new_list) {
            curl_slist_free_all(curl_headers);
            return NULL;
        }
        curl_headers = new_list;
    }
    return curl_headers;
}

/**
 * Free header entry
 */
static void free_header_entry(header_entry_t *entry)
{
    if (entry->key) {
        free(entry->key);
    }
    if (entry->value) {
        free(entry->value);
    }
    entry->key = NULL;
    entry->value = NULL;
}

/**
 * Find header by key (case-insensitive)
 */
static header_entry_t *find_header(http_libcurl_handle_t *handle, const char *key)
{
    for (int i = 0; i < handle->header_count; i++) {
        if (strcasecmp(handle->headers[i].key, key) == 0) {
            return &handle->headers[i];
        }
    }
    return NULL;
}

/**
 * Setup function for libcurl implementation
 */
osal_err_t osal_http_impl_setup(osal_http_impl_t *http_impl)
{
    if (http_impl == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    /* Initialize libcurl globally */
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        OSAL_LOGE(TAG, "Failed to initialize libcurl: %s", curl_easy_strerror(res));
        return OSAL_ERR_FAIL;
    }

    http_impl->init = http_libcurl_init;
    http_impl->cleanup = http_libcurl_cleanup;

    http_impl->set_url = http_libcurl_set_url;
    http_impl->set_method = http_libcurl_set_method;
    http_impl->set_header = http_libcurl_set_header;
    http_impl->delete_header = http_libcurl_delete_header;
    http_impl->delete_all_headers = http_libcurl_delete_all_headers;
    http_impl->set_request_body = http_libcurl_set_request_body;
    http_impl->set_read_callback = http_libcurl_set_read_callback;

    http_impl->perform = http_libcurl_perform;
    http_impl->get_status_code = http_libcurl_get_status_code;
    http_impl->get_content_length = http_libcurl_get_content_length;
    OSAL_LOGI(TAG, "libcurl HTTP implementation setup complete");
    http_impl->setup_done = true;
    return OSAL_ERR_OK;
}
