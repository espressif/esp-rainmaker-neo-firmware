/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mqtt_util.c
 * @brief common utility functions for MQTT common
 *
 */

#include "osal_mqtt_util.h"

#include <string.h>

bool osal_mqtt_match_topic( const char *topic,
                            size_t topic_len,
                            const char *filter,
                            size_t filter_len )
{
    const char *f = filter, *t = topic;
    while ((f - filter) < filter_len && (t - topic) < topic_len) {
        // wildcard '#'
        if (*f == '#') {
            // must be last in filter, and match rest
            return ( (f - filter) == filter_len - 1 );
        }
        // wildcard '+'
        if (*f == '+') {
            // skip this level in both
            while (*t && *t != '/') {
                t++;
            }
            f++;
            if (*f == '/') {
                f++;
            }
            if (*t == '/') {
                t++;
            }
            continue;
        }
        // normal characters: match until '/'
        const char *fn = f, *tn = t;
        while ((fn - filter) < filter_len && *fn != '/') {
            fn++;
        }
        while ((tn - topic) < topic_len && *tn != '/') {
            tn++;
        }
        if ((fn - f) != (tn - t) ||
                strncmp(f, t, fn - f) != 0) {
            return false;
        }
        f = (fn - filter) < filter_len ? fn + 1 : fn;
        t = (tn - topic) < topic_len ? tn + 1 : tn;
    }
    // complete match only if both strings ended or filter ends with '#'
    if (*f == '#' && (f - filter) == filter_len - 1) {
        return true;
    }
    return ( (f - filter) == filter_len && (t - topic) == topic_len );
}
