/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include <string.h>
#include "osal_queue.h"
#include "osal_ticks.h"

typedef struct {
    int a;
    int b;
} pair_t;

void test_queue_basic(void)
{
    osal_queue_handle_t q = osal_queue_create(2, sizeof(pair_t));
    TEST_ASSERT_NOT_NULL(q);

    pair_t p1 = {1, 2};
    pair_t p2 = {3, 4};
    pair_t pr;

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_queue_send(q, &p1, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_queue_send(q, &p2, 0));
    /* queue full */
    TEST_ASSERT_EQUAL(OSAL_ERR_TIMEOUT, osal_queue_send(q, &p1, 0));

    memset(&pr, 0, sizeof(pr));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_queue_receive(q, &pr, 0));
    TEST_ASSERT_EQUAL(1, pr.a);
    TEST_ASSERT_EQUAL(2, pr.b);

    memset(&pr, 0, sizeof(pr));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_queue_receive(q, &pr, 0));
    TEST_ASSERT_EQUAL(3, pr.a);
    TEST_ASSERT_EQUAL(4, pr.b);

    /* queue empty */
    TEST_ASSERT_EQUAL(OSAL_ERR_TIMEOUT, osal_queue_receive(q, &pr, 0));

    osal_queue_delete(q);
}

void test_queue_ext_basic(void)
{
    /* Same behaviour as test_queue_basic, but the queue is created with its backing storage in
     * external RAM (PSRAM) when available. Exercises the static-buffer create/delete path. */
    osal_queue_handle_t q = osal_queue_create_ext(2, sizeof(pair_t));
    TEST_ASSERT_NOT_NULL(q);

    pair_t p1 = {1, 2};
    pair_t p2 = {3, 4};
    pair_t pr;

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_queue_send(q, &p1, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_queue_send(q, &p2, 0));
    /* queue full */
    TEST_ASSERT_EQUAL(OSAL_ERR_TIMEOUT, osal_queue_send(q, &p1, 0));

    memset(&pr, 0, sizeof(pr));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_queue_receive(q, &pr, 0));
    TEST_ASSERT_EQUAL(1, pr.a);
    TEST_ASSERT_EQUAL(2, pr.b);

    memset(&pr, 0, sizeof(pr));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_queue_receive(q, &pr, 0));
    TEST_ASSERT_EQUAL(3, pr.a);
    TEST_ASSERT_EQUAL(4, pr.b);

    /* queue empty */
    TEST_ASSERT_EQUAL(OSAL_ERR_TIMEOUT, osal_queue_receive(q, &pr, 0));

    osal_queue_delete(q);
}
