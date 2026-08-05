/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include "unity.h"
#include "test_http_common_prototypes.h"

int test_http_common_all_tests_unity(void)
{
    UNITY_BEGIN();

    /* Basic HTTP tests */
    RUN_TEST(test_http_get_no_tls);
    RUN_TEST(test_http_post_no_tls);
    RUN_TEST(test_http_get_tls);
    RUN_TEST(test_http_post_tls);
    RUN_TEST(test_http_streaming);
    RUN_TEST(test_http_head_method);
    RUN_TEST(test_http_options_method);
    RUN_TEST(test_http_put_method);
    RUN_TEST(test_http_patch_method);
    RUN_TEST(test_http_delete_method);
    RUN_TEST(test_http_multiple_requests);

    return UNITY_END();
}
