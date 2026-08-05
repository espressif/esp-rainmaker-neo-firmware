/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file credentials_provider.c
 * @brief Implementation of the credentials provider for the ESP RainMaker Neo SDK.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "esp_rmaker_credentials_provider.h"

/* Platform common includes */
#include "osal_mem_alloc.h"

/* Public function definitions ****************************************************/

void esp_rmaker_credentials_free_credential(esp_rmaker_credential_t *p_credential)
{
    if (!p_credential) {
        return;
    }
    free(p_credential->credential);
    p_credential->credential = NULL;
    p_credential->len = 0;
}
