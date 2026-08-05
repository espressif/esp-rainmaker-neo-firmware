/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sysctrl_esp.c
 * @brief ESP-IDF system control implementation.
 */

/* Includes **************************************************************/

/* Declarations */
#include "osal_sysctrl.h"

/* ESP-IDF */
#include "esp_system.h"

/* Public function definitions **********************************************************/

osal_err_t osal_sysctrl_reboot(void)
{
    esp_restart();
    return OSAL_ERR_OK;
}
