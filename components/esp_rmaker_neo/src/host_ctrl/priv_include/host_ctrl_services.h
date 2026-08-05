/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file host_ctrl_services.h
 * @brief Special services for the host control.
 */

#ifndef __HOST_CTRL_SERVICES_H__
#define __HOST_CTRL_SERVICES_H__

/* Includes *******************************************************/

/* Error types */
#include "esp_rmaker_error_types.h"

/* Node includes */
#include "esp_rmaker_node.h"

/* Standard types *******************************************************/

#define RMAKER_HOST_CTRL_SERVICE_COUNT 1

#define RMAKER_HOST_CTRL_SERVICE_PARAM_IN "esp.rmaker.host_ctrl_in"
#define RMAKER_HOST_CTRL_SERVICE_PARAM_OUT "esp.rmaker.host_ctrl_out"

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add a latency service to the node.
 * - Service name: "latency"
 * - Service type: "test_latency"
 * - Service parameters:
 *   - "in_val": int. Used to trigger the write callback. Default: -1.
 *   - "recv_ts": int. When the write callback for 'in_val' is called, this parameter will be set to the timestamp of the write.
 *   - "recv_ts_rem_ms": int. When the write callback for 'in_val' is called, this parameter will be set to the remainder of the timestamp of the write in milliseconds.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_host_ctrl_latency_service_enable(void);

/**
 * @brief Disable the latency service.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_host_ctrl_latency_service_disable(void);

#ifdef __cplusplus
}
#endif

#endif /* __HOST_CTRL_SERVICES_H__ */
