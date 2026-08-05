/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include "unity.h"
#include "test_http_common_prototypes.h"

void setUp(void)
{
    /* Setup code if needed */
}

void tearDown(void)
{
    /* Cleanup code if needed */
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    return test_http_common_all_tests_unity();
}
