/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file prov_helpers_internal.h
 * @brief Provisioning registry internal functions
 */

#ifndef __PROV_INTERNAL_H__
#define __PROV_INTERNAL_H__

/* Includes *******************************************************/

/* Provisioning registry includes. */
#include "prov_helpers.h"

/* Types *******************************************************/

typedef struct {
    osal_event_base_t event_base;
    struct {
        int32_t prov_init;
        int32_t prov_start;
        int32_t prov_end;
    } event_ids;
} __event_loop_info_t;

/* Variables *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

extern __event_loop_info_t __event_loop_info;

/* Function declarations *******************************************************/

/**
 * @brief Initialize the event loop information.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t __prov_event_loop_init(void);


/**
 * @brief Create an endpoint in the provisioning backend.
 *
 * @param[in] endpoint The endpoint to create.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t __prov_backend_endpoint_create(const prov_endpoint_t *endpoint);

/**
 * @brief Register an endpoint in the provisioning backend.
 *
 * @param[in] endpoint The endpoint to register.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t __prov_backend_endpoint_register(const prov_endpoint_t *endpoint);

#ifdef __cplusplus
}
#endif

#endif /* __PROV_INTERNAL_H__ */
