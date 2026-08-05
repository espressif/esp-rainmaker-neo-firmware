/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_log_level.h
 * @brief Log levels and the compile-time maximum log level.
 */

#ifndef __OSAL_LOG_LEVEL_H__
#define __OSAL_LOG_LEVEL_H__

#include "sdkconfig.h"

/* Log if level is greater than or equal to the current log level */
#ifndef LOG_MAX_LEVEL
#define LOG_MAX_LEVEL CONFIG_LOG_MAXIMUM_LEVEL
#endif

/**
 * @brief Log level
 */
typedef enum {
    OSAL_LOG_LEVEL_NONE    = 0,    /*!< No log output */
    OSAL_LOG_LEVEL_ERROR   = 1,    /*!< Critical errors, software module can not recover on its own */
    OSAL_LOG_LEVEL_WARN    = 2,    /*!< Error conditions from which recovery measures have been taken */
    OSAL_LOG_LEVEL_INFO    = 3,    /*!< Information messages which describe normal flow of events */
    OSAL_LOG_LEVEL_DEBUG   = 4,    /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
    OSAL_LOG_LEVEL_VERBOSE = 5,    /*!< Bigger chunks of debugging information, or frequent messages which can potentially flood the output. */
    OSAL_LOG_LEVEL_MAX     = 6,    /*!< Number of levels supported */
} osal_log_level_t;

#endif /* __OSAL_LOG_LEVEL_H__ */
