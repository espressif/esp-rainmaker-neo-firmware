/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_local_ctrl_sec2_cache.c
 * @brief SRP6a salt/verifier caching rule for the local endpoints service (security 2).
 *
 * Generating a salt/verifier pair is expensive, so it is cached in NVS - but the cache is
 * only valid for the PoP stored beside it. A custom PoP is never written to NVS, so caching
 * a pair derived from one would leave NVS self-inconsistent and break every later SEC2
 * handshake that used the stored PoP.
 *
 * The salt is random per generation, so "the cache still belongs to the original PoP" is
 * checked by asking for that PoP again and requiring the exact same bytes back: a
 * regenerated pair would differ, and a pair poisoned by the custom-PoP run would differ too.
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <stdlib.h>
#include <string.h>

#include "local_ctrl/sec2_cache.h"
#include "constants/esp_rmaker_nvs_common.h"
#include "osal_storage.h"

#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2

/* Stand-ins for a stored PoP and one an application would push at runtime. */
#define TEST_SEC2_NVS_POP    "0123456789"
#define TEST_SEC2_CUSTOM_POP "abcd1234"

/* Must match LOCAL_CTRL_NVS_NAMESPACE in svc_local_endpoints.c - pinned here on purpose, so
 * renaming the namespace (which would orphan every deployed device's cache) fails a test. */
#define TEST_SEC2_NVS_NAMESPACE "local_ctrl"

/** Start from an empty cache, so these tests do not depend on each other or on run order. */
static void __sec2_cache_reset(void)
{
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_init(RMAKER_NVS_PART_NAME));

    osal_storage_handle_t handle;
    if (osal_storage_open(RMAKER_NVS_PART_NAME, TEST_SEC2_NVS_NAMESPACE,
                          OSAL_STORAGE_OPEN_READWRITE, &handle) == OSAL_ERR_OK) {
        osal_storage_erase_all(handle);
        osal_storage_commit(handle);
        osal_storage_close(handle);
    }
}

void test_local_ctrl_sec2_custom_pop_does_not_poison_cache(void)
{
    __sec2_cache_reset();

    char *nvs_salt = NULL;
    char *nvs_verifier = NULL;
    int nvs_verifier_len = 0;

    /* A PoP that is in NVS: generates a pair and caches it. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
                          TEST_SEC2_NVS_POP, false, false,
                          &nvs_salt, &nvs_verifier, &nvs_verifier_len));
    TEST_ASSERT_NOT_NULL(nvs_salt);
    TEST_ASSERT_NOT_NULL(nvs_verifier);
    TEST_ASSERT_GREATER_THAN_INT(0, nvs_verifier_len);

    /* A custom PoP: must derive its own pair and leave the cache alone. */
    char *custom_salt = NULL;
    char *custom_verifier = NULL;
    int custom_verifier_len = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
                          TEST_SEC2_CUSTOM_POP, false, true,
                          &custom_salt, &custom_verifier, &custom_verifier_len));
    TEST_ASSERT_NOT_NULL(custom_salt);
    TEST_ASSERT_NOT_NULL(custom_verifier);
    /* Different PoP and a fresh random salt, so the pair must not match the cached one. */
    TEST_ASSERT_FALSE(custom_verifier_len == nvs_verifier_len &&
                      memcmp(custom_verifier, nvs_verifier, (size_t) nvs_verifier_len) == 0);

    /* Back to the stored PoP: the cache must still be the one derived from it, byte for byte.
     * Before the fix the custom run overwrote it, and this came back as the custom pair. */
    char *reused_salt = NULL;
    char *reused_verifier = NULL;
    int reused_verifier_len = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
                          TEST_SEC2_NVS_POP, true, false,
                          &reused_salt, &reused_verifier, &reused_verifier_len));
    TEST_ASSERT_EQUAL_INT(nvs_verifier_len, reused_verifier_len);
    TEST_ASSERT_EQUAL_MEMORY(nvs_salt, reused_salt, 16);
    TEST_ASSERT_EQUAL_MEMORY(nvs_verifier, reused_verifier, (size_t) nvs_verifier_len);

    free(nvs_salt);
    free(nvs_verifier);
    free(custom_salt);
    free(custom_verifier);
    free(reused_salt);
    free(reused_verifier);
}

/** A freshly generated PoP is written to NVS, so its pair is cached and reused. */
void test_local_ctrl_sec2_generated_pop_is_cached(void)
{
    __sec2_cache_reset();

    char *first_salt = NULL;
    char *first_verifier = NULL;
    int first_verifier_len = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
                          TEST_SEC2_NVS_POP, false, false,
                          &first_salt, &first_verifier, &first_verifier_len));

    char *second_salt = NULL;
    char *second_verifier = NULL;
    int second_verifier_len = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
                          TEST_SEC2_NVS_POP, true, false,
                          &second_salt, &second_verifier, &second_verifier_len));

    TEST_ASSERT_EQUAL_INT(first_verifier_len, second_verifier_len);
    TEST_ASSERT_EQUAL_MEMORY(first_salt, second_salt, 16);
    TEST_ASSERT_EQUAL_MEMORY(first_verifier, second_verifier, (size_t) first_verifier_len);

    free(first_salt);
    free(first_verifier);
    free(second_salt);
    free(second_verifier);
}

void test_local_ctrl_sec2_resolve_rejects_invalid_args(void)
{
    char *salt = NULL;
    char *verifier = NULL;
    int verifier_len = 0;

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
                          NULL, false, false, &salt, &verifier, &verifier_len));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
                          TEST_SEC2_NVS_POP, false, false, NULL, &verifier, &verifier_len));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
                          TEST_SEC2_NVS_POP, false, false, &salt, NULL, &verifier_len));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
                          TEST_SEC2_NVS_POP, false, false, &salt, &verifier, NULL));
}

#endif /* CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2 */
