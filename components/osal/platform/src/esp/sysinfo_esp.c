/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sysinfo_esp.c
 * @brief ESP-IDF system information implementation.
 */

/* Includes **************************************************************/

/* Declarations */
#include "osal_sysinfo.h"

/* Standard C headers */
#include <stddef.h>

/* ESP-IDF */
#include "esp_app_desc.h"

/* Configuration */
#include "sdkconfig.h"

/* Public function definitions **********************************************************/

const char *osal_sysinfo_get_fw_version(void)
{
    /* Get app description */
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (!app_desc) {
        return NULL;
    }

    /* Return the firmware version */
    return app_desc->version;
}

const char *osal_sysinfo_get_project_name(void)
{
    /* Get app description */
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (!app_desc) {
        return NULL;
    }

    /* Return the project name */
    return app_desc->project_name;
}

const char *osal_sysinfo_get_platform_name(void)
{
#ifdef CONFIG_IDF_TARGET
    return CONFIG_IDF_TARGET;
#else
    /* Unreachable in a normal ESP-IDF build; keeps the getter's never-NULL contract. */
    return "esp-unknown";
#endif
}
