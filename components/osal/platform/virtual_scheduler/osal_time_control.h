/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_time_control.h
 * @brief Platform common manual time control.
 */

#ifndef __OSAL_TIME_CONTROL_H__
#define __OSAL_TIME_CONTROL_H__

#include "osal_time.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the time manually
 * @param[in] time The time to set
 * @return true if successful, false otherwise
 */
bool osal_time_control_set_time(time_t time);

/**
 * @brief Advance the time manually
 * @param[in] time_diff The time difference to advance
 * @return true if successful, false otherwise
 */
bool osal_time_control_advance_time(time_t time_diff);

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_TIME_CONTROL_H__ */
