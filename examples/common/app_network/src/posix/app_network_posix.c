/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_network_posix.c
 * @brief POSIX network stubs. There is no real network stack on the host, so
 *        init/provision are no-ops and there are no credentials to reset.
 */

/* Declarations */
#include "app_network_neo.h"

/* Platform includes */
#include "osal_log.h"

static const char *TAG = "app_network";

void app_network_init(void)
{
    /* No network stack on POSIX. */
}

osal_err_t app_network_reset_credentials(void)
{
    OSAL_LOGW(TAG, "Network reset called (no-op on POSIX)");
    return OSAL_ERR_OK;
}

osal_err_t app_network_provision(uint16_t device_type, uint8_t device_subtype)
{
    (void) device_type;
    (void) device_subtype;
    return OSAL_ERR_OK;
}

void app_network_set_prov_hooks(const app_network_prov_hooks_t *hooks)
{
    /* No provisioning on POSIX, so lifecycle hooks are never invoked. */
    (void) hooks;
}
