/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include "unity.h"
#include "test_osal_http_prototypes.h"

int test_osal_http_all_tests_unity(void)
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

    /* Contract tests */
    RUN_TEST(test_http_contract_null_value_removes_header);
    RUN_TEST(test_http_contract_removing_absent_header_succeeds);
    RUN_TEST(test_http_contract_null_content_type_keeps_header);
    RUN_TEST(test_http_contract_body_before_method);
    RUN_TEST(test_http_contract_body_keeps_method);
    RUN_TEST(test_http_contract_empty_body_sends_length_zero);
    RUN_TEST(test_http_contract_method_not_stale);

    return UNITY_END();
}
