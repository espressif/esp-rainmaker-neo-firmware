/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ext_io.h
 * @brief Common interface for external I/O.
 */

#ifndef __OSAL_EXT_IO_H__
#define __OSAL_EXT_IO_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the external I/O interface.
 * @return true if successful, false otherwise
 */
bool osal_ext_io_init(void);

/**
 * @brief Deinitialize the external I/O interface.
 * @return true if successful, false otherwise
 */
bool osal_ext_io_deinit(void);

/**
 * @brief Write a line to the external I/O interface (e.g., instructions). This automatically adds a CRLF line terminator.
 * @param[in] line The line to write
 * @param[in] line_length The length of the line (excluding the null terminator)
 * @return true if successful, false otherwise
 */
bool osal_ext_io_write_line(const char *line, size_t line_length);

/**
 * @brief Get the external I/O interface until a certain character is sent, or max_buffer_length is reached.
 * @note This blocks until the until_char is sent or max_buffer_length is reached.
 * @param[out] buffer The buffer to store the input
 * @param[in] max_buffer_length The maximum length of the buffer
 * @param[in] until_char The character to stop at
 * @param[out] read_length The length of the input
 * @return true if successful, false otherwise
 */
bool osal_ext_io_read_until_sync(uint8_t *buffer, size_t max_buffer_length, uint8_t until_char, size_t *read_length);

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_EXT_IO_H__ */
