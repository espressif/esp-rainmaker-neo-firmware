/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file linenoise.h
 * @brief The linenoise read hook, for vendored ESP-IDF code on POSIX.
 *
 * The POSIX REPL (esp_console_shim.c) reads stdin itself and has no linenoise, so the hook does nothing.
 */

#pragma once

#include <sys/types.h>

typedef ssize_t (*linenoise_read_bytes_fn)(int, void *, size_t);

static inline void linenoiseSetReadFunction(linenoise_read_bytes_fn read_fn)
{
    (void) read_fn;
}
