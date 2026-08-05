/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_credentials_provider.c
 * @brief Implementation of the credentials provider for the ESP RainMaker Neo SDK.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "esp_rmaker_flow.h"

/* Private credentials provider includes */
#include "esp_rmaker_credentials.h"

/* Public function definitions ****************************************************/

esp_rmaker_error_t esp_rmaker_credentials_provider_override(const esp_rmaker_credentials_providers_t *p_credentials_providers)
{
    return esp_rmaker_credentials_override(p_credentials_providers);
}
