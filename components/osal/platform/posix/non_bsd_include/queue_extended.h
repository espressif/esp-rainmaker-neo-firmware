/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file queue_extended.h
 * @brief Extra sys/queue.h macros missing on non-BSD libc implementations.
 */

#ifndef __QUEUE_EXTENDED_H__
#define __QUEUE_EXTENDED_H__

#include <sys/queue.h>

#define SLIST_FOREACH_SAFE(var, head, field, tvar)           \
    for ((var) = SLIST_FIRST((head));                        \
        (var) && ((tvar) = SLIST_NEXT((var), field), 1);    \
        (var) = (tvar))

#endif /* __QUEUE_EXTENDED_H__ */
