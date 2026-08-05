/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file FreeRTOS.h
 * @brief Empty FreeRTOS compatibility header for vendored ESP-IDF sources built on POSIX.
 *
 * Many ESP-IDF components unconditionally `#include <freertos/FreeRTOS.h>` (and task.h / queue.h) at
 * the top of a translation unit even when the code path compiled on POSIX uses no FreeRTOS symbols.
 * These empty headers satisfy the include so such sources compile unchanged. They are intentionally
 * lowercase (`freertos/`) to match the include casing used by upstream code on case-sensitive
 * filesystems.
 *
 * If a POSIX-built path actually needs FreeRTOS symbols, add the relevant shim here rather than
 * pulling in a real FreeRTOS.
 */
#pragma once
