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
#define OSAL_EXT_IO_RECEIVED_PING_LENGTH (sizeof(OSAL_EXT_IO_RECEIVED_PING) - 1)
#define OSAL_EXT_IO_RECEIVED_PING_TIMEOUT_MS CONFIG_OSAL_EXT_IO_ESP_ACK_TIMEOUT_MS
#define OSAL_EXT_IO_RECEIVED_PING_RETRIES 5
/* Idle poll interval of the command reader while the RX FIFO is empty. */
#define OSAL_EXT_IO_RX_POLL_MS 10
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
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

static const char *TAG = "osal_extio_esp";

#if OSAL_EXT_IO_MULTIPLEXED
/* Stack buffer that holds the vast majority of log lines without touching the
 * heap. Longer lines fall back to a one-shot malloc so nothing is truncated. */
#define OSAL_EXT_IO_LOG_STACK_BUFFER_SIZE 160
static vprintf_like_t s_prev_vprintf;
/* Inbound bytes the frame writer pulled off the UART while waiting for the
 * host's ACK ping but that turned out not to be part of one. They are command
 * bytes; hand them to the command reader instead of discarding them. */
static StreamBufferHandle_t s_pending_rx;
/* First task to write a frame; later writes must come from it too. */
static TaskHandle_t s_writer_task;

/* Frames and commands share one RX stream, so the ACK wait and the command
 * reader must never run concurrently - which holds only while one task does
 * both. A second writer would silently steal an ACK and desync the link. */
static bool osal_ext_io_claim_writer(void)
{
    TaskHandle_t self_task = xTaskGetCurrentTaskHandle();
    if (s_writer_task == NULL) {
        s_writer_task = self_task;
    } else if (s_writer_task != self_task) {
        ESP_LOGE(TAG, "write_line is single-writer: bound to '%s', called from '%s'",
                 pcTaskGetName(s_writer_task), pcTaskGetName(self_task));
        return false;
    }
    return true;
}

static void osal_ext_io_stash_rx(const void *data, size_t length)
{
    if (length == 0 || s_pending_rx == NULL) {
        return;
    }
    if (xStreamBufferSend(s_pending_rx, data, length, 0) != length) {
        ESP_LOGW(TAG, "Dropped %u inbound byte(s), pending RX buffer full", (unsigned)length);
    }
}

/* Wait for the host's ACK of a just-written frame, matching the ping marker one
 * byte at a time. Anything that does not advance the match belongs to an
 * inbound command, so it is stashed rather than consumed - reading the marker
 * as a fixed-size block would swallow whichever command bytes shared the read
 * and leave the rest of the marker behind to corrupt the next one. */
static bool osal_ext_io_await_ping(void)
{
    static const uint8_t ping[] = OSAL_EXT_IO_RECEIVED_PING;
    size_t matched = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(OSAL_EXT_IO_RECEIVED_PING_TIMEOUT_MS);

    while (1) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) {
            osal_ext_io_stash_rx(ping, matched);
            return false;
        }

        uint8_t byte = 0;
        int len = uart_read_bytes(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, &byte, 1, deadline - now);
        if (len != 1) {
            osal_ext_io_stash_rx(ping, matched);
            return false;
        }

        if (byte == ping[matched]) {
            if (++matched == OSAL_EXT_IO_RECEIVED_PING_LENGTH) {
                return true;
            }
            continue;
        }

        /* Mismatch: the bytes matched so far were command bytes after all. The
         * marker has no self-overlap, so only its first byte can restart it. */
        osal_ext_io_stash_rx(ping, matched);
        matched = 0;
        if (byte == ping[0]) {
            matched = 1;
        } else {
            osal_ext_io_stash_rx(&byte, 1);
        }
    }
}

/* Read one inbound byte, stashed leftovers first. Returns 1 on a byte, 0 if
 * none was available, -1 on error. */
static int osal_ext_io_read_byte(uint8_t *out)
{
    if (s_pending_rx != NULL && xStreamBufferReceive(s_pending_rx, out, 1, 0) == 1) {
        return 1;
    }

    size_t available = 0;
    if (uart_get_buffered_data_len(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, &available) != ESP_OK) {
        return -1;
    }
    if (available == 0) {
        vTaskDelay(pdMS_TO_TICKS(OSAL_EXT_IO_RX_POLL_MS));
        return 0;
    }

    return uart_read_bytes(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, out, 1, 0);
}

/* vprintf hook: emit each log line as one uart_write_bytes, whose driver-level
 * mutex is what keeps it from splitting an RPC frame. Log bytes carry no frame
 * markers, so the host treats them as monitor output. */
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

    uart_write_bytes(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, out, (size_t)len);

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
    if (s_pending_rx == NULL) {
        s_pending_rx = xStreamBufferCreate(CONFIG_OSAL_EXT_IO_ESP_UART_BUFFER_SIZE, 1);
        if (s_pending_rx == NULL) {
            ESP_LOGE(TAG, "Failed to create pending RX buffer");
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
    s_writer_task = NULL;
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

    if (!osal_ext_io_claim_writer()) {
        return false;
    }
    for (int retries = 0; retries < OSAL_EXT_IO_RECEIVED_PING_RETRIES; retries++) {
        uart_write_bytes(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, buffer, buffer_length);
        if (osal_ext_io_await_ping()) {
            return true;
        }
    }

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
    if (read_length) {
        *read_length = 0;
    }
    if (buffer == NULL || max_buffer_length == 0) {
        return false;
    }

    uint8_t *cur = buffer;
    /* The last byte is kept for the NUL terminator, as in the POSIX port. */
    const uint8_t *last = buffer + max_buffer_length - 1;
    while (1) {
        if (cur == last) {
            /* Buffer full: stop before reading a byte that cannot be stored, so
             * the rest of the line is read by the next call. */
            break;
        }
        uint8_t byte = 0;
#if OSAL_EXT_IO_MULTIPLEXED
        int len = osal_ext_io_read_byte(&byte);
#else /* OSAL_EXT_IO_MULTIPLEXED */
        int len = uart_read_bytes(CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM, &byte, 1, 50 / portTICK_PERIOD_MS);
#endif /* OSAL_EXT_IO_MULTIPLEXED */
        if (len < 0) {
            return false;
        }
        if (len == 0) {
            continue;
        }

        if ((byte == '\b' || byte == '\x7f') && cur > buffer) {
            cur--;
            continue;
        }

        if (byte == until_char) {
            break;
        }

        *cur++ = byte;

#if OSAL_EXT_IO_MULTIPLEXED
        /* Strip host ping markers that arrive with no write waiting on them:
         * the writer retransmits an unacked frame, and the host then sends one
         * ping per copy it received. The marker is a fixed non-ASCII sequence
         * that never occurs in a (printable) command, so drop it wherever it
         * appears in the inbound stream. */
        if (cur - buffer >= OSAL_EXT_IO_RECEIVED_PING_LENGTH &&
                memcmp(cur - OSAL_EXT_IO_RECEIVED_PING_LENGTH, OSAL_EXT_IO_RECEIVED_PING, OSAL_EXT_IO_RECEIVED_PING_LENGTH) == 0) {
            cur -= OSAL_EXT_IO_RECEIVED_PING_LENGTH;
        }
#endif /* OSAL_EXT_IO_MULTIPLEXED */
    }

    *cur = '\0';
    if (read_length) {
        *read_length = (size_t)(cur - buffer);
    }
    return true;
}
