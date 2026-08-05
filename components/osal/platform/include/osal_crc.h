/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_crc.h
 * @brief Platform common CRC (Cyclic Redundancy Check) header file.
 */

#ifndef __OSAL_CRC_H__
#define __OSAL_CRC_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute CRC32 (IEEE 802.3, polynomial 0xEDB88320, reflected, initial 0xFFFFFFFF, final XOR 0xFFFFFFFF).
 *
 * @param[in] data Pointer to input buffer
 * @param[in] len  Length in bytes
 * @return Computed CRC32 value
 */
uint32_t osal_crc32_generate(const uint8_t *data, size_t len);

/**
 * @brief Validate a CRC32 value against the data.
 *
 * @param[in] data     Pointer to input buffer
 * @param[in] len      Length in bytes
 * @param[in] expected Expected CRC32 value
 * @return true if computed CRC32 matches expected, false otherwise
 */
bool osal_crc32_validate(const uint8_t *data, size_t len, uint32_t expected);

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_CRC_H__ */
