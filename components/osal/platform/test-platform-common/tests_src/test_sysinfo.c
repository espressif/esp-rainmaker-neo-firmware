/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "osal_sysinfo.h"
#include <string.h>
#include <stdint.h>

void test_sysinfo_platform_name(void)
{
    /* Never NULL, never empty: the value is reported to the cloud. Deliberately no
     * comparison against an expected string -- this suite builds for both ESP-IDF and POSIX,
     * and the value is the chip target on the former. */
    const char *platform = osal_sysinfo_get_platform_name();
    TEST_ASSERT_NOT_NULL(platform);
    TEST_ASSERT_GREATER_THAN_size_t(0, strlen(platform));

    /* Stable across calls: a static string, not a rebuilt buffer. */
    TEST_ASSERT_EQUAL_PTR(platform, osal_sysinfo_get_platform_name());
}

void test_sysinfo_base_mac(void)
{
    /* --- Argument validation --- */
    uint8_t mac[OSAL_MAC_ADDR_LEN];
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, osal_sysinfo_get_base_mac(NULL, sizeof(mac)));
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, osal_sysinfo_get_base_mac(mac, OSAL_MAC_ADDR_LEN - 1));

    /* --- Every supported platform can report an address: a chip base MAC on ESP-IDF, a
     *     non-loopback interface on a host --- */
    memset(mac, 0, sizeof(mac));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_sysinfo_get_base_mac(mac, sizeof(mac)));

    /* An all-zero address identifies nothing, so it is never a valid answer. */
    const uint8_t zero_mac[OSAL_MAC_ADDR_LEN] = {0};
    TEST_ASSERT_NOT_EQUAL(0, memcmp(mac, zero_mac, sizeof(mac)));

    /* --- Stable: the address identifies the device, so repeated reads must agree --- */
    uint8_t mac_again[OSAL_MAC_ADDR_LEN];
    memset(mac_again, 0, sizeof(mac_again));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_sysinfo_get_base_mac(mac_again, sizeof(mac_again)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac, mac_again, OSAL_MAC_ADDR_LEN);

    /* --- A larger buffer is accepted, and nothing past the address is touched --- */
    uint8_t oversized[OSAL_MAC_ADDR_LEN + 4];
    memset(oversized, 0xA5, sizeof(oversized));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_sysinfo_get_base_mac(oversized, sizeof(oversized)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac, oversized, OSAL_MAC_ADDR_LEN);
    for (size_t i = OSAL_MAC_ADDR_LEN; i < sizeof(oversized); i++) {
        TEST_ASSERT_EQUAL_UINT8(0xA5, oversized[i]);
    }
}
