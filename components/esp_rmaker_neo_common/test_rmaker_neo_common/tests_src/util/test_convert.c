/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_convert.c
 * @brief Test the convert utility.
 */

#include "unity.h"
#include "test_rmng_common_prototypes.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "util/esp_rmaker_convert_hex.h"

/* Test data ******************************************************************/

#define TEST_DATA_COUNT 5

static const char *test_data_hex_str[TEST_DATA_COUNT] = {
    "abcdef123456789",       // odd length (15)
    "1edcba9876543210",      // even length (16)
    "0123456789abcde",       // odd length (15)
    "deadbeefcafebabe",      // even length (16)
    "a1234567890abcdef"      // odd length (17)
};

// static const char *test_data_base64_str[TEST_DATA_COUNT] = {
//     "Crze8SNFZ4k=",   // bytes 0
//     "Hty6mHZUMhA=",   // bytes 1
//     "ABI0VniavN4=",   // bytes 2
//     "3q2+78r+ur4=",   // bytes 3
//     "ChI0VniQq83v"    // bytes 4
// };

static const uint8_t test_data_bytes_0[] = {0x0a, 0xbc, 0xde, 0xf1, 0x23, 0x45, 0x67, 0x89};
static const uint8_t test_data_bytes_1[] = {0x1e, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
static const uint8_t test_data_bytes_2[] = {0x00, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde};
static const uint8_t test_data_bytes_3[] = {0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe};
static const uint8_t test_data_bytes_4[] = {0x0a, 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef};

static const uint8_t *test_data_bytes[TEST_DATA_COUNT] = {
    test_data_bytes_0,
    test_data_bytes_1,
    test_data_bytes_2,
    test_data_bytes_3,
    test_data_bytes_4
};

static const int test_data_bytes_len[TEST_DATA_COUNT] = {
    8, // "abcdef123456789"  (15 chars -> 8 bytes, first padded)
    8, // "1edcba9876543210" (16 chars -> 8 bytes)
    8, // "0123456789abcde"  (15 chars -> 8 bytes, first padded)
    8, // "deadbeefcafebabe" (16 chars -> 8 bytes)
    9  // "a1234567890abcdef" (17 chars -> 9 bytes, first padded)
};

void test_rmaker_convert_bytes_to_hex(void)
{
    for (int i = 0; i < TEST_DATA_COUNT; i++) {
        char hex_str[2 * test_data_bytes_len[i] + 1];
        size_t test_data_str_len = strlen(test_data_hex_str[i]);
        esp_rmaker_error_t err = esp_rmaker_convert_bytes_to_hex(test_data_bytes[i], test_data_bytes_len[i], hex_str, sizeof(hex_str));
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "convert_bytes_to_hex failed");
        // ignore the first character if the hex string length is odd (first character is the padding)
        TEST_ASSERT_EQUAL_STRING_MESSAGE(test_data_hex_str[i], &hex_str[test_data_str_len % 2], "convert_bytes_to_hex mismatch");
    }
}

void test_rmaker_convert_hex_to_bytes(void)
{
    for (int i = 0; i < TEST_DATA_COUNT; i++) {
        uint8_t bytes[test_data_bytes_len[i]];
        esp_rmaker_error_t err = esp_rmaker_convert_hex_to_bytes(test_data_hex_str[i], strlen(test_data_hex_str[i]), bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "convert_hex_to_bytes failed");
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(test_data_bytes[i], bytes, test_data_bytes_len[i], "convert_hex_to_bytes mismatch");
    }
}

void test_rmaker_convert_hex_error_paths(void)
{
    /* bytes_to_hex: NULL args and insufficient buffer */
    char hex[5];
    uint8_t data[2] = {0xAA, 0xBB};
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_convert_bytes_to_hex(NULL, 2, hex, sizeof(hex)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_convert_bytes_to_hex(data, 2, NULL, sizeof(hex)));
    /* need 2*2+1 = 5 bytes, so 4 should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_convert_bytes_to_hex(data, 2, hex, 4));

    /* hex_to_bytes: NULL args */
    uint8_t out[2];
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_convert_hex_to_bytes(NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_convert_hex_to_bytes("aa", 2, NULL, sizeof(out)));

    /* hex_to_bytes: insufficient output buffer */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_convert_hex_to_bytes("aabb", 4, out, 1));

    /* hex_to_bytes: invalid characters */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_convert_hex_to_bytes("xz", 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_convert_hex_to_bytes("gg", 2, out, sizeof(out)));
}

// void test_rmaker_convert_base64_to_bytes(void)
// {
//     for (int i = 0; i < TEST_DATA_COUNT; i++)
//     {
//         size_t bytes_len;
//         uint8_t *bytes_ptr = esp_rmaker_convert_base64_to_bytes(test_data_base64_str[i], strlen(test_data_base64_str[i]), &bytes_len);
//         TEST_ASSERT_NOT_NULL_MESSAGE(bytes_ptr, "convert_base64_to_bytes failed");
//         TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(test_data_bytes[i], bytes_ptr, bytes_len, "convert_base64_to_bytes mismatch");
//         free(bytes_ptr);
//     }
// }

// void test_rmaker_convert_bytes_to_base64(void)
// {
//     for (int i = 0; i < TEST_DATA_COUNT; i++)
//     {
//         size_t base64_str_len;
//         char *base64_str_ptr = esp_rmaker_convert_bytes_to_base64(test_data_bytes[i], test_data_bytes_len[i], &base64_str_len);
//         TEST_ASSERT_NOT_NULL_MESSAGE(base64_str_ptr, "convert_bytes_to_base64 failed");
//         TEST_ASSERT_EQUAL_STRING_MESSAGE(test_data_base64_str[i], base64_str_ptr, "convert_bytes_to_base64 mismatch");
//         free(base64_str_ptr);
//     }
// }
