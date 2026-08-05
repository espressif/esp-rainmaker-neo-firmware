/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __OSAL_LOG_IMPL_H__
#define __OSAL_LOG_IMPL_H__

/**
 * @file osal_log_impl.h
 * @brief Platform common log implementation for POSIX.
 */

/* Host build: map to fprintf */

#include <stdio.h>
#include <time.h>
#include "osal_log_level.h"

// Color codes
#define COLOR_ERROR   "\033[31m"
#define COLOR_WARN    "\033[33m"
#define COLOR_INFO    "\033[32m"
#define COLOR_DEBUG   "\033[34m"
#define COLOR_VERBOSE "\033[35m"
#define COLOR_RESET   "\033[0m"

#define LOG_INTERNAL(level, tag, fmt, ...) LOG_IF_LEVEL_LOCAL(level, tag, fmt, ##__VA_ARGS__)

#define LOG_IF_LEVEL_LOCAL(level, tag, fmt, ...) \
    do { \
        if (level <= LOG_MAX_LEVEL) { LOG_LEVEL_LOCAL(level, tag, fmt, ##__VA_ARGS__); } \
    } while (0)

#define LOG_LEVEL_LOCAL(level, tag, fmt, ...) \
    do { \
        if (level == OSAL_LOG_LEVEL_ERROR) { fprintf(stderr, COLOR_ERROR "E (%ld)[%s] " fmt COLOR_RESET "\n", (unsigned long) clock(), tag, ##__VA_ARGS__); } \
        else if (level == OSAL_LOG_LEVEL_WARN) { fprintf(stdout, COLOR_WARN "W (%ld)[%s] " fmt COLOR_RESET "\n", (unsigned long) clock(), tag, ##__VA_ARGS__); } \
        else if (level == OSAL_LOG_LEVEL_INFO) { fprintf(stdout, COLOR_INFO "I (%ld)[%s] " fmt COLOR_RESET "\n", (unsigned long) clock(), tag, ##__VA_ARGS__); } \
        else if (level == OSAL_LOG_LEVEL_DEBUG) { fprintf(stdout, COLOR_DEBUG "D (%ld)[%s] " fmt COLOR_RESET "\n", (unsigned long) clock(), tag, ##__VA_ARGS__); } \
        else if (level == OSAL_LOG_LEVEL_VERBOSE) { fprintf(stdout, COLOR_VERBOSE "V (%ld)[%s] " fmt COLOR_RESET "\n", (unsigned long) clock(), tag, ##__VA_ARGS__); } \
        else { fprintf(stdout, COLOR_ERROR "? (%ld)[%s] " fmt COLOR_RESET "\n", (unsigned long) clock(), tag, ##__VA_ARGS__); } \
    } while (0)

void posix_log_buffer_hex(const char *tag, const void *buf, size_t len, osal_log_level_t level);
#define LOG_BUFFER_HEX_INTERNAL(tag, buf, len, level) posix_log_buffer_hex(tag, buf, len, level)
#endif /* __OSAL_LOG_IMPL_H__ */
