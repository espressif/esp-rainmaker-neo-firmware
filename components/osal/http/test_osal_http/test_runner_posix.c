/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include "unity.h"
#include "test_osal_http_prototypes.h"

void setUp(void)
{
    /* Setup code if needed */
}

void tearDown(void)
{
    test_osal_http_tear_down();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    return test_osal_http_all_tests_unity();
}
