/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ext_io_packet_constants_esp.h
 * @brief Packet constants for ESP.
 */

#ifndef __OSAL_EXT_IO_PACKET_CONSTANTS_ESP_H__
#define __OSAL_EXT_IO_PACKET_CONSTANTS_ESP_H__

/* Use rarely used Unicode characters to avoid confusion with normal output or internal data */
// Start of External I/O packet
#define OSAL_EXT_IO_HEADER "\xF5\xF6\xF7\xF8"
// End of External I/O packet
#define OSAL_EXT_IO_TRAILER "\xF8\xF7\xF6\xF5"
// Received ping
#define OSAL_EXT_IO_RECEIVED_PING "\xF9\xFA\xFB\xFC"

#endif /* __OSAL_EXT_IO_PACKET_CONSTANTS_ESP_H__ */
