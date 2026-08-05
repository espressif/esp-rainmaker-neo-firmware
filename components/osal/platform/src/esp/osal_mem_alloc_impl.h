/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mem_alloc_impl.h
 * @brief Implementation of the memory allocation interface for ESP.
 */

#ifndef __OSAL_MEM_ALLOC_IMPL_H__
#define __OSAL_MEM_ALLOC_IMPL_H__

#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <sdkconfig.h>

/* Use SPIRAM if enabled */
#if (CONFIG_SPIRAM_SUPPORT && (CONFIG_SPIRAM_USE_CAPS_ALLOC || CONFIG_SPIRAM_USE_MALLOC))
#define OSAL_MALLOC_EXTRAM(size)         heap_caps_malloc_prefer(size, 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL)
#define OSAL_CALLOC_EXTRAM(num, size)    heap_caps_calloc_prefer(num, size, 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL)
#define OSAL_REALLOC_EXTRAM(ptr, size)   heap_caps_realloc_prefer(ptr, size, 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL)
#else
#define OSAL_MALLOC_EXTRAM(size)         malloc(size)
#define OSAL_CALLOC_EXTRAM(num, size)    calloc(num, size)
#define OSAL_REALLOC_EXTRAM(ptr, size)   realloc(ptr, size)
#endif

/**
 * @brief strdup() variant that places the copy in external RAM (PSRAM) when available.
 *
 * @param[in] s String to duplicate (may be NULL).
 *
 * @return Pointer to the duplicated string (free with free()), or NULL on allocation failure or NULL input.
 */
static inline char *osal_strdup_extram(const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char *copy = (char *) OSAL_MALLOC_EXTRAM(len);
    if (copy != NULL) {
        memcpy(copy, s, len);
    }
    return copy;
}
#define OSAL_STRDUP_EXTRAM(s)            osal_strdup_extram(s)

#endif
