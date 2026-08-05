/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_core_sign_challenge.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <stdlib.h>
#include "core_internal.h"

void test_core_sign_challenge_invalid_args(void)
{
    uint8_t *signature = NULL;
    size_t signature_len = 0;
    uint8_t challenge[16];

    /* NULL challenge */
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, esp_rmaker_core_sign_challenge(NULL, sizeof(challenge), &signature, &signature_len), "sign_challenge should fail with NULL challenge");

    /* NULL signature pointer */
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, esp_rmaker_core_sign_challenge(challenge, sizeof(challenge), NULL, &signature_len), "sign_challenge should fail with NULL signature pointer");

    /* NULL signature_len pointer */
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, esp_rmaker_core_sign_challenge(challenge, sizeof(challenge), &signature, NULL), "sign_challenge should fail with NULL signature_len pointer");

    /* Zero challenge length */
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, esp_rmaker_core_sign_challenge(challenge, 0, &signature, &signature_len), "sign_challenge should fail with zero challenge length");
}
