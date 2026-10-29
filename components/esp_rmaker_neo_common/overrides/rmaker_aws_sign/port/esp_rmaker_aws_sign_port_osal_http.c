/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_aws_sign_port_osal_http.c
 * @brief HTTP ops for the rmaker_aws_sign port, over osal's HTTP client.
 */

#include "esp_rmaker_aws_sign_port_osal_priv.h"

/* Signing exists to send requests, so an osal without its HTTP client is a
 * misconfiguration rather than a smaller build. */
#if defined(__has_include) && !__has_include("osal_http_impl.h")
#error "rmaker_aws_sign needs osal's HTTP client: build osal with OSAL_INCLUDE_HTTP / CONFIG_OSAL_INCLUDE_HTTP=y"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "osal_http_impl.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"

static const char *TAG = "rmng_aws_sign_http";

/** What the component holds as its ``esp_rmaker_aws_sign_http_handle_t``. */
typedef struct {
    osal_http_handle_t h;
    esp_rmaker_aws_sign_http_receiver_t rx;   /**< Set for the duration of one perform. */
    bool rx_set;
    char content_type[64];
} __port_http_t;

static osal_http_impl_t __http_impl;
static bool __http_impl_ready = false;

static osal_http_method_t __method_to_osal(esp_rmaker_aws_sign_http_method_t method)
{
    switch (method) {
    case ESP_RMAKER_AWS_SIGN_HTTP_POST:
        return OSAL_HTTP_METHOD_POST;
    case ESP_RMAKER_AWS_SIGN_HTTP_PUT:
        return OSAL_HTTP_METHOD_PUT;
    case ESP_RMAKER_AWS_SIGN_HTTP_DELETE:
        return OSAL_HTTP_METHOD_DELETE;
    case ESP_RMAKER_AWS_SIGN_HTTP_PATCH:
        return OSAL_HTTP_METHOD_PATCH;
    default:
        return OSAL_HTTP_METHOD_GET;
    }
}

static int __http_headers_cb(int status_code, int64_t content_length, void *user_data)
{
    (void) status_code;
    __port_http_t *port_h = (__port_http_t *) user_data;
    if (port_h == NULL || !port_h->rx_set || port_h->rx.hint == NULL) {
        return 0;
    }
    return port_h->rx.hint(port_h->rx.ctx, content_length > 0 ? (size_t) content_length : 0);
}

static int __http_read_cb(uint8_t *buffer, size_t len, int64_t content_length, void *user_data)
{
    (void) content_length;
    __port_http_t *port_h = (__port_http_t *) user_data;
    if (port_h == NULL || !port_h->rx_set || port_h->rx.sink == NULL) {
        return (int) len;    /* no sink; discard rather than abort */
    }
    return port_h->rx.sink(port_h->rx.ctx, buffer, len);
}

static esp_err_t __http_open(const esp_rmaker_aws_sign_http_config_t *cfg,
                             esp_rmaker_aws_sign_http_handle_t *out)
{
    if (cfg == NULL || cfg->url == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!__http_impl_ready) {
        OSAL_LOGE(TAG, "osal HTTP implementation unavailable");
        return ESP_ERR_INVALID_STATE;
    }

    __port_http_t *port_h = (__port_http_t *) OSAL_CALLOC_EXTRAM(1, sizeof(*port_h));
    if (port_h == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* No server CA here: osal attaches the platform bundle once transport is SSL. */
    const bool is_tls = strncmp(cfg->url, "https://", 8) == 0;
    const osal_http_config_t hcfg = {
        .url = cfg->url,
        .method = __method_to_osal(cfg->method),
        .client_cert = cfg->client_cert,
        .client_cert_len = cfg->client_cert_len,
        .client_key = cfg->client_key,
        .client_key_len = cfg->client_key_len,
        .transport_type = is_tls ? OSAL_HTTP_TRANSPORT_OVER_SSL : OSAL_HTTP_TRANSPORT_OVER_TCP,
        .timeout_ms = cfg->timeout_ms,
        .keep_alive_enable = cfg->keep_alive,
    };

    osal_err_t err = __http_impl.init(&hcfg, &port_h->h);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to open HTTP client: %d", (int) err);
        free(port_h);
        return esp_rmaker_aws_sign_port_err(err);
    }
    err = __http_impl.set_read_callback(port_h->h, __http_read_cb, port_h);
    if (err == OSAL_ERR_OK) {
        err = __http_impl.set_headers_callback(port_h->h, __http_headers_cb, port_h);
    }
    if (err != OSAL_ERR_OK) {
        __http_impl.cleanup(port_h->h);
        free(port_h);
        return esp_rmaker_aws_sign_port_err(err);
    }

    *out = (esp_rmaker_aws_sign_http_handle_t) port_h;
    return ESP_OK;
}

static esp_err_t __http_set_url(esp_rmaker_aws_sign_http_handle_t h, const char *url)
{
    __port_http_t *port_h = (__port_http_t *) h;
    if (port_h == NULL || url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_rmaker_aws_sign_port_err(__http_impl.set_url(port_h->h, url));
}

static esp_err_t __http_set_method(esp_rmaker_aws_sign_http_handle_t h,
                                   esp_rmaker_aws_sign_http_method_t method)
{
    __port_http_t *port_h = (__port_http_t *) h;
    if (port_h == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_rmaker_aws_sign_port_err(__http_impl.set_method(port_h->h, __method_to_osal(method)));
}

static esp_err_t __http_set_header(esp_rmaker_aws_sign_http_handle_t h, const char *name,
                                   const char *value)
{
    __port_http_t *port_h = (__port_http_t *) h;
    if (port_h == NULL || name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* A NULL value removes the header, which is how the pooled handle is stripped
     * between requests: leftovers would go on the wire unsigned. */
    if (strcasecmp(name, "Content-Type") == 0) {
        /* Remembered because osal takes the content type only with the body, set later. */
        const char *content_type = value ? value : "";
        if (strlen(content_type) >= sizeof(port_h->content_type)) {
            OSAL_LOGE(TAG, "Content-Type is longer than %d bytes", (int) sizeof(port_h->content_type) - 1);
            return ESP_ERR_INVALID_SIZE;    /* truncated, it would differ from the signed value */
        }
        snprintf(port_h->content_type, sizeof(port_h->content_type), "%s", content_type);
    }
    return esp_rmaker_aws_sign_port_err(__http_impl.set_header(port_h->h, name, value));
}

static esp_err_t __http_perform(esp_rmaker_aws_sign_http_handle_t h, const uint8_t *body,
                                size_t body_len, const esp_rmaker_aws_sign_http_receiver_t *rx)
{
    __port_http_t *port_h = (__port_http_t *) h;
    if (port_h == NULL || rx == NULL || rx->sink == NULL || (body_len > 0 && body == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Unconditional: a kept-alive handle would otherwise resend the previous body. */
    const char *content_type = port_h->content_type[0] ? port_h->content_type : NULL;
    osal_err_t err = __http_impl.set_request_body(port_h->h, body, body_len, content_type);
    if (err != OSAL_ERR_OK) {
        return esp_rmaker_aws_sign_port_err(err);
    }

    port_h->rx = *rx;
    port_h->rx_set = true;
    err = __http_impl.perform(port_h->h);
    port_h->rx_set = false;

    return esp_rmaker_aws_sign_port_err(err);
}

static int __http_status_code(esp_rmaker_aws_sign_http_handle_t h)
{
    __port_http_t *port_h = (__port_http_t *) h;
    if (port_h == NULL) {
        return -1;
    }
    return __http_impl.get_status_code(port_h->h);
}

static void __http_close(esp_rmaker_aws_sign_http_handle_t h)
{
    __port_http_t *port_h = (__port_http_t *) h;
    if (port_h == NULL) {
        return;
    }
    __http_impl.cleanup(port_h->h);
    free(port_h);
}

esp_err_t esp_rmaker_aws_sign_port_osal_http_setup(void)
{
    if (!__http_impl_ready) {
        const osal_err_t err = osal_http_impl_setup(&__http_impl);
        if (err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to set up the osal HTTP implementation: %d", (int) err);
            return esp_rmaker_aws_sign_port_err(err);
        }
        __http_impl_ready = true;
    }
    return ESP_OK;
}

const esp_rmaker_aws_sign_http_ops_t esp_rmaker_aws_sign_port_osal_http_ops = {
    .open = __http_open,
    .set_url = __http_set_url,
    .set_method = __http_set_method,
    .set_header = __http_set_header,
    .perform = __http_perform,
    .status_code = __http_status_code,
    .close = __http_close,
};
