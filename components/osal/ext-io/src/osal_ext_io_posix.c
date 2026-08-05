/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_ext_io.h"
#include "osal_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#endif
#if defined(__linux__)
#include <pty.h>
#endif

static const char *TAG = "osal_extio_posix";

static int s_master_fd = -1;
static char s_slave_path[128];

static bool write_all(int fd, const uint8_t *data, size_t length)
{
    size_t total_written = 0;
    while (total_written < length) {
        ssize_t written = write(fd, data + total_written, length - total_written);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total_written += (size_t)written;
    }
    return true;
}

bool osal_ext_io_init(void)
{
#if defined(__APPLE__) || defined(__linux__)
    int master_fd = -1;
    int slave_fd = -1;

    if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) != 0) {
        OSAL_LOGE(TAG, "openpty failed: %s", strerror(errno));
        return false;
    }

    const char *slave_name = ttyname(slave_fd);
    if (slave_name == NULL) {
        OSAL_LOGE(TAG, "ttyname failed: %s", strerror(errno));
        close(master_fd);
        close(slave_fd);
        return false;
    }

    // Configure slave side to raw mode so bytes pass through unchanged
    struct termios tios;
    if (tcgetattr(slave_fd, &tios) == 0) {
        cfmakeraw(&tios);
        tcsetattr(slave_fd, TCSANOW, &tios);
    }

    // We do not keep the slave fd open; external tools will open it by path
    close(slave_fd);

    s_master_fd = master_fd;
    strncpy(s_slave_path, slave_name, sizeof(s_slave_path) - 1);
    s_slave_path[sizeof(s_slave_path) - 1] = '\0';

    // Write the slave path to a file in the current directory
    FILE *port_file = fopen("port.out", "w");
    if (port_file != NULL) {
        fprintf(port_file, "%s\n", s_slave_path);
        fclose(port_file);
    } else {
        OSAL_LOGE(TAG, "Failed to write serial port file");
    }

    OSAL_LOGI(TAG, "POSIX PTY ready. Connect to: %s", s_slave_path);
    return true;
#else
    OSAL_LOGE(TAG, "POSIX PTY not supported on this platform");
    return false;
#endif
}

bool osal_ext_io_deinit(void)
{
    if (s_master_fd >= 0) {
        close(s_master_fd);
        s_master_fd = -1;
    }
    s_slave_path[0] = '\0';
    return true;
}

bool osal_ext_io_write_line(const char *line, size_t line_length)
{
    if (s_master_fd < 0) {
        return false;
    }
    if (line == NULL && line_length != 0) {
        return false;
    }
    if (line_length > 0) {
        return write_all(s_master_fd, (const uint8_t *)line, line_length);
    }
    return true;
}

bool osal_ext_io_read_until_sync(uint8_t *buffer, size_t max_buffer_length, uint8_t until_char, size_t *read_length)
{
    if (read_length) {
        *read_length = 0;
    }
    if (s_master_fd < 0 || buffer == NULL || max_buffer_length == 0) {
        return false;
    }

    uint8_t *cur = buffer;
    const uint8_t *end = buffer + max_buffer_length; // maximum writable position

    while (1) {
        uint8_t byte = 0;
        ssize_t n = read(s_master_fd, &byte, 1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            // EOF on master end
            break;
        }

        // Handle backspace or delete
        if ((byte == '\b' || byte == 0x7F) && cur > buffer) {
            cur--;
            continue;
        }

        if (byte == until_char) {
            // Null-terminate if there is space
            if (cur < end) {
                *cur = '\0';
            } else if (max_buffer_length > 0) {
                buffer[max_buffer_length - 1] = '\0';
            }
            break;
        }

        if (cur < end - 1) { // reserve room for '\0'
            *cur++ = byte;
        } else {
            // Buffer full; terminate and stop
            buffer[max_buffer_length - 1] = '\0';
            break;
        }
    }

    if (read_length) {
        *read_length = (size_t)(cur - buffer);
    }
    return true;
}
