/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file local_endpoints.h
 * @brief Core hooks for the local endpoints service (local control and challenge-response).
 */

#ifndef __SERVICES_LOCAL_ENDPOINTS_H__
#define __SERVICES_LOCAL_ENDPOINTS_H__

#include "esp_rmaker_error_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the endpoint sets the application enabled before this call.
 *
 * Runs from the start task once the credentials are in place and the network is up. An
 * enable that arrives after this call starts at once.
 *
 * @note A start failure is logged, not returned: the node runs without local endpoints.
 *
 * @return ESP_RMAKER_OK
 */
esp_rmaker_error_t esp_rmaker_local_endpoints_on_start(void);

/**
 * @brief Forget the pending requests and the started state. Runs from esp_rmaker_deinit().
 */
void esp_rmaker_local_endpoints_on_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICES_LOCAL_ENDPOINTS_H__ */
