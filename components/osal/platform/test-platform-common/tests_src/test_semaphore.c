/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "osal_semaphore.h"
#include "osal_ticks.h"

void test_mutex_basic(void)
{
    osal_semaphore_handle_t m = osal_semaphore_create_mutex();
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take(m, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_TIMEOUT, osal_semaphore_take(m, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_give(m));
    osal_semaphore_delete(m);
}

void test_recursive_mutex_basic(void)
{
    osal_semaphore_handle_t m = osal_semaphore_create_recursive_mutex();
    TEST_ASSERT_NOT_NULL(m);
    /* Unlike a plain mutex, the owner may take it again instead of blocking. */
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take_recursive(m, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take_recursive(m, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take_recursive(m, 0));
    /* Released only once every take is matched. */
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_give_recursive(m));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_give_recursive(m));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_give_recursive(m));
    /* Fully released: it can be taken afresh. */
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take_recursive(m, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_give_recursive(m));
    osal_semaphore_delete(m);
}

void test_recursive_mutex_unbalanced_give_fails(void)
{
    osal_semaphore_handle_t m = osal_semaphore_create_recursive_mutex();
    TEST_ASSERT_NOT_NULL(m);
    /* Nothing held yet. */
    TEST_ASSERT_EQUAL(OSAL_ERR_FAIL, osal_semaphore_give_recursive(m));

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take_recursive(m, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_give_recursive(m));
    /* One give too many. */
    TEST_ASSERT_EQUAL(OSAL_ERR_FAIL, osal_semaphore_give_recursive(m));
    osal_semaphore_delete(m);
}

void test_binary_basic(void)
{
    osal_semaphore_handle_t s = osal_semaphore_create_binary();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL(OSAL_ERR_TIMEOUT, osal_semaphore_take(s, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_give(s));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take(s, 0));
    osal_semaphore_delete(s);
}

void test_counting_basic(void)
{
    osal_semaphore_handle_t s = osal_semaphore_create_counting(2, 1);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take(s, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_TIMEOUT, osal_semaphore_take(s, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_give(s));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_give(s));
    /* overflow should fail */
    TEST_ASSERT_EQUAL(OSAL_ERR_FAIL, osal_semaphore_give(s));
    osal_semaphore_delete(s);
}

void test_semaphore_null_handle_contract(void)
{
    /* INVALID_ARG, not FAIL: FAIL already means "operation refused" above. On FreeRTOS
     * these used to hit configASSERT and take the system down. */
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, osal_semaphore_take(NULL, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, osal_semaphore_give(NULL));
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, osal_semaphore_take_recursive(NULL, 0));
    TEST_ASSERT_EQUAL(OSAL_ERR_INVALID_ARG, osal_semaphore_give_recursive(NULL));
    /* No-op, not a fault: reaching the next line is the assertion. */
    osal_semaphore_delete(NULL);
}
