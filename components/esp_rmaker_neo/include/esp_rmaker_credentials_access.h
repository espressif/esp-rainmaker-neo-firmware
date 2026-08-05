/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_credentials_access.h
 * @brief Public APIs for accessing the credentials provider
 */

#ifndef __ESP_RMAKER_CREDENTIALS_ACCESS_H__
#define __ESP_RMAKER_CREDENTIALS_ACCESS_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stddef.h>

/* Error types */
#include "esp_rmaker_error_types.h"

/* Credentials provider includes */
#include "esp_rmaker_credentials_provider.h"

/* Public functions **************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Override some/all of the credentials providers.
 * By default, the credentials are read from the factory partition.
 * @note This function should be called before ANY initialization functions are called for the overrides to take effect:
 * - esp_rmaker_pre_prov_init()
 * - esp_rmaker_node_init()
 *
 * @param[in] p_credentials_providers The credentials providers. Set a provider to NULL to use the default credentials provider for that provider.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_credentials_provider_override(const esp_rmaker_credentials_providers_t *p_credentials_providers);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_CREDENTIALS_ACCESS_H__ */
