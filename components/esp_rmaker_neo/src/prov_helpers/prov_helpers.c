/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file prov_helpers.c
 * @brief Common functions for the provisioning registry.
 */

/* Includes *******************************************************/

/* Declarations */
#include "prov_helpers_internal.h"

/* Standard includes */
#include <string.h>
#include <stdatomic.h>

/* Platform common includes */
#include "osal_mem_alloc.h"
#include "osal_queue.h"

/* Variables *******************************************************/

__event_loop_info_t __event_loop_info;

/* Public function definitions *******************************************************/

osal_err_t prov_get_registration_info(prov_registration_info_t *registration_info)
{
    if (registration_info == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    if (__prov_event_loop_init() != OSAL_ERR_OK) {
        return OSAL_ERR_FAIL;
    }

    registration_info->event_base = __event_loop_info.event_base;
    registration_info->event_ids.prov_init = __event_loop_info.event_ids.prov_init;
    registration_info->event_ids.prov_start = __event_loop_info.event_ids.prov_start;
    registration_info->event_ids.prov_end = __event_loop_info.event_ids.prov_end;
    registration_info->actions.endpoint_create = __prov_backend_endpoint_create;
    registration_info->actions.endpoint_register = __prov_backend_endpoint_register;
    return OSAL_ERR_OK;
}
