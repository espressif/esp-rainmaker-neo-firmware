/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_ota_image_progress.c
 * @brief Unit tests for image_progress_init / image_progress_seed / image_progress_add_bytes.
 *
 * All three functions are pure struct operations (checkpoint arithmetic) with one
 * side-effect call to ota_job_state_post_event when a checkpoint is reached.
 * Tests validate the struct invariants directly; the post_event call is safe in
 * the UNINITIALIZED FSM state (work-queue is already mocked) and does not need
 * counting here - structural invariants are sufficient to detect the update-storm
 * regression (seed not fast-forwarding checkpoint.next).
 */

#include "unity.h"
#include "test_rmng_ota_prototypes.h"

#include <stdint.h>

#include "esp_rmaker_error_types.h"
#include "ota_image_progress.h"

/* =========================================================================
 * image_progress_init
 * ========================================================================= */

void test_progress_init_zero_filesize_returns_error(void)
{
    image_progress_ctx_t ctx;
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, image_progress_init(&ctx, 0));
}

void test_progress_init_null_ctx_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, image_progress_init(NULL, 1000));
}

void test_progress_init_sets_zero_received_and_correct_checkpoint(void)
{
    image_progress_ctx_t ctx;
    /* 1000 bytes, CONFIG_RMNG_OTA_PROGRESS_CHECKPOINTS == 10 -> inc = 100 */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_init(&ctx, 1000));
    TEST_ASSERT_EQUAL_UINT32(0, ctx.bytes_received);
    TEST_ASSERT_GREATER_THAN_UINT32(0, ctx.checkpoint.inc);
    TEST_ASSERT_EQUAL_UINT32(ctx.checkpoint.inc, ctx.checkpoint.next);
}

/* =========================================================================
 * image_progress_seed - fast-forward invariants
 * ========================================================================= */

void test_progress_seed_null_ctx_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, image_progress_seed(NULL, 500));
}

void test_progress_seed_sets_bytes_received(void)
{
    image_progress_ctx_t ctx;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_init(&ctx, 1000));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_seed(&ctx, 450));
    TEST_ASSERT_EQUAL_UINT32(450, ctx.bytes_received);
}

/* checkpoint.next must jump past the seed so the first add_bytes after resume
 * does NOT immediately fire a progress event. */
void test_progress_seed_advances_checkpoint_past_seed(void)
{
    image_progress_ctx_t ctx;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_init(&ctx, 1000));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_seed(&ctx, 450));
    /* checkpoint.next must be strictly greater than bytes_received */
    TEST_ASSERT_GREATER_THAN_UINT32(ctx.bytes_received, ctx.checkpoint.next);
}

/* seed at 0 must behave identically to init (no event, next == inc). */
void test_progress_seed_zero_equals_init(void)
{
    image_progress_ctx_t base, seeded;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_init(&base, 1000));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_init(&seeded, 1000));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_seed(&seeded, 0));

    TEST_ASSERT_EQUAL_UINT32(base.bytes_received,   seeded.bytes_received);
    TEST_ASSERT_EQUAL_UINT32(base.checkpoint.next,  seeded.checkpoint.next);
    TEST_ASSERT_EQUAL_UINT32(base.checkpoint.inc,   seeded.checkpoint.inc);
}

/* seed exactly on a checkpoint boundary: next must be the NEXT boundary above
 * it, not the same one (no immediate event on the first byte). */
void test_progress_seed_on_boundary_next_is_one_inc_ahead(void)
{
    image_progress_ctx_t ctx;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_init(&ctx, 1000));
    uint32_t inc = ctx.checkpoint.inc;

    /* seed at exactly 3 * inc */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_seed(&ctx, 3 * inc));
    TEST_ASSERT_EQUAL_UINT32(4 * inc, ctx.checkpoint.next);
}

/* =========================================================================
 * image_progress_add_bytes - checkpoint advancement
 * ========================================================================= */

void test_progress_add_bytes_null_ctx_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, image_progress_add_bytes(NULL, 100));
}

/* Adding bytes below the first checkpoint must not advance checkpoint.next. */
void test_progress_add_bytes_below_checkpoint_no_advance(void)
{
    image_progress_ctx_t ctx;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_init(&ctx, 1000));
    uint32_t next_before = ctx.checkpoint.next;

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_add_bytes(&ctx, next_before - 1));
    TEST_ASSERT_EQUAL_UINT32(next_before, ctx.checkpoint.next);
    TEST_ASSERT_EQUAL_UINT32(next_before - 1, ctx.bytes_received);
}

/* Reaching a checkpoint must advance checkpoint.next by exactly one inc. */
void test_progress_add_bytes_at_checkpoint_advances_next(void)
{
    image_progress_ctx_t ctx;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_init(&ctx, 1000));
    uint32_t inc  = ctx.checkpoint.inc;
    uint32_t next = ctx.checkpoint.next; /* == inc after init */

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_add_bytes(&ctx, next));
    TEST_ASSERT_EQUAL_UINT32(2 * inc, ctx.checkpoint.next);
}

/* After seeding to 50 %, a small add must not fire until the next checkpoint
 * boundary; checkpoint.next must remain at seed's fast-forwarded value. */
void test_progress_seed_then_add_small_no_advance(void)
{
    image_progress_ctx_t ctx;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_init(&ctx, 1000));
    uint32_t inc = ctx.checkpoint.inc;

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_seed(&ctx, 5 * inc));
    uint32_t next_after_seed = ctx.checkpoint.next; /* 6 * inc */

    /* add one byte - must not advance checkpoint.next */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_add_bytes(&ctx, 1));
    TEST_ASSERT_EQUAL_UINT32(next_after_seed, ctx.checkpoint.next);
    TEST_ASSERT_EQUAL_UINT32(5 * inc + 1, ctx.bytes_received);
}

/* After seeding, reaching the next checkpoint must advance exactly once. */
void test_progress_seed_then_add_to_checkpoint_advances_once(void)
{
    image_progress_ctx_t ctx;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_init(&ctx, 1000));
    uint32_t inc = ctx.checkpoint.inc;

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_seed(&ctx, 5 * inc));
    uint32_t next_after_seed = ctx.checkpoint.next; /* 6 * inc */

    /* Add exactly (next_after_seed - bytes_received) to hit checkpoint */
    uint32_t delta = next_after_seed - ctx.bytes_received;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, image_progress_add_bytes(&ctx, delta));
    TEST_ASSERT_EQUAL_UINT32(next_after_seed + inc, ctx.checkpoint.next);
}
