/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_ota_filetype_version.c
 * @brief Unit tests for the default filetype handler version_to_uint32 parser.
 *
 * Regression coverage for patch-version handling: "x.y.z" must encode the patch
 * field so that e.g. 1.0.0 < 1.0.1 - a parser that drops the patch field would
 * reject equal-patch bumps as "lower than or equal".
 */

#include "unity.h"
#include "test_rmng_ota_prototypes.h"

#include <string.h>

#include "esp_rmaker_error_types.h"
#include "esp_rmaker_ota_filetype_handler.h"
#include "ota_filetype_handler_internal.h"

/* Helper: run the default ctx version_to_uint32 over a NUL-terminated string. */
static esp_rmaker_error_t conv(const char *s, uint32_t *out)
{
    const esp_rmaker_ota_ft_ctx_t *ctx = filetype_handler_get_default_ctx();
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_NOT_NULL(ctx->version_to_uint32);
    esp_rmaker_ota_ft_version_t v = { .str = s, .len = (s ? strlen(s) : 0) };
    return ctx->version_to_uint32(v, out);
}

/* Expected encoding: major << 20 | minor << 10 | patch */
#define VENC(maj, min, pat) (((uint32_t)(maj) << 20) | ((uint32_t)(min) << 10) | (uint32_t)(pat))

void test_ft_version_basic_xyz(void)
{
    uint32_t n = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.0.0", &n));
    TEST_ASSERT_EQUAL_UINT32(VENC(1, 0, 0), n);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.0.1", &n));
    TEST_ASSERT_EQUAL_UINT32(VENC(1, 0, 1), n);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("12.34.56", &n));
    TEST_ASSERT_EQUAL_UINT32(VENC(12, 34, 56), n);
}

/* The core regression: patch bump must compare strictly greater. */
void test_ft_version_patch_bump_is_greater(void)
{
    uint32_t a = 0, b = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.0.0", &a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.0.1", &b));
    TEST_ASSERT_GREATER_THAN_UINT32(a, b);
}

/* Full ordering across major/minor/patch. */
void test_ft_version_ordering(void)
{
    uint32_t v100 = 0, v101 = 0, v110 = 0, v200 = 0, v1_99_99 = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.0.0", &v100));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.0.1", &v101));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.1.0", &v110));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("2.0.0", &v200));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.99.99", &v1_99_99));

    TEST_ASSERT_GREATER_THAN_UINT32(v100, v101);
    TEST_ASSERT_GREATER_THAN_UINT32(v101, v110);
    TEST_ASSERT_GREATER_THAN_UINT32(v110, v200);
    /* 2.0.0 outranks 1.99.99 (major dominates). */
    TEST_ASSERT_GREATER_THAN_UINT32(v1_99_99, v200);
}

/* "x.y" (no patch) is valid and equals "x.y.0". */
void test_ft_version_two_field_equals_zero_patch(void)
{
    uint32_t two = 0, three = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.2", &two));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.2.0", &three));
    TEST_ASSERT_EQUAL_UINT32(three, two);
    TEST_ASSERT_EQUAL_UINT32(VENC(1, 2, 0), two);
}

/* Segments beyond patch are ignored. */
void test_ft_version_extra_segments_ignored(void)
{
    uint32_t a = 0, b = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.2.3.4", &a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, conv("1.2.3", &b));
    TEST_ASSERT_EQUAL_UINT32(b, a);
}

void test_ft_version_invalid_inputs_rejected(void)
{
    uint32_t n = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, conv(NULL, &n));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, conv("", &n));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, conv("100", &n));     /* no dot */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, conv("1.0.", &n));    /* trailing dot */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, conv("1..0", &n));    /* empty field */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, conv(".1.0", &n));    /* leading dot */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, conv("1.a", &n));     /* non-digit */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, conv("1.0.1024", &n)); /* field > 1023 */
}
