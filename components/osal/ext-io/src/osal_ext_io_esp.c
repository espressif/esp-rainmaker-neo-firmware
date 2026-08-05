/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ext_io_esp.c
 * @brief External I/O for ESP32. Implemented using UART.
 */

/* Define if the UART is multiplexed with the monitor */
#define OSAL_EXT_IO_MULTIPLEXED CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM == 0

#include "osal_ext_io.h"
#if OSAL_EXT_IO_MULTIPLEXED
#include "osal_ext_io_packet_constants_esp.h"
#define OSAL_EXT_IO_RECEIVED_PING_LENGTH sizeof(OSAL_EXT_IO_RECEIVED_PING) - 1
#define OSAL_EXT_IO_RECEIVED_PING_TIMEOUT_MS 500
#define OSAL_EXT_IO_RECEIVED_PING_RETRIES 5
#endif /* OSAL_EXT_IO_MULTIPLEXED */

/* Standard */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ESP-IDF */
#include "driver/uart.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "esp_log.h"

/* FreeRTOS */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "osal_extio_esp";

#if OSAL_EXT_IO_MULTIPLEXED
/* When the UART is shared with the console monitor, the framed RPC writer and
 * the ESP-IDF log writer both target the same UART. Without mutual exclusion
 * their bytes interleave: log output lands inside an RPC frame, the host-side
 * demultiplexer can no longer find the trailer at the expected offset, and the
 * whole frame is discarded - surfacing as a corrupt / "invalid command"
 * response. A single recursive mutex serializes every UART write so each frame
 * and each log line is emitted atomically. Recursive so a log emitted from
 * within a locked section (e.g. the UART driver) can never self-deadlock. */
static SemaphoreHandle_t s_uart_write_mutex;
/* Stack buffer that holds the vast majority of log lines without touching the
 * heap. Longer lines fall back to a one-shot malloc so nothing is truncated. */
#define OSAL_EXT_IO_LOG_STACK_BUFFER_SIZE 160
static vprintf_like_t s_prev_vprintf;

static inline void osal_ext_io_uart_lock(void)
{
    if (s_uart_write_mutex) {
        xSemaphoreTakeRecursive(s_uart_write_mutex, portMAX_DELAY);
    }
}

static inline void osal_ext_io_uart_unlock(void)
{
    if (s_uart_write_mutex) {
        xSemaphoreGiveRecursive(s_uart_write_mutex);
    }
}

/* vprintf hook: route every ESP-IDF log line through the same UART write lock
 * as the RPC framer so the two never interleave on the wire. Log bytes are
 * written raw (no frame markers) so the host treats them as monitor output. */
static int osal_ext_io_log_vprintf(const char *fmt, va_list ap)
{
    char stack_buffer[OSAL_EXT_IO_LOG_STACK_BUFFER_SIZE];

    /* First pass into the stack buffer. vsnprintf returns the length the line
     * *would* occupy (excluding the NUL), which tells us whether it fit. */
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int len = vsnprintf(stack_buffer, sizeof(stack_buffer), fmt, ap_copy);
    va_end(ap_copy);
    if (len <= 0) {
        return len;
    }

    const char *out = stack_buffer;
    char *heap_buffer = NULL;
    if ((size_t)len >= sizeof(stack_buffer)) {
        /* Line overflowed the stack buffer - reformat into an exact-fit heap
         * allocation so nothing is truncated. On allocation failure, fall back
         * to the (truncated) stack copy rather than dropping the line. */
        heap_buffer = malloc((size_t)len + 1);
        if (heap_buffer != NULL) {
            vsnprintf(heap_buffer, (size_t)len + 1, fmt, ap);
            out = heap_buffer;
        } else {
            len = (int)sizeof(stack_buffer) - 1;
        }
    }

    osal_ext_io_uart_lock();
    uart_write_bytes(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, out, (size_t)len);
    osal_ext_io_uart_unlock();

    free(heap_buffer);
    return len;
}
#endif /* OSAL_EXT_IO_MULTIPLEXED */

bool osal_ext_io_init(void)
{
    esp_err_t err = ESP_OK;

    /* Configure the UART if not mulitplexing with the monitor */
    const uart_config_t uart_config = {
        .baud_rate = CONFIG_OSAL_EXT_IO_ESP_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    err = uart_param_config(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART");
        return false;
    }
#if !OSAL_EXT_IO_MULTIPLEXED
    err = uart_set_pin(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, CONFIG_OSAL_EXT_IO_ESP_UART_TXD, CONFIG_OSAL_EXT_IO_ESP_UART_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pin");
        return false;
    }
#endif /* !OSAL_EXT_IO_MULTIPLEXED */

    int intr_alloc_flags = 0;
#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif
    err = uart_driver_install(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, CONFIG_OSAL_EXT_IO_ESP_UART_BUFFER_SIZE, CONFIG_OSAL_EXT_IO_ESP_UART_BUFFER_SIZE, 0, NULL, intr_alloc_flags);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver");
        return false;
    }

#if OSAL_EXT_IO_MULTIPLEXED
    /* Serialize RPC frames against console logs on the shared UART. Create the
     * mutex before installing the log hook so the hook never runs lock-less. */
    if (s_uart_write_mutex == NULL) {
        s_uart_write_mutex = xSemaphoreCreateRecursiveMutex();
        if (s_uart_write_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create UART write mutex");
            return false;
        }
    }
    s_prev_vprintf = esp_log_set_vprintf(osal_ext_io_log_vprintf);
#endif /* OSAL_EXT_IO_MULTIPLEXED */

    return true;
}

bool osal_ext_io_deinit(void)
{
#if OSAL_EXT_IO_MULTIPLEXED
    /* Stop routing logs through the (about-to-be-removed) UART path first. */
    esp_log_set_vprintf(s_prev_vprintf ? s_prev_vprintf : vprintf);
#endif /* OSAL_EXT_IO_MULTIPLEXED */
    uart_driver_delete(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM);
    return true;
}

bool osal_ext_io_write_line(const char *line, size_t line_length)
{
#if OSAL_EXT_IO_MULTIPLEXED
    // Encapsulate the line in the header and trailer with length field
    size_t header_length = sizeof(OSAL_EXT_IO_HEADER) - 1;
    size_t trailer_length = sizeof(OSAL_EXT_IO_TRAILER) - 1;
    size_t length_field_length = 2; // uint16_t
    size_t buffer_length = line_length + header_length + length_field_length + trailer_length;
    char buffer[buffer_length];

    // Copy header
    memcpy(buffer, OSAL_EXT_IO_HEADER, header_length);

    // Copy length as big-endian uint16_t
    uint16_t data_length = (uint16_t)line_length;
    buffer[header_length] = (data_length >> 8) & 0xFF;     // MSB
    buffer[header_length + 1] = data_length & 0xFF;         // LSB

    // Copy data
    memcpy(buffer + header_length + length_field_length, line, line_length);

    // Copy trailer
    memcpy(buffer + header_length + length_field_length + line_length, OSAL_EXT_IO_TRAILER, trailer_length);

    int retries = 0, read_len = 0;
    char ping_buffer[OSAL_EXT_IO_RECEIVED_PING_LENGTH];
    osal_ext_io_uart_lock();
    do {
        uart_write_bytes(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, buffer, buffer_length);
        read_len = uart_read_bytes(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, ping_buffer, OSAL_EXT_IO_RECEIVED_PING_LENGTH, OSAL_EXT_IO_RECEIVED_PING_TIMEOUT_MS / portTICK_PERIOD_MS);
        if (read_len == OSAL_EXT_IO_RECEIVED_PING_LENGTH && memcmp(ping_buffer, OSAL_EXT_IO_RECEIVED_PING, OSAL_EXT_IO_RECEIVED_PING_LENGTH) == 0) {
            // Received ping
            osal_ext_io_uart_unlock();
            return true;
        }
        retries++;
    } while (retries < OSAL_EXT_IO_RECEIVED_PING_RETRIES);
    osal_ext_io_uart_unlock();

    // Failed to write line
    return false;
#else /* OSAL_EXT_IO_MULTIPLEXED */
    // Write the actual line
    uart_write_bytes(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, line, line_length);
#endif /* OSAL_EXT_IO_MULTIPLEXED */
    return true;
}

bool osal_ext_io_read_until_sync(uint8_t *buffer, size_t max_buffer_length, uint8_t until_char, size_t *read_length)
{
    uint8_t *cur = buffer;
    while (1) {
        int len = uart_read_bytes(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, cur, 1, 50 / portTICK_PERIOD_MS);
        if (len < 0) {
            return false;
        }
        if (len == 0) {
            continue;
        }

        if ((*cur == '\b' || *cur == '\x7f') && cur > buffer) {
            cur--;
            continue;
        }

        if (*cur == until_char) {
            *cur = '\0';
            break;
        }

        if (cur - buffer >= max_buffer_length) {
            break;
        }

        cur++;

#if OSAL_EXT_IO_MULTIPLEXED
        /* Strip any host ping marker that lands in the command stream. The host
         * ACKs each received frame with a ping; under load it can be slow, the
         * writer retransmits, and the host then sends one ping per received
         * copy - leaving stray ping markers in the RX FIFO that would otherwise
         * prepend the next command and make it parse as invalid. The marker is
         * a fixed non-ASCII sequence that never occurs in a (printable)
         * command, so drop it wherever it appears in the inbound stream. */
        if (cur - buffer >= OSAL_EXT_IO_RECEIVED_PING_LENGTH &&
                memcmp(cur - OSAL_EXT_IO_RECEIVED_PING_LENGTH, OSAL_EXT_IO_RECEIVED_PING, OSAL_EXT_IO_RECEIVED_PING_LENGTH) == 0) {
            cur -= OSAL_EXT_IO_RECEIVED_PING_LENGTH;
        }
#endif /* OSAL_EXT_IO_MULTIPLEXED */
    }

    *read_length = (size_t)(cur - buffer);
    return true;
}
