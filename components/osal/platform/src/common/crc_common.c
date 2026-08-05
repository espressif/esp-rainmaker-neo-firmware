/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file crc_common.c
 * @brief Platform common CRC (Cyclic Redundancy Check) implementation file.
 */

#include "osal_crc.h"

#define CRC32_POLY_REFLECTED 0xEDB88320u

uint32_t osal_crc32_generate(const uint8_t *data, size_t len)
{
    if (!data) {
        return 0;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint32_t)data[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (CRC32_POLY_REFLECTED & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

bool osal_crc32_validate(const uint8_t *data, size_t len, uint32_t expected)
{
    return osal_crc32_generate(data, len) == expected;
}
