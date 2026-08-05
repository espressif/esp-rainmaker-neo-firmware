/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mqtt_util.h
 * @brief MQTT utility helpers (topic matching and comparison).
 */

#ifndef OSAL_MQTT_UTIL_H
#define OSAL_MQTT_UTIL_H

/* Standard includes. */
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Match filter with wildcards and a given topic.
 *
 * @param[in] topic      The topic to test.
 * @param[in] topic_len  Length of @p topic in bytes, excluding any NUL terminator.
 * @param[in] filter     The topic filter to test against. May contain the MQTT single-level
 *                       (`+`) and multi-level (`#`) wildcards; `#` must be the last character.
 * @param[in] filter_len Length of @p filter in bytes, excluding any NUL terminator.
 *
 * @return true if the topic matches the filter, and false otherwise.
 */
bool osal_mqtt_match_topic( const char *topic,
                            size_t topic_len,
                            const char *filter,
                            size_t filter_len );

#ifdef __cplusplus
}
#endif

#endif /* OSAL_MQTT_UTIL_H */
