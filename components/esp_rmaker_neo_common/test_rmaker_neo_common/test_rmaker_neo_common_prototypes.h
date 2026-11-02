/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_rmaker_neo_common_prototypes.h
 * @brief Prototypes for the RainMaker Neo common test suite.
 */

#ifndef __TEST_RMAKER_NEO_COMMON_PROTOTYPES_H__
#define __TEST_RMAKER_NEO_COMMON_PROTOTYPES_H__

/* Credentials *****************************************************************/

/**
 * @brief Test the basic functionality of the credentials override.
 */
void test_credentials_override(void);

/**
 * @brief Test that the factory client ID does not fall back to the certificate CN.
 */
void test_credentials_factory_client_id_no_cn_fallback(void);

/**
 * @brief Test that the factory client ID is the stored node ID.
 */
void test_credentials_factory_client_id_node_id(void);

/**
 * @brief Test that the factory client ID is not found without a node ID or a certificate.
 */
void test_credentials_factory_client_id_not_found(void);

/* Work queue *****************************************************************/

/**
 * @brief Test the basic functionality of the work queue.
 */
void test_work_queue_basic(void);

/**
 * @brief Test error paths of work queue lifecycle.
 */
void test_work_queue_error_paths(void);

/* Utilities - convert ***********************************************************/

/**
 * @brief Test the basic functionality of the bytes to hex conversion.
 */
void test_rmaker_convert_bytes_to_hex(void);

/**
 * @brief Test the basic functionality of the hex to bytes conversion.
 */
void test_rmaker_convert_hex_to_bytes(void);

/**
 * @brief Test error paths of hex conversion helpers.
 */
void test_rmaker_convert_hex_error_paths(void);

/* Utilities - nvs ***********************************************************/

/**
 * @brief Test the basic functionality of the nvs update int.
 */
void test_rmaker_nvs_update_int_success(void);

/**
 * @brief Test NULL parameters for nvs update int.
 */
void test_rmaker_nvs_update_int_null_params(void);

/**
 * @brief Test the basic functionality of the nvs get int.
 */
void test_rmaker_nvs_get_int_success(void);

/**
 * @brief Test nvs get int with key not found.
 */
void test_rmaker_nvs_get_int_not_found(void);

/**
 * @brief Test the basic functionality of the nvs update string.
 */
void test_rmaker_nvs_update_string_success(void);

/**
 * @brief Test NULL parameters for nvs update string.
 */
void test_rmaker_nvs_update_string_null_params(void);

/**
 * @brief Test the basic functionality of the nvs get string.
 */
void test_rmaker_nvs_get_string_success(void);

/**
 * @brief Test nvs get string with key not found.
 */
void test_rmaker_nvs_get_string_not_found(void);

/**
 * @brief Test round trip operations for int values.
 */
void test_rmaker_nvs_round_trip_int(void);

/**
 * @brief Test round trip operations for string values.
 */
void test_rmaker_nvs_round_trip_string(void);

/**
 * @brief Test storing multiple keys in same namespace.
 */
void test_rmaker_nvs_multiple_keys(void);

/**
 * @brief Test overwriting existing values.
 */
void test_rmaker_nvs_overwrite_values(void);

/**
 * @brief Test clearing an NVS namespace removes stored entries.
 */
void test_rmaker_clear_nvs_namespace_removes_entries(void);

/**
 * @brief Test the basic functionality of the nvs update bool.
 */
void test_rmaker_nvs_update_bool_success(void);

/**
 * @brief Test invalid parameters for nvs update bool.
 */
void test_rmaker_nvs_update_bool_invalid_params(void);

/**
 * @brief Test the basic functionality of the nvs update u16 and get.
 */
void test_rmaker_nvs_update_u16_and_get(void);

/**
 * @brief Test getting binary data with handle.
 */
void test_rmaker_nvs_get_binary_with_handle(void);

/**
 * @brief Test round trip of nvs_update_binary / nvs_get_binary.
 */
void test_rmaker_nvs_update_binary_round_trip(void);

/**
 * @brief Test overwriting a binary value via nvs_update_binary.
 */
void test_rmaker_nvs_update_binary_overwrite(void);

/**
 * @brief Test invalid-argument paths of nvs_update_binary variants.
 */
void test_rmaker_nvs_update_binary_invalid_params(void);

/**
 * @brief Test round trip of nvs_update_binary_with_handle.
 */
void test_rmaker_nvs_update_binary_with_handle_round_trip(void);

/**
 * @brief Test zero-length NULL payload accepted by nvs_update_binary.
 */
void test_rmaker_nvs_update_binary_zero_length(void);

/* Utilities - crypto ************************************************************/

/**
 * @brief Test the basic functionality of the SHA-256 hash function.
 */
void test_rmaker_sha256_basic(void);

/**
 * @brief Test the error paths of the SHA-256 hash function.
 */
void test_rmaker_sha256_errors(void);

/**
 * @brief Test signing and verifying for RSA and EC keys over test vectors.
 */
void test_rmaker_sign_verify_basic(void);

/**
 * @brief Test the error paths of the signing and verifying function.
 */
void test_rmaker_sign_verify_errors(void);

/**
 * @brief Test the basic functionality of the ESP binary key to DER conversion.
 */
void test_esp_key_bin_to_der_basic(void);

/**
 * @brief Test key generation for each supported key type, and that the type round-trips.
 */
void test_rmaker_gen_key_pem_basic(void);

/**
 * @brief Test the error paths of key generation.
 */
void test_rmaker_gen_key_pem_errors(void);

/**
 * @brief Test that generated CSRs parse and carry the requested Common Name.
 */
void test_rmaker_gen_csr_pem_basic(void);

/**
 * @brief Test the per-key-type PEM CSR bounds and the fallback for unknown types.
 */
void test_rmaker_csr_pem_max_len(void);

/**
 * @brief Test the error paths of CSR generation and key-type detection.
 */
void test_rmaker_gen_csr_pem_errors(void);

/**
 * @brief Test reading the subject Common Name of a certificate.
 */
void test_rmaker_get_cert_cn(void);

/* Retry **********************************************************************/

/**
 * @brief Test backoff retry behavior.
 */
void test_backoff_retry_successful_schedule(void);

/**
 * @brief Test that retry does not re-arm a timer that a concurrent reset cancels.
 */
void test_backoff_race_reset_vs_retry(void);

/**
 * @brief Test that two concurrent retries on an idle context schedule one timer.
 */
void test_backoff_race_concurrent_schedule(void);

/**
 * @brief Test that a reset cancels the timer of a retry that is in progress.
 */
void test_backoff_race_reset_during_retry(void);

/* rmaker_aws_sign osal port **************************************************/

/**
 * @brief Test that the port table sets its ops and is the same on each call.
 */
void test_aws_sign_port_table(void);

/**
 * @brief Test that set_header() refuses a content type that it must truncate.
 */
void test_aws_sign_port_long_content_type(void);

/**
 * @brief Test that the JSON op reads nested and root string fields.
 */
void test_aws_sign_port_json_reads_nested_and_root(void);

/**
 * @brief Test that the JSON op refuses malformed documents.
 */
void test_aws_sign_port_json_refuses_bad_documents(void);

/**
 * @brief Test that the length hint comes before the body.
 */
void test_aws_sign_port_hint_before_body(void);

/**
 * @brief Test that the length hint comes for an empty body.
 */
void test_aws_sign_port_hint_on_empty_body(void);

/**
 * @brief Test that an error from the hint aborts the perform.
 */
void test_aws_sign_port_hint_aborts(void);

/**
 * @brief Test that a POST sends its body and content type.
 */
void test_aws_sign_port_post_body_and_content_type(void);

/* All tests ******************************************************************/

/**
 * @brief Run all RainMaker Neo common tests.
 */
int test_rmaker_neo_common_all_tests_unity(void);

#endif /* __TEST_RMAKER_NEO_COMMON_PROTOTYPES_H__ */
