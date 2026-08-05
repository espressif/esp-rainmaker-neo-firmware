/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file host_ctrl_latency.c
 * @brief Service used to measure latency for the data model
 */

/* Includes *******************************************************/

/* Declarations */
#include "host_ctrl_services.h"

/* Standard includes */
#include <stddef.h>
#include <inttypes.h>
#include <string.h>

/* Platform includes */
#include "osal_log.h"
#include "osal_time.h"

/* RMNG includes */
#include "esp_rmaker_node.h"
#include "esp_rmaker_data_model.h"

/* Constants *******************************************************/

static const char *TAG = "rmng_hc_svc_latency";

static const char *service_id = "latency";
static const char *service_type = "test_latency";
static const char *in_val_param_id = "in_val";
static const char *recv_ts_param_id = "recv_ts";
static const char *recv_ts_rem_ms_param_id = "recv_ts_rem_ms";

/* Variables *******************************************************/

static esp_rmaker_device_t *__latency_service = NULL;

/* Private function declarations *******************************************************/

/**
 * @brief Write callback for the latency service.
 *
 * @param[in] device Pointer to the device.
 * @param[in] param Pointer to the parameter.
 * @param[in] val Updated value.
 * @param[in] priv_data Pointer to the private data.
 * @param[in] ctx Context associated with the request.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __latency_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx);


/* Private function definitions *******************************************************/

static esp_rmaker_error_t __latency_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (strcmp(esp_rmaker_param_get_id(param), in_val_param_id) == 0) {
        uint64_t time_ms = osal_get_time_ms(NULL);
        int32_t time_s = time_ms / 1000;
        int32_t time_ms_remainder = time_ms % 1000;
        OSAL_LOGI(TAG, "Latency timestamp as %" PRId32 ".%03" PRId32 " s", time_s, time_ms_remainder);
        esp_rmaker_param_t *recv_ts_param = esp_rmaker_device_get_param_by_id(device, recv_ts_param_id);
        esp_rmaker_param_t *recv_ts_rem_ms_param = esp_rmaker_device_get_param_by_id(device, recv_ts_rem_ms_param_id);

        if (!recv_ts_param || !recv_ts_rem_ms_param) {
            OSAL_LOGE(TAG, "Failed to get '%s' or '%s' parameter", recv_ts_param_id, recv_ts_rem_ms_param_id);
            return ESP_RMAKER_FAIL;
        }

        esp_rmaker_param_update(recv_ts_param, esp_rmaker_int(time_s));
        esp_rmaker_param_update(recv_ts_rem_ms_param, esp_rmaker_int(time_ms_remainder));
        esp_rmaker_param_update(param, val);
    }
    return ESP_RMAKER_OK;
}

/* Public function definitions *******************************************************/

esp_rmaker_error_t esp_rmaker_host_ctrl_latency_service_enable(void)
{
    const esp_rmaker_node_t *node = esp_rmaker_get_node();
    if (!node) {
        OSAL_LOGE(TAG, "Node not found");
        return ESP_RMAKER_INVALID_STATE;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Make the service */
    __latency_service = esp_rmaker_service_create(service_id, service_type, NULL);
    if (!__latency_service) {
        OSAL_LOGE(TAG, "Failed to create latency service");
        err = ESP_RMAKER_NO_MEM;
        goto add_service_latency_err;
    }

    /* Make the 'in_val' parameter */
    esp_rmaker_param_t *param = esp_rmaker_param_create(in_val_param_id, RMAKER_HOST_CTRL_SERVICE_PARAM_IN, esp_rmaker_int(-1), PROP_FLAG_READ | PROP_FLAG_WRITE);
    if (!param) {
        OSAL_LOGE(TAG, "Failed to create '%s' parameter", in_val_param_id);
        err = ESP_RMAKER_NO_MEM;
        goto add_service_latency_err;
    }

    /* Add the parameter to the device */
    if (esp_rmaker_device_add_param(__latency_service, param) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add '%s' parameter to latency device", in_val_param_id);
        err = ESP_RMAKER_FAIL;
        goto add_service_latency_err;
    }

    /* Make the 'recv_ts' parameter */
    param = esp_rmaker_param_create(recv_ts_param_id, RMAKER_HOST_CTRL_SERVICE_PARAM_OUT, esp_rmaker_int(-1), PROP_FLAG_READ);
    if (!param) {
        OSAL_LOGE(TAG, "Failed to create '%s' parameter", recv_ts_param_id);
        err = ESP_RMAKER_NO_MEM;
        goto add_service_latency_err;
    }
    /* Add the parameter to the device */
    if (esp_rmaker_device_add_param(__latency_service, param) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add '%s' parameter to latency device", recv_ts_param_id);
        err = ESP_RMAKER_FAIL;
        goto add_service_latency_err;
    }

    /* Make the 'recv_ts_rem_ms' parameter */
    param = esp_rmaker_param_create(recv_ts_rem_ms_param_id, RMAKER_HOST_CTRL_SERVICE_PARAM_OUT, esp_rmaker_int(-1), PROP_FLAG_READ);
    if (!param) {
        OSAL_LOGE(TAG, "Failed to create '%s' parameter", recv_ts_rem_ms_param_id);
        err = ESP_RMAKER_NO_MEM;
        goto add_service_latency_err;
    }
    /* Add the parameter to the device */
    if (esp_rmaker_device_add_param(__latency_service, param) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add '%s' parameter to latency device", recv_ts_rem_ms_param_id);
        err = ESP_RMAKER_FAIL;
        goto add_service_latency_err;
    }

    /* Add the write callback to the device */
    if (esp_rmaker_device_add_cb(__latency_service, __latency_write_cb, NULL) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add write callback to latency device");
        err = ESP_RMAKER_FAIL;
        goto add_service_latency_err;
    }

    /* Add the device to the node */
    if (esp_rmaker_node_add_device(node, __latency_service) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add latency device to node");
        err = ESP_RMAKER_FAIL;
        goto add_service_latency_err;
    }

    return ESP_RMAKER_OK;

add_service_latency_err:
    if (__latency_service) {
        esp_rmaker_device_delete(__latency_service);
    }
    return err;
}

esp_rmaker_error_t esp_rmaker_host_ctrl_latency_service_disable(void)
{
    if (!__latency_service) {
        OSAL_LOGI(TAG, "Latency service not enabled; skipping disable");
        return ESP_RMAKER_OK;
    }

    const esp_rmaker_node_t *node = esp_rmaker_get_node();
    if (!node) {
        OSAL_LOGE(TAG, "Node not found");
        return ESP_RMAKER_INVALID_STATE;
    }
    if (esp_rmaker_node_remove_device(node, __latency_service) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to remove latency device from node");
        return ESP_RMAKER_FAIL;
    }
    if (esp_rmaker_device_delete(__latency_service) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to delete latency device");
        return ESP_RMAKER_FAIL;
    }
    __latency_service = NULL;
    return ESP_RMAKER_OK;
}
