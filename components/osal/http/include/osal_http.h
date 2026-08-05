/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_http.h
 * @brief Prototypes for the HTTP common component.
 */

#ifndef OSAL_HTTP_PROTOTYPES_H
#define OSAL_HTTP_PROTOTYPES_H

/* Standard, ESP-IDF includes */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "osal_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** HTTP Methods */
typedef enum {
    OSAL_HTTP_METHOD_GET = 0,
    OSAL_HTTP_METHOD_POST,
    OSAL_HTTP_METHOD_PUT,
    OSAL_HTTP_METHOD_PATCH,
    OSAL_HTTP_METHOD_DELETE,
    OSAL_HTTP_METHOD_HEAD,
    OSAL_HTTP_METHOD_OPTIONS,
    OSAL_HTTP_METHOD_MAX,
} osal_http_method_t;

/** HTTP Transport types */
typedef enum {
    OSAL_HTTP_TRANSPORT_UNKNOWN = 0,
    OSAL_HTTP_TRANSPORT_OVER_TCP,
    OSAL_HTTP_TRANSPORT_OVER_SSL,
} osal_http_transport_t;

/** HTTP Client configuration */
typedef struct {
    /** HTTP URL */
    const char *url;

    /** HTTP Hostname */
    const char *host;

    /** HTTP Port */
    int port;

    /** HTTP Path */
    const char *path;

    /** HTTP Method */
    osal_http_method_t method;

    /** SSL/TLS (server CA uses platform bundle: ESP cert bundle, POSIX ca-bundle-posix) */
    const char *client_cert;        /**< Client cert: DER, or NULL-terminated PEM */
    size_t client_cert_len;         /**< Cert length. 0 => PEM, impl derives strlen()+1 */
    const char *client_key;         /**< Client key: DER, or NULL-terminated PEM */
    size_t client_key_len;          /**< Key length. 0 => PEM, impl derives strlen()+1 */

    /** Transport */
    osal_http_transport_t transport_type;

    /** Timeouts */
    int timeout_ms;

    /** Buffer sizes */
    int buffer_size_tx;
    int buffer_size_rx;

    /** Keep alive settings */
    bool keep_alive_enable;
    int keep_alive_idle;
    int keep_alive_interval;
    int keep_alive_count;

    /** Other options */
    bool skip_cert_common_name_check;
    const char *common_name;

} osal_http_config_t;

/** HTTP Client handle (opaque) */
typedef struct osal_http_handle *osal_http_handle_t;

/**
 * @brief HTTP Init function prototype
 *
 * @param[in] config The HTTP client configuration.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_http_init_t)(const osal_http_config_t *config, osal_http_handle_t *handle);

/**
 * @brief HTTP Cleanup function prototype
 *
 * @param[in] handle The HTTP client handle.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_http_cleanup_t)(osal_http_handle_t handle);

/**
 * @brief HTTP Set URL function prototype

 * @param[in] handle The HTTP client handle.
 * @param[in] url The URL to set.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_http_set_url_t)(osal_http_handle_t handle, const char *url);

/**
 * @brief HTTP Set Method function prototype

 * @param[in] handle The HTTP client handle.
 * @param[in] method The HTTP method to set.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_http_set_method_t)(osal_http_handle_t handle, osal_http_method_t method);

/**
 * @brief HTTP Set Header function prototype

 * @param[in] handle The HTTP client handle.
 * @param[in] key The header key to set.
 * @param[in] value The header value to set.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_http_set_header_t)(osal_http_handle_t handle, const char *key, const char *value);

/**
 * @brief HTTP Delete Header function prototype

 * @param[in] handle The HTTP client handle.
 * @param[in] key The header key to delete.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_http_delete_header_t)(osal_http_handle_t handle, const char *key);

/**
 * @brief HTTP Delete All Headers function prototype

 * @param[in] handle The HTTP client handle.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_http_delete_all_headers_t)(osal_http_handle_t handle);

/**
 * @brief HTTP Set Request Body function prototype

 * @param[in] handle The HTTP client handle.
 * @param[in] body The request body.
 * @param[in] body_size The size of the request body.
 * @param[in] content_type The content type of the request body.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_http_set_request_body_t)(osal_http_handle_t handle, const uint8_t *body, size_t body_size, const char *content_type);

/** HTTP Read callback function prototype
 * Called whenever data is available to read.
 *
 * @param[in] buffer Buffer to read from.
 * @param[in] len Maximum length to read.
 * @param[in] content_length Content length.
 *
 * @return Number of bytes processed. If this mismatches the length, the operation will be aborted.
 */
typedef int (*osal_http_read_callback_t)(uint8_t *buffer, size_t len, int64_t content_length);

/**
 * @brief HTTP Set Read Callback function prototype
 *
 * @param[in] handle The HTTP client handle.
 * @param[in] read_callback The read callback function.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*http_comon_set_read_callback_t)(osal_http_handle_t handle, osal_http_read_callback_t read_callback);

/** HTTP Perform function prototype
 * - Performs the HTTP request.
 * - Calls the read callback for each chunk of data received.
 * - Returns when the request is complete or an error occurs.
 *
 * @param[in] handle The HTTP client handle.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_http_perform_t)(osal_http_handle_t handle);


/**
 * @brief HTTP Get Status Code function prototype
 *
 * @param[in] handle The HTTP client handle.
 *
 * @return HTTP status code, or negative on error.
 */
typedef int (*osal_http_get_status_code_t)(osal_http_handle_t handle);

/**
 * @brief HTTP Get Content Length function prototype
 *
 * @param[in] handle The HTTP client handle.
 *
 * @return Content length, or negative on error.
 */
typedef int64_t (*osal_http_get_content_length_t)(osal_http_handle_t handle);

/**  HTTP implementation */
typedef struct {
    /** Flag to indicate if the HTTP config setup is done */
    bool setup_done;

    /** Function pointers */

    /* Initialization and cleanup */
    osal_http_init_t init;
    osal_http_cleanup_t cleanup;

    /* Request setup */
    osal_http_set_url_t set_url;
    osal_http_set_method_t set_method;
    osal_http_set_header_t set_header;
    osal_http_delete_header_t delete_header;
    osal_http_delete_all_headers_t delete_all_headers;
    osal_http_set_request_body_t set_request_body;
    http_comon_set_read_callback_t set_read_callback;

    /* Request execution */
    osal_http_perform_t perform;
    osal_http_get_status_code_t get_status_code;
    osal_http_get_content_length_t get_content_length;
} osal_http_impl_t;

#ifdef __cplusplus
}
#endif

#endif /* OSAL_HTTP_PROTOTYPES_H */
