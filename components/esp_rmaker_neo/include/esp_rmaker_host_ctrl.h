/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_host_ctrl.h
 * @brief Remote control interface.
 */

#ifndef __ESP_RMAKER_HOST_CTRL_H__
#define __ESP_RMAKER_HOST_CTRL_H__

#include "esp_rmaker_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the remote control interface, and the SDK.
 * @note This initializes the SDK and maintains its state. You should not initialize the SDK separately.
 * If you want to make changes to the state, then manipulate it through the remote control interface.
 *
 * @param[in] config SDK configuration, forwarded to esp_rmaker_node_init(). Must not be NULL;
 *                   the structure is copied, so it need not outlive the call.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if config is NULL.
 * @return ESP_RMAKER_FAIL otherwise.
 */
esp_rmaker_error_t esp_rmaker_host_ctrl_init(esp_rmaker_config_t *config);

/**
 * @brief Deinitialize the remote control interface, and the SDK.
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_FAIL otherwise.
 */
esp_rmaker_error_t esp_rmaker_host_ctrl_deinit(void);

/**
 * @brief Start the remote control interface.
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_FAIL otherwise.
 */
esp_rmaker_error_t esp_rmaker_host_ctrl_start(void);

/**
 * @brief Stop the remote control interface.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_STATE if the remote control task is not running.
 */
esp_rmaker_error_t esp_rmaker_host_ctrl_stop(void);

/**
 * @brief Kill the running remote control instance.
 * @note This function is platform-specific.
 */
void esp_rmaker_host_ctrl_kill(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_HOST_CTRL_H__ */
