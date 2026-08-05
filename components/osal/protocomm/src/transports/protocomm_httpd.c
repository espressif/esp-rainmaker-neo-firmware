/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * SPDX-License-Identifier: Apache-2.0
 * CivetWeb-backed protocomm HTTP transport (mirrors ESP-IDF protocomm_httpd.c).
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "civetweb.h"
#include "esp_err.h"
#include "esp_log.h"
#include "osal_random.h"
#include "osal_semaphore.h"
#include "osal_ticks.h"

#include "protocomm.h"
#include "protocomm_httpd.h"

#include "protocomm_priv.h"

static const char *TAG = "osal_pcomm_httpd";

static protocomm_t *pc_httpd;
static bool pc_ext_httpd_handle_provided;
static uint32_t sock_session_id = PROTOCOMM_NO_SESSION_ID;
static uint32_t cookie_session_id = PROTOCOMM_NO_SESSION_ID;
/* The protocomm security backend tracks a single session at a time, but CivetWeb dispatches
 * requests on multiple worker threads. Serialize the handler so concurrent requests can't
 * clobber the shared session bookkeeping mid-handshake. */
static osal_semaphore_handle_t s_handler_lock;

#define MAX_REQ_BODY_LEN 4096

/* Emit a complete HTTP response for an error status. CivetWeb does NOT synthesize a response
 * from the handler's return value, so a handler that returns without writing leaves the client
 * with a closed connection and no response ("RemoteDisconnected"). Always write something. */
static void __send_status_response(struct mg_connection *conn, int status)
{
    const char *reason = (status == 400) ? "Bad Request"
                         : (status == 404) ? "Not Found"
                         : "Internal Server Error";
    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/octet-stream\r\n"
              "Content-Length: 0\r\n"
              "Connection: close\r\n"
              "\r\n",
              status, reason);
}

static int civet_post_handler(struct mg_connection *conn, void *user_data)
{
    (void) user_data;

    /* All locals that are in scope at the `respond` label are declared up front so the
     * `goto respond` paths never jump over an initializer (-Wjump-misses-init). */
    esp_err_t ret;
    uint8_t *outbuf = NULL;
    char *req_body = NULL;
    const char *ep_name = NULL;
    ssize_t outlen = 0;
    int http_status = 500;
    bool set_cookie = false;
    bool same_session = false;
    uint32_t cur_sock_session_id = (uint32_t)((uintptr_t) conn & 0xFFFFFFFFu);
    const char *cookie_hdr = NULL;
    size_t recv_size = 0;

    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (!ri || !ri->local_uri) {
        __send_status_response(conn, 400);
        return 400;
    }

    if (s_handler_lock != NULL) {
        (void) osal_semaphore_take(s_handler_lock, OSAL_MAX_DELAY);
    }

    if (pc_httpd == NULL) {
        ESP_LOGE(TAG, "Request received with no active protocomm instance");
        goto respond;  /* http_status stays 500 */
    }

    cookie_hdr = mg_get_header(conn, "Cookie");
    if (cookie_hdr != NULL) {
        char cookie_buf[48] = {0};
        snprintf(cookie_buf, sizeof(cookie_buf), "session=%" PRIu32, cookie_session_id);
        if (cookie_session_id != PROTOCOMM_NO_SESSION_ID && strstr(cookie_hdr, cookie_buf) != NULL) {
            sock_session_id = PROTOCOMM_NO_SESSION_ID;
            same_session = true;
        }
    } else if (cur_sock_session_id == sock_session_id && sock_session_id != PROTOCOMM_NO_SESSION_ID) {
        same_session = true;
    }

    if (!same_session) {
        if (cookie_session_id != PROTOCOMM_NO_SESSION_ID) {
            ESP_LOGW(TAG, "Closing session with ID: %" PRIu32, cookie_session_id);
            if (pc_httpd->sec && pc_httpd->sec->close_transport_session) {
                ret = pc_httpd->sec->close_transport_session(pc_httpd->sec_inst, cookie_session_id);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Error closing session");
                }
            }
            cookie_session_id = PROTOCOMM_NO_SESSION_ID;
            sock_session_id = PROTOCOMM_NO_SESSION_ID;
        }
        uint32_t cur_cookie_session_id = osal_random_generate();
        ESP_LOGD(TAG, "Creating new session: %" PRIu32, cur_cookie_session_id);
        if (pc_httpd->sec && pc_httpd->sec->new_transport_session) {
            ret = pc_httpd->sec->new_transport_session(pc_httpd->sec_inst, cur_cookie_session_id);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to launch new session");
                http_status = 500;
                goto respond;
            }
        }
        cookie_session_id = cur_cookie_session_id;
        sock_session_id = cur_sock_session_id;
        set_cookie = true;
    }

    if (ri->content_length <= 0 || ri->content_length > MAX_REQ_BODY_LEN) {
        ESP_LOGE(TAG, "Invalid content length");
        http_status = 400;
        goto respond;
    }

    req_body = (char *) malloc((size_t) ri->content_length);
    if (!req_body) {
        http_status = 500;
        goto respond;
    }

    while (recv_size < (size_t) ri->content_length) {
        int n = mg_read(conn, req_body + recv_size, (size_t) ri->content_length - recv_size);
        if (n <= 0) {
            http_status = 400;
            goto respond;
        }
        recv_size += (size_t) n;
    }

    ep_name = ri->local_uri + 1;

    ret = protocomm_req_handle(pc_httpd, ep_name, cookie_session_id, (uint8_t *) req_body, (ssize_t) recv_size,
                               &outbuf, &outlen);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Data handler failed (ep=%s, session=%" PRIu32 "): %d", ep_name, cookie_session_id, (int) ret);
        http_status = 500;
        goto respond;
    }

    http_status = 200;

respond:
    if (http_status == 200) {
        if (set_cookie) {
            char set_cookie_hdr[64];
            snprintf(set_cookie_hdr, sizeof(set_cookie_hdr), "session=%" PRIu32, cookie_session_id);
            mg_printf(conn,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/octet-stream\r\n"
                      "Set-Cookie: %s\r\n"
                      "Content-Length: %" PRId64 "\r\n"
                      "\r\n",
                      set_cookie_hdr, (int64_t) outlen);
        } else {
            mg_printf(conn,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/octet-stream\r\n"
                      "Content-Length: %" PRId64 "\r\n"
                      "\r\n",
                      (int64_t) outlen);
        }
        if (outlen > 0 && outbuf != NULL) {
            mg_write(conn, outbuf, (size_t) outlen);
        }
    } else {
        __send_status_response(conn, http_status);
    }

    free(outbuf);
    free(req_body);
    if (s_handler_lock != NULL) {
        (void) osal_semaphore_give(s_handler_lock);
    }
    return http_status;
}

static esp_err_t protocomm_httpd_add_endpoint(const char *ep_name, protocomm_req_handler_t req_handler,
        void *priv_data)
{
    (void) req_handler;
    (void) priv_data;

    if (pc_httpd == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    struct mg_context *ctx = (struct mg_context *) pc_httpd->priv;
    if (!ctx) {
        return ESP_ERR_INVALID_STATE;
    }

    char *ep_uri = calloc(1, strlen(ep_name) + 2);
    if (!ep_uri) {
        return ESP_ERR_NO_MEM;
    }
    sprintf(ep_uri, "/%s", ep_name);

    mg_set_request_handler(ctx, ep_uri, civet_post_handler, NULL);
    free(ep_uri);
    return ESP_OK;
}

static esp_err_t protocomm_httpd_remove_endpoint(const char *ep_name)
{
    if (pc_httpd == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    struct mg_context *ctx = (struct mg_context *) pc_httpd->priv;
    if (!ctx) {
        return ESP_ERR_INVALID_STATE;
    }

    char *ep_uri = calloc(1, strlen(ep_name) + 2);
    if (!ep_uri) {
        return ESP_ERR_NO_MEM;
    }
    sprintf(ep_uri, "/%s", ep_name);
    mg_set_request_handler(ctx, ep_uri, NULL, NULL);
    free(ep_uri);
    return ESP_OK;
}

esp_err_t protocomm_httpd_start(protocomm_t *pc, const protocomm_httpd_config_t *config)
{
    if (!pc || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (pc_httpd) {
        if (pc == pc_httpd) {
            return ESP_ERR_INVALID_STATE;
        }
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (config->ext_handle_provided) {
        if (config->data.handle) {
            pc->priv = config->data.handle;
            pc_ext_httpd_handle_provided = true;
        } else {
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        const protocomm_http_server_config_t *sc = &config->data.config;
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%u", (unsigned) sc->port);
        const char *opts[] = {
            "listening_ports", port_str,
            "num_threads", "4",
            NULL,
        };
        struct mg_context *ctx = mg_start(NULL, NULL, opts);
        if (!ctx) {
            ESP_LOGE(TAG, "Failed to start CivetWeb");
            return ESP_FAIL;
        }
        pc->priv = ctx;
        pc_ext_httpd_handle_provided = false;
    }

    /* Create the serialization lock once and reuse it across start/stop cycles. Endpoint
     * handlers are only registered after this returns, so no request can race the creation.
     * If creation fails, the handler degrades to unserialized but still responds correctly. */
    if (s_handler_lock == NULL) {
        s_handler_lock = osal_semaphore_create_mutex();
        if (s_handler_lock == NULL) {
            ESP_LOGW(TAG, "Failed to create handler lock; requests will not be serialized");
        }
    }

    pc->add_endpoint = protocomm_httpd_add_endpoint;
    pc->remove_endpoint = protocomm_httpd_remove_endpoint;
    pc_httpd = pc;
    cookie_session_id = PROTOCOMM_NO_SESSION_ID;
    sock_session_id = PROTOCOMM_NO_SESSION_ID;
    return ESP_OK;
}

esp_err_t protocomm_httpd_stop(protocomm_t *pc)
{
    if ((pc != NULL) && (pc == pc_httpd)) {
        if (!pc_ext_httpd_handle_provided) {
            struct mg_context *ctx = (struct mg_context *) pc_httpd->priv;
            if (ctx) {
                mg_stop(ctx);
            }
        } else {
            pc_ext_httpd_handle_provided = false;
        }
        pc_httpd->priv = NULL;
        pc_httpd = NULL;
        cookie_session_id = PROTOCOMM_NO_SESSION_ID;
        sock_session_id = PROTOCOMM_NO_SESSION_ID;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}
