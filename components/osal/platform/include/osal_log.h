/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_log.h
 * @brief Platform common log header file.
 */

#ifndef __OSAL_LOG_H__
#define __OSAL_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Include the log level header file */
#include "osal_log_level.h"

/* Include the implementation header file that defines the LOG_INTERNAL(level, tag, fmt, ...) macro */
#include "osal_log_impl.h"

#define OSAL_LOGW(tag, fmt, ...) LOG_INTERNAL(OSAL_LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define OSAL_LOGI(tag, fmt, ...) LOG_INTERNAL(OSAL_LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define OSAL_LOGD(tag, fmt, ...) LOG_INTERNAL(OSAL_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define OSAL_LOGV(tag, fmt, ...) LOG_INTERNAL(OSAL_LOG_LEVEL_VERBOSE, tag, fmt, ##__VA_ARGS__)
#define OSAL_LOGE(tag, fmt, ...) LOG_INTERNAL(OSAL_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)

#define OSAL_LOG_BUFFER_HEX(tag, buf, len, level) LOG_BUFFER_HEX_INTERNAL(tag, buf, len, level)
#ifdef __cplusplus
}
#endif

#endif /* __OSAL_LOG_H__ */
