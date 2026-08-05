/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_ota_image_handler.c
 * @brief Unit tests for image_handler_write_chunk (image/handler.c).
 *
 * Covers the range check that keeps a stream-supplied offset inside the image size
 * declared in the job document, before it reaches a filetype handler.
 */

#include "unity.h"
#include "test_rmng_ota_prototypes.h"

#include <stdint.h>
#include <string.h>

#include "esp_rmaker_error_types.h"
#include "ota_image_handler.h"

#define TEST_IMAGE_SIZE 1024

static size_t __chunks_seen;
static size_t __last_offset;
static size_t __last_size;

static esp_rmaker_error_t __spy_on_download_chunk(esp_rmaker_ota_ft_download_handle_t handle,
        const uint8_t *data, size_t size, size_t offset)
{
    (void)handle;
    (void)data;
    __chunks_seen++;
    __last_offset = offset;
    __last_size = size;
    return ESP_RMAKER_OK;
}

static const esp_rmaker_ota_ft_ctx_t __spy_ft_handler = {
    .on_download_chunk = __spy_on_download_chunk,
};

/* A ctx built by hand: image_handler_begin() would pull in osal_ota and a real
 * partition, and write_chunk only reads handle/ft_handler/image_size. */
static image_handler_ctx_t __make_ctx(void)
{
    image_handler_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = (esp_rmaker_ota_ft_download_handle_t)(uintptr_t)0xD0D0;
    ctx.ft_handler = &__spy_ft_handler;
    ctx.image_size = TEST_IMAGE_SIZE;
    return ctx;
}

static void __reset_spy(void)
{
    __chunks_seen = 0;
    __last_offset = 0;
    __last_size = 0;
}

void test_image_handler_chunk_within_image_is_written(void)
{
    image_handler_ctx_t ctx = __make_ctx();
    uint8_t data[64] = {0};
    __reset_spy();

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_handler_write_chunk(&ctx, data, sizeof(data), 0));
    TEST_ASSERT_EQUAL_UINT(1, __chunks_seen);

    /* Exactly filling the image is in range. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      image_handler_write_chunk(&ctx, data, sizeof(data), TEST_IMAGE_SIZE - sizeof(data)));
    TEST_ASSERT_EQUAL_UINT(2, __chunks_seen);
    TEST_ASSERT_EQUAL_UINT(TEST_IMAGE_SIZE - sizeof(data), __last_offset);
}

void test_image_handler_chunk_past_image_end_is_rejected(void)
{
    image_handler_ctx_t ctx = __make_ctx();
    uint8_t data[64] = {0};
    __reset_spy();

    /* Offset inside the image, but the chunk runs off the end by one byte. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      image_handler_write_chunk(&ctx, data, sizeof(data),
                              TEST_IMAGE_SIZE - sizeof(data) + 1));
    /* Offset entirely past the image - what an out-of-range block id produces. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      image_handler_write_chunk(&ctx, data, sizeof(data), TEST_IMAGE_SIZE));
    TEST_ASSERT_EQUAL_UINT(0, __chunks_seen);
}

void test_image_handler_chunk_offset_overflow_is_rejected(void)
{
    image_handler_ctx_t ctx = __make_ctx();
    uint8_t data[64] = {0};
    __reset_spy();

    /* offset + size wraps; a check written that way would pass this. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      image_handler_write_chunk(&ctx, data, sizeof(data), UINT32_MAX - 8));
    TEST_ASSERT_EQUAL_UINT(0, __chunks_seen);
}
