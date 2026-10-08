/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file host_ctrl_echo_endpoint.c
 * @brief Custom local control endpoint that echoes each request
 */

/* Includes *******************************************************/

/* Declarations */
#include "host_ctrl_services.h"

/* Standard includes */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Platform includes */
#include "osal_event_loop.h"
#include "osal_log.h"

/* RMNG includes */
#include "esp_rmaker_event_loop.h"
#include "esp_rmaker_standard_services.h"

/* Constants *******************************************************/

static const char *TAG = "rmng_hc_svc_echo_ep";

/* Variables *******************************************************/

static bool __echo_endpoint_enabled = false;

/* Private functions *******************************************************/

static int __echo_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf,
                          ssize_t *outlen, void *priv_data)
{
    (void) session_id;
    (void) priv_data;
    *outbuf = malloc(inlen > 0 ? (size_t) inlen : 1);
    if (*outbuf == NULL) {
        return -1;
    }
    if (inlen > 0) {
        memcpy(*outbuf, inbuf, (size_t) inlen);
    }
    *outlen = inlen;
    return 0;
}

static void __local_ctrl_started_handler(void *arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void) arg;
    (void) event_base;
    (void) event_id;
    (void) event_data;
    esp_rmaker_error_t err = esp_rmaker_local_ctrl_add_endpoint(RMAKER_HOST_CTRL_ECHO_ENDPOINT_NAME, __echo_handler,
                             NULL);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to add endpoint %s: %d", RMAKER_HOST_CTRL_ECHO_ENDPOINT_NAME, (int) err);
    }
}

/* Public functions *******************************************************/

esp_rmaker_error_t esp_rmaker_host_ctrl_echo_endpoint_service_enable(void)
{
    if (__echo_endpoint_enabled) {
        return ESP_RMAKER_OK;
    }
    if (osal_event_handler_register(RMAKER_EVENT, RMAKER_EVENT_LOCAL_CTRL_STARTED, __local_ctrl_started_handler,
                                    NULL) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register local control started handler");
        return ESP_RMAKER_FAIL;
    }
    __echo_endpoint_enabled = true;

    /* Local control can already be running if its service was added first */
    (void) esp_rmaker_local_ctrl_add_endpoint(RMAKER_HOST_CTRL_ECHO_ENDPOINT_NAME, __echo_handler, NULL);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_host_ctrl_echo_endpoint_service_disable(void)
{
    if (!__echo_endpoint_enabled) {
        return ESP_RMAKER_OK;
    }
    (void) osal_event_handler_unregister(RMAKER_EVENT, RMAKER_EVENT_LOCAL_CTRL_STARTED, __local_ctrl_started_handler);
    (void) esp_rmaker_local_ctrl_remove_endpoint(RMAKER_HOST_CTRL_ECHO_ENDPOINT_NAME);
    __echo_endpoint_enabled = false;
    return ESP_RMAKER_OK;
}
