/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mem_alloc_impl.h
 * @brief Implementation of the memory allocation interface for POSIX.
 */

#ifndef __OSAL_MEM_ALLOC_IMPL_H__
#define __OSAL_MEM_ALLOC_IMPL_H__

#include "osal_mem_alloc.h"
#include <stdlib.h>
#include <string.h>

#define OSAL_MALLOC_EXTRAM(size)           malloc(size)
#define OSAL_CALLOC_EXTRAM(num, size)      calloc(num, size)
#define OSAL_REALLOC_EXTRAM(ptr, size)     realloc(ptr, size)
#define OSAL_STRDUP_EXTRAM(s)              strdup(s)

#endif
