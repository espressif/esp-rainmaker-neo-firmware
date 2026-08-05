/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file log_posix.c
 * @brief POSIX logging implementation.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "osal_log_impl.h"

void posix_log_buffer_hex(const char *tag, const void *buf, size_t len, osal_log_level_t level)
{
    char local_buf[len * 3 + 1];
    for (size_t i = 0; i < len; i++) {
        snprintf(local_buf + i * 3, 4, "%.2X ", ((const uint8_t *)buf)[i]);
    }
    local_buf[len * 3] = '\0';
    LOG_INTERNAL(level, tag, "%s", local_buf);
}
