/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file prov_helpers_posix.c
 * @brief POSIX implementation of the provisioning backend.
 *
 * Network provisioning is not supported on POSIX: the host already has
 * network connectivity, and node credentials come from the factory NVS
 * (nvs_persistent). These hooks satisfy the provisioning-backend interface
 * with no-op successes so the shared bring-up flow proceeds unchanged.
 */

/* Includes *******************************************************/

/* Public function declaration include. */
#include "prov_helpers_internal.h"

/* Platform common includes */
#include "osal_log.h"

/* Preprocessor definitions *******************************************************/

OSAL_EVENT_DEFINE_BASE(PROV_BACKEND_POSIX_EVENT_BASE);

/* Variables *******************************************************/

static const char *TAG = "rmng_prov_posix";

/* Function definitions *******************************************************/

osal_err_t __prov_event_loop_init(void)
{
    /* Synthetic IDs on a local event base; no POSIX provisioning transport
     * exists to emit them. */
    __event_loop_info.event_base = PROV_BACKEND_POSIX_EVENT_BASE;
    __event_loop_info.event_ids.prov_init = 0;
    __event_loop_info.event_ids.prov_start = 1;
    __event_loop_info.event_ids.prov_end = 2;
    return OSAL_ERR_OK;
}

osal_err_t __prov_backend_endpoint_create(const prov_endpoint_t *endpoint)
{
    /* No provisioning transport on POSIX; accept so bring-up proceeds. */
    OSAL_LOGI(TAG, "Creating endpoint: %s", endpoint->name);
    return OSAL_ERR_OK;
}

osal_err_t __prov_backend_endpoint_register(const prov_endpoint_t *endpoint)
{
    /* No provisioning transport on POSIX; accept so bring-up proceeds. */
    OSAL_LOGI(TAG, "Registering endpoint: %s", endpoint->name);
    return OSAL_ERR_OK;
}
