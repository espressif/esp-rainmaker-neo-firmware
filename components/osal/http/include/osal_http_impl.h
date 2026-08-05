/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_http_impl.h
 * @brief Setup of the platform HTTP client vtable (::osal_http_impl_t).
 */

#ifndef OSAL_HTTP_IMPL_H
#define OSAL_HTTP_IMPL_H

/* Common includes */
#include "osal_http.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief This function initializes HTTP implementation layer with all the default functions.
 *
 * All future HTTP function calls should then be made with this impl, e.g., http_impl.init().
 *
 * @param[out] http_impl Pointer to an allocated HTTP implementation structure.
 *
 * @return OSAL_ERR_OK on success.
 * @return An error code in case of any error.
 */
osal_err_t osal_http_impl_setup( osal_http_impl_t *http_impl );

#ifdef __cplusplus
}
#endif

#endif /* OSAL_HTTP_IMPL_H */
