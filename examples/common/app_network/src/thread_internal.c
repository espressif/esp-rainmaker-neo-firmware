/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file thread_internal.c
 * @brief Internal functions for the Thread network.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "app_network_neo.h"

/* OpenThread includes */
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "openthread/instance.h"
#include "openthread/thread.h"

/* Public function definitions **********************************************************/

/* Reset Wi-Fi credentials */
osal_err_t app_network_reset_credentials(void)
{
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *instance = esp_openthread_get_instance();
    bool enabled = otThreadGetDeviceRole(instance) != OT_DEVICE_ROLE_DISABLED;
    if (enabled) {
        (void)otThreadSetEnabled(instance, false);
    }
    (void)otInstanceErasePersistentInfo(instance);
    (void)otThreadSetEnabled(instance, enabled);
    esp_openthread_lock_release();
    return OSAL_ERR_OK;
}
