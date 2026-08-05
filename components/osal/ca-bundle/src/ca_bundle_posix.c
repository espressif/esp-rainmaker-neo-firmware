/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_ca_bundle.h"

/* Symbols from the embedded cacrt_all.pem.
 * Use asm() so the linker symbol matches the assembler output on all platforms (e.g. macOS). */
extern const unsigned char _binary_cacrt_all_pem_start[] asm("_binary_cacrt_all_pem_start");
extern const unsigned char _binary_cacrt_all_pem_end[] asm("_binary_cacrt_all_pem_end");

void osal_ca_bundle_get(const unsigned char **start, const unsigned char **end)
{
    if (start != NULL) {
        *start = _binary_cacrt_all_pem_start;
    }
    if (end != NULL) {
        *end = _binary_cacrt_all_pem_end;
    }
}
