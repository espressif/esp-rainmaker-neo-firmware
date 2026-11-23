/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file local_ctrl/prov.h
 * @brief Local control endpoints on the provisioning service.
 *
 * Serves get_params, set_params and get_config on the provisioning protocomm instance, next
 * to ch_resp, so a client can control the node over the provisioning session. The handlers
 * are the ones in local_ctrl/endpoints.h; this module only attaches them to the registry.
 */

#ifndef __LOCAL_CTRL_PROV_H__
#define __LOCAL_CTRL_PROV_H__

/* Includes *******************************************************/

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Preprocessor definitions *******************************************************/

/** The capability advertised under `rmaker_extra`. Frozen wire value. */
#define RMAKER_PROV_LOCAL_CTRL_CAPABILITY "local_ctrl"

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add the local control endpoints to the provisioning registry.
 *
 * @note The endpoints serve the node that esp_rmaker_node_init() registered. Until a node
 *       exists, they answer with status `Fail`.
 *
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_prov_local_ctrl_init(void);

/**
 * @brief Remove the local control endpoints from the provisioning registry.
 *
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_prov_local_ctrl_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __LOCAL_CTRL_PROV_H__ */
