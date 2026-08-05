/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmng_common_prototypes.h"

int test_rmng_common_all_tests_unity(void)
{
    UNITY_BEGIN();

    /* Credentials *****************************************************************/
    RUN_TEST(test_credentials_override);

    /* Work queue *****************************************************************/
    RUN_TEST(test_work_queue_basic);
    RUN_TEST(test_work_queue_error_paths);

    /* Utilities - convert ***********************************************************/
    RUN_TEST(test_rmaker_convert_bytes_to_hex);
    RUN_TEST(test_rmaker_convert_hex_to_bytes);
    RUN_TEST(test_rmaker_convert_hex_error_paths);

    /* Utilities - nvs ***********************************************************/
    RUN_TEST(test_rmaker_nvs_update_int_success);
    RUN_TEST(test_rmaker_nvs_update_int_null_params);
    RUN_TEST(test_rmaker_nvs_get_int_success);
    RUN_TEST(test_rmaker_nvs_get_int_not_found);
    RUN_TEST(test_rmaker_nvs_update_string_success);
    RUN_TEST(test_rmaker_nvs_update_string_null_params);
    RUN_TEST(test_rmaker_nvs_get_string_success);
    RUN_TEST(test_rmaker_nvs_get_string_not_found);
    RUN_TEST(test_rmaker_nvs_round_trip_int);
    RUN_TEST(test_rmaker_nvs_round_trip_string);
    RUN_TEST(test_rmaker_nvs_multiple_keys);
    RUN_TEST(test_rmaker_nvs_overwrite_values);
    RUN_TEST(test_rmaker_clear_nvs_namespace_removes_entries);
    RUN_TEST(test_rmaker_nvs_update_bool_success);
    RUN_TEST(test_rmaker_nvs_update_bool_invalid_params);
    RUN_TEST(test_rmaker_nvs_update_u16_and_get);
    RUN_TEST(test_rmaker_nvs_get_binary_with_handle);
    RUN_TEST(test_rmaker_nvs_update_binary_round_trip);
    RUN_TEST(test_rmaker_nvs_update_binary_overwrite);
    RUN_TEST(test_rmaker_nvs_update_binary_invalid_params);
    RUN_TEST(test_rmaker_nvs_update_binary_with_handle_round_trip);
    RUN_TEST(test_rmaker_nvs_update_binary_zero_length);

    /* Utilities - crypto ************************************************************/
    RUN_TEST(test_rmaker_sha256_basic);
    RUN_TEST(test_rmaker_sha256_errors);
    RUN_TEST(test_rmaker_sign_verify_basic);
    RUN_TEST(test_rmaker_sign_verify_errors);
    RUN_TEST(test_esp_key_bin_to_der_basic);
    RUN_TEST(test_rmaker_gen_key_pem_basic);
    RUN_TEST(test_rmaker_gen_key_pem_errors);
    RUN_TEST(test_rmaker_gen_csr_pem_basic);
    RUN_TEST(test_rmaker_csr_pem_max_len);
    RUN_TEST(test_rmaker_gen_csr_pem_errors);

    /* Retry **********************************************************************/
    RUN_TEST(test_backoff_retry_successful_schedule);

    return UNITY_END();
}
