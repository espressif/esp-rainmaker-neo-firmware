/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_crypto.h
 * @brief Cryptographic utility functions.
 */

#ifndef __ESP_RMAKER_CRYPTO_H__
#define __ESP_RMAKER_CRYPTO_H__

/* Standard C headers */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Error types */
#include "esp_rmaker_error_types.h"

/* Pre-processor definitions *****************************************************/

#define RMAKER_CRYPTO_SHA256_HASH_LEN 32

/**
 * @name PEM CSR buffer sizes
 *
 * Upper bounds on a PEM CSR, per key type, including the NUL and enough room for the newline
 * escaping a JSON payload needs (one byte per line). A caller that knows its key type at
 * compile time can size a buffer from these; ::esp_rmaker_crypto_csr_pem_max_len does the same
 * at run time.
 *
 * Measured with a 22-character node ID as the subject Common Name: ECDSA P-256 is 375 bytes
 * raw and 382 escaped, RSA-2048 is 907 and 922. A longer Common Name costs roughly a byte and
 * a half per character, and the subject is capped at 80 bytes by the generator, so the slack
 * here covers any node ID.
 * @{
 */
#define RMAKER_CRYPTO_CSR_PEM_LEN_ECDSA_P256 512
#define RMAKER_CRYPTO_CSR_PEM_LEN_RSA_2048   1280
/** Fallback for a key whose type is not in ::esp_rmaker_crypto_key_type_t. Sized for RSA-3072
 * (1257 raw, 1278 escaped), the next size up that a pre-flashed key might plausibly be. */
#define RMAKER_CRYPTO_CSR_PEM_LEN_MAX        2048
/** @} */

/* Types *************************************************************************/

/**
 * @brief Private key type.
 *
 * Used both to request a key type from ::esp_rmaker_crypto_gen_key_pem and to report
 * the type of an existing key from ::esp_rmaker_crypto_get_key_type.
 *
 * @note Not an exhaustive list of usable key types, only of the ones generated and detected
 *       here. Extend it if more types need to be detected.
 */
typedef enum {
    RMAKER_CRYPTO_KEY_TYPE_UNKNOWN = 0, /**< Unrecognised or unsupported key type. */
    RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256,  /**< ECDSA, NIST P-256 (secp256r1). */
    RMAKER_CRYPTO_KEY_TYPE_RSA_2048,    /**< RSA, 2048-bit. */
} esp_rmaker_crypto_key_type_t;

/* Public function declarations *************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if the key is supported for encryption.
 * @param[in] private_key The private key
 * @param[in] private_key_len The length of the private key
 * @return True if the key is supported, false otherwise
 */
bool esp_rmaker_crypto_is_key_supported_tls(const uint8_t *private_key, size_t private_key_len);

/**
 * @brief Generate a new private key and return it PEM-encoded.
 *
 * The key is generated via PSA and then serialised to PEM, so the result is usable
 * anywhere a PEM private key is expected (TLS, ::esp_rmaker_crypto_sign_data,
 * ::esp_rmaker_crypto_gen_csr_pem).
 *
 * @note This is slow. RSA-2048 generation in particular can take tens of seconds on
 *       ESP32, so call it from a task with a low priority and a large stack.
 *
 * @param[in]  key_type    The type of key to generate.
 * @param[out] key_pem     The PEM-encoded private key, NUL-terminated. Must be freed by the caller.
 * @param[out] key_pem_len Length of the PEM buffer *including* the NUL terminator, matching the
 *                         convention used by the factory-partition credential getters.
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG on invalid args,
 *         ESP_RMAKER_NO_MEM on allocation failure, ESP_RMAKER_FAIL otherwise.
 */
esp_rmaker_error_t esp_rmaker_crypto_gen_key_pem(esp_rmaker_crypto_key_type_t key_type,
        uint8_t **key_pem,
        size_t *key_pem_len);

/**
 * @brief Generate a PEM-encoded Certificate Signing Request for the given private key.
 *
 * The CSR is signed with SHA-256 and carries a single subject attribute, `CN=<common_name>`.
 *
 * @note Generating a CSR needs a sizeable stack; do not call this from a thread with a small
 *       one (e.g. a protocomm endpoint handler).
 *
 * @param[in]  key_pem     PEM-encoded private key. @p key_pem_len must include the NUL
 *                         terminator, as produced by ::esp_rmaker_crypto_gen_key_pem and by
 *                         the factory-partition credential getters.
 * @param[in]  key_pem_len Length of @p key_pem including the NUL terminator.
 * @param[in]  common_name The Common Name to put in the CSR subject.
 * @param[out] csr_pem     The PEM-encoded CSR, NUL-terminated. Must be freed by the caller.
 * @param[out] csr_pem_len Length of the CSR buffer *including* the NUL terminator.
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG on invalid args,
 *         ESP_RMAKER_NO_MEM on allocation failure, ESP_RMAKER_FAIL otherwise.
 */
esp_rmaker_error_t esp_rmaker_crypto_gen_csr_pem(const uint8_t *key_pem,
        size_t key_pem_len,
        const char *common_name,
        uint8_t **csr_pem,
        size_t *csr_pem_len);

/**
 * @brief Determine the type of a private key.
 *
 * @param[in]  private_key     PEM- or DER-encoded private key. For PEM, @p private_key_len
 *                             must include the NUL terminator.
 * @param[in]  private_key_len The length of the private key.
 * @param[out] p_key_type      The detected key type. Set to ::RMAKER_CRYPTO_KEY_TYPE_UNKNOWN
 *                             if the key parses but is not one of the supported types.
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG on invalid args,
 *         ESP_RMAKER_INVALID_STATE if the key could not be parsed.
 */
esp_rmaker_error_t esp_rmaker_crypto_get_key_type(const uint8_t *private_key,
        size_t private_key_len,
        esp_rmaker_crypto_key_type_t *p_key_type);

/**
 * @brief Upper bound on the PEM CSR a given key type produces.
 *
 * Includes the NUL and room for JSON newline escaping, so one buffer of this size holds the
 * CSR both as written and as escaped. See @ref RMAKER_CRYPTO_CSR_PEM_LEN_ECDSA_P256 and
 * friends for where the numbers come from.
 *
 * @param[in] key_type The key type the CSR will be signed with.
 * @return The buffer size to use, or ::RMAKER_CRYPTO_CSR_PEM_LEN_MAX for a key type this
 *         module does not recognise -- a bound has to be returned either way, and the
 *         generator fails cleanly if even that is too small.
 */
size_t esp_rmaker_crypto_csr_pem_max_len(esp_rmaker_crypto_key_type_t key_type);

/**
 * @brief Generate a SHA-256 hash of the given data.
 *
 * @param[in] data The data to hash.
 * @param[in] data_len The length of the data to hash.
 * @param[out] hash The hash to store the result in. Must be pre-allocated.
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if the data is NULL, error code otherwise.
 */
esp_rmaker_error_t esp_rmaker_crypto_gen_sha256(const uint8_t *data, size_t data_len, uint8_t hash[RMAKER_CRYPTO_SHA256_HASH_LEN]);

/**
 * @brief Sign data using the given PEM-encoded private key (supports RSA and EC).
 *
 * The data is hashed with SHA-256 and the resulting digest is signed using the
 * algorithm appropriate for the key type. The signature format depends on the key:
 * - RSA: raw signature, length equals the key size in bytes
 * - EC (e.g., P-256): ASN.1 DER-encoded ECDSA signature (variable length)
 *
 * @param[in]  private_key      PEM-encoded private key string (null-terminated).
 * @param[in]  private_key_len  Length of the private key in bytes.
 * @param[in]  data             Data to sign.
 * @param[in]  data_len         Length of the data to sign in bytes.
 * @param[out] signature        Pointer to the output buffer for the signature. The caller is responsible for freeing the buffer if not NULL.
 * @param[out] signature_len    On success, number of bytes written to signature.
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG on invalid args, ESP_RMAKER_FAIL otherwise.
 */
esp_rmaker_error_t esp_rmaker_crypto_sign_data(const uint8_t *private_key,
        size_t private_key_len,
        const uint8_t *data,
        size_t data_len,
        uint8_t **signature,
        size_t *signature_len);

/**
 * @brief Convert an ESP binary key to a DER key.
 *
 * The key is expected to be in this format:
 * - ECDSA, NIST P-256
 * - Exactly 32 bytes long
 * - Big-endian raw integer format
 *
 * @param[in] key The binary key
 * @param[in] key_len The length of the binary key
 * @param[out] der The DER key. Must be freed by the caller.
 * @param[out] der_len The length of the DER key
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG on invalid args, ESP_RMAKER_FAIL otherwise.
 */
esp_rmaker_error_t esp_rmaker_crypto_esp_key_bin_to_der(const uint8_t *key, size_t key_len, uint8_t **der, size_t *der_len);

#ifdef __cplusplus
}
#endif

#endif // __ESP_RMAKER_CRYPTO_H__
