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

/* Event loop includes */
#include "osal_event_loop.h"

/* Preprocessor definitions *******************************************************/

/** Every module that can add an endpoint is compiled into this component: claim and
 * chal_resp. Raise this when more land. */
#define PROV_MAX_ENDPOINTS 2

/** One capability per feature: claim and ch_resp. Raise this when more land. */
#define PROV_MAX_CAPABILITIES 2

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
 * @brief Get the endpoint in a registry slot.
 *
 * @note Slot order is add order, which decides the BLE characteristic UUIDs.
 *
 * @param[in] slot The slot, below ::PROV_MAX_ENDPOINTS.
 *
 * @return The endpoint, or NULL when the slot is free or out of range.
 */
const prov_endpoint_t *__prov_endpoint_get(size_t slot);

/**
 * @brief Collect every capability declared under a label.
 *
 * @param[in] label The label to collect for.
 * @param[out] capabilities Filled with the capability strings, in add order.
 * @param[in] max_capabilities The size of `capabilities`.
 *
 * @return The number of capabilities written.
 */
size_t __prov_app_info_collect(prov_app_label_t label, const char **capabilities, size_t max_capabilities);

/**
 * @brief Set an app info entry in the provisioning backend.
 *
 * @note Call once per label: the backend appends rather than replaces, so a repeated label
 *       yields a duplicate key and loses capabilities.
 *
 * @param[in] label The app info label.
 * @param[in] version The app info version.
 * @param[in] capabilities The capabilities to advertise under `label`.
 * @param[in] total_capabilities The number of capabilities.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t __prov_backend_app_info_set(const char *label, const char *version,
                                       const char **capabilities, size_t total_capabilities);

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
