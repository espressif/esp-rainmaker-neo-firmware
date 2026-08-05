/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ca_bundle.h
 * @brief Access to the bundled root CA certificates used for TLS.
 */

#ifndef OSAL_CA_BUNDLE_H
#define OSAL_CA_BUNDLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get pointers to the embedded CA certificate bundle (PEM).
 *
 * @param[out] start  On success, set to the start of the bundle (inclusive).
 * @param[out] end    On success, set to the end of the bundle (exclusive).
 *                    Size in bytes is (end - start). The buffer is null-terminated.
 */
void osal_ca_bundle_get(const unsigned char **start, const unsigned char **end);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_CA_BUNDLE_H */
