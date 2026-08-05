/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mem_alloc.h
 * @brief Portable heap allocation wrappers.
 */

#ifndef __OSAL_MEM_ALLOC_H__
#define __OSAL_MEM_ALLOC_H__

#ifdef __cplusplus
extern "C"
{
#endif

/* Include the implementation of the memory allocation interface for the platform */
#include "osal_mem_alloc_impl.h"

#ifdef __cplusplus
}
#endif

#endif
