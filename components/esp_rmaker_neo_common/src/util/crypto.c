/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file crypto.c
 * @brief Cryptographic utility functions.
 */

/* Include headers **************************************************************/

/* Standard C headers */
#include <string.h>
#include <inttypes.h>
#include <stdint.h>

/* Declarations */
#include "util/esp_rmaker_crypto.h"

/* mbedtls */
#include "mbedtls/build_info.h"
#include "mbedtls/pk.h"
#include "psa/crypto.h"
#include "mbedtls/asn1write.h"
#include "mbedtls/x509_csr.h"
#if MBEDTLS_VERSION_MAJOR < 4
#include "mbedtls/psa_util.h"
#endif /* MBEDTLS_VERSION_MAJOR < 4 */

/* Platform includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Key and CSR generation need the mbedTLS X.509/PK *writing* support, which is optional:
 * unconditionally enabled in esp_config.h before ESP-IDF v6.0, a Kconfig option from v6.0
 * on. Probe for it rather than assuming, so that builds which do not use claiming still
 * compile with those options off. */
#if defined(MBEDTLS_PEM_WRITE_C) && defined(MBEDTLS_PK_WRITE_C) && \
    defined(MBEDTLS_X509_CREATE_C) && defined(MBEDTLS_X509_CSR_WRITE_C)
#define RMAKER_CRYPTO_HAVE_CSR_WRITE 1
#endif

#if defined(CONFIG_RMAKER_CRYPTO_CSR_GENERATION) && !defined(RMAKER_CRYPTO_HAVE_CSR_WRITE)
#error "Key/CSR generation was requested but mbedTLS certificate-request writing is disabled. Enable MBEDTLS_PEM_WRITE_C, MBEDTLS_PK_WRITE_C, MBEDTLS_X509_CREATE_C and MBEDTLS_X509_CSR_WRITE_C (ESP-IDF: Component config > mbedTLS), or turn off whatever selected RMAKER_CRYPTO_CSR_GENERATION (e.g. claiming)."
#endif

static const char *TAG = "rmng_crypto";

/* Private function declarations *************************************************/

/**
 * @brief Parse a PEM/DER private key into an mbedtls_pk_context.
 *
 * Sole site of the mbedTLS 3.x/4.x divergence in private-key parsing: 4.0 dropped the
 * RNG callback pair from mbedtls_pk_parse_key(). Everything else in this file uses PSA
 * or the version-stable pk<->PSA bridge helpers.
 *
 * @param[out] pk The context to populate. Must be initialized by the caller, and freed by
 *                the caller regardless of the outcome.
 * @param[in] private_key The private key. For PEM, private_key_len must include the NUL.
 * @param[in] private_key_len The length of the private key.
 * @return ESP_RMAKER_OK on success, otherwise error code
 */
static esp_rmaker_error_t __crypto_parse_key(mbedtls_pk_context *pk, const uint8_t *private_key, size_t private_key_len);

/**
 * @brief Convert a PEM/DER private key into PSA key attributes and mbedtls_pk_context
 * @param[in] private_key The private key
 * @param[in] private_key_len The length of the private key
 * @param[in] usage The PSA key usage
 * @param[out] attributes The PSA key attributes
 * @param[out] pk The mbedtls_pk_context. Must be initialized and freed by the caller.
 * @return ESP_RMAKER_OK on success, otherwise error code
 */
static esp_rmaker_error_t __crypto_key_into_psa(const uint8_t *private_key, size_t private_key_len, psa_key_usage_t usage, psa_key_attributes_t *attributes, mbedtls_pk_context *pk);

/**
 * @brief Get the length of the content of a DER integer
 * @param[in] x The integer
 * @param[in] n The length of the integer
 * @return The length of the content of the integer
 */
static size_t __crypto_der_int_content_len_from_fixed(const uint8_t *x, size_t n);

/**
 * @brief Write a DER integer
 * @param[out] dst The destination buffer
 * @param[in,out] off The offset in the destination buffer
 * @param[in] x The integer
 * @param[in] n The length of the integer
 */
static void __crypto_der_write_int_from_fixed(uint8_t *dst, size_t *off, const uint8_t *x, size_t n);
/**
 * @brief Convert an ECDSA raw signature to DER format
 * @param[in] raw The raw signature
 * @param[in] raw_len The length of the raw signature
 * @param[in] key_bits The key bits
 * @param[out] der The DER signature. Must be freed by the caller.
 * @param[out] der_len The length of the DER signature
 * @return ESP_RMAKER_OK on success, otherwise error code
 */
static esp_rmaker_error_t __crypto_ecdsa_raw_to_der(const uint8_t *raw, size_t raw_len, size_t key_bits, uint8_t **der, size_t *der_len);

/**
 * @brief Check if the key is supported
 * @param[in] private_key The private key
 * @param[in] private_key_len The length of the private key
 * @param[in] usage The PSA key usage
 * @return True if the key is supported, false otherwise
 */
static bool __crypto_is_key_supported(const uint8_t *private_key, size_t private_key_len, psa_key_usage_t usage);

/**
 * @brief Classify a key from PSA attributes already derived from it
 * @param[in] attributes The PSA key attributes
 * @return The matching key type, or RMAKER_CRYPTO_KEY_TYPE_UNKNOWN
 */
static esp_rmaker_crypto_key_type_t __crypto_key_type_from_attributes(const psa_key_attributes_t *attributes);

/* Private function definitions **************************************************/

static esp_rmaker_error_t __crypto_parse_key(mbedtls_pk_context *pk, const uint8_t *private_key, size_t private_key_len)
{
    if (pk == NULL || private_key == NULL || private_key_len == 0) {
        OSAL_LOGE(TAG, "Invalid arguments: pk=%p, private_key=%p, private_key_len=%" PRIu32, pk, private_key, (uint32_t)private_key_len);
        return ESP_RMAKER_INVALID_ARG;
    }

    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        OSAL_LOGE(TAG, "Failed to initialize PSA crypto: %" PRId32, (int32_t)status);
        return ESP_RMAKER_INVALID_STATE;
    }

    OSAL_LOGD(TAG, "Parsing key (length: %" PRIu32 ")", (uint32_t)private_key_len);
    int ret;
#if MBEDTLS_VERSION_MAJOR >= 4
    ret = mbedtls_pk_parse_key(pk, private_key, private_key_len, NULL, 0);
#else
    /* Use the PSA-backed RNG rather than seeding a private CTR_DRBG: PSA is already
     * initialised above, and the entropy/CTR_DRBG modules are gone in mbedTLS 4.x. */
    ret = mbedtls_pk_parse_key(pk,
                               private_key,
                               private_key_len,
                               NULL, 0, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
#endif /* MBEDTLS_VERSION_MAJOR >= 4 */

    if (ret != 0) {
        OSAL_LOGE(TAG, "Failed to parse key: -0x%04X", -ret);
        return ESP_RMAKER_INVALID_STATE;
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __crypto_key_into_psa(const uint8_t *private_key, size_t private_key_len, psa_key_usage_t usage, psa_key_attributes_t *attributes, mbedtls_pk_context *pk)
{
    if (private_key == NULL || private_key_len == 0 || attributes == NULL || pk == NULL) {
        OSAL_LOGE(TAG, "Invalid arguments: private_key=%p, private_key_len=%" PRIu32 ", usage=%" PRIu32 ", attributes=%p, pk=%p", private_key, (uint32_t)private_key_len, (uint32_t)usage, attributes, pk);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Try parsing the key */
    esp_rmaker_error_t parse_err = __crypto_parse_key(pk, private_key, private_key_len);
    if (parse_err != ESP_RMAKER_OK) {
        return parse_err;
    }

    /* Get PSA key attributes */
    int ret = mbedtls_pk_get_psa_attributes(pk, usage, attributes);
    if (ret != 0) {
        OSAL_LOGE(TAG, "Failed to get PSA key attributes: -0x%04X", -ret);
        return ESP_RMAKER_INVALID_STATE;
    }

    return ESP_RMAKER_OK;
}

static esp_rmaker_crypto_key_type_t __crypto_key_type_from_attributes(const psa_key_attributes_t *attributes)
{
    psa_key_type_t psa_type = psa_get_key_type(attributes);
    size_t key_bits = psa_get_key_bits(attributes);
    if (PSA_KEY_TYPE_IS_ECC(psa_type) && key_bits == 256) {
        return RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256;
    }
    if (PSA_KEY_TYPE_IS_RSA(psa_type) && key_bits == 2048) {
        return RMAKER_CRYPTO_KEY_TYPE_RSA_2048;
    }
    OSAL_LOGW(TAG, "Unrecognised key: psa_type=0x%04X, bits=%" PRIu32,
              (unsigned)psa_type, (uint32_t)key_bits);
    return RMAKER_CRYPTO_KEY_TYPE_UNKNOWN;
}

static size_t __crypto_der_int_content_len_from_fixed(const uint8_t *x, size_t n)
{
    // Trim leading zeros
    size_t i = 0;
    while (i < n && x[i] == 0x00) {
        i++;
    }

    size_t vlen = (i == n) ? 1 : (n - i); // if all zeros => value is single 0x00

    // If highest bit set, need leading 0x00 to keep INTEGER positive
    uint8_t first = (i == n) ? 0x00 : x[i];
    if (first & 0x80) {
        vlen += 1;
    }

    return vlen;
}

static void __crypto_der_write_int_from_fixed(uint8_t *dst, size_t *off,
        const uint8_t *x, size_t n)
{
    size_t i = 0;
    while (i < n && x[i] == 0x00) {
        i++;
    }

    const uint8_t *v = (i == n) ? (const uint8_t *)"\x00" : (x + i);
    size_t vraw_len = (i == n) ? 1 : (n - i);

    // tag
    dst[(*off)++] = 0x02;

    // length and optional sign pad
    uint8_t first = v[0];
    bool need_pad = (first & 0x80) != 0;
    size_t vlen = vraw_len + (need_pad ? 1 : 0);

    dst[(*off)++] = (uint8_t)vlen; // assumes vlen < 128 (true for typical ECC sizes)

    if (need_pad) {
        dst[(*off)++] = 0x00;
    }
    memcpy(&dst[*off], v, vraw_len);
    *off += vraw_len;
}

static esp_rmaker_error_t __crypto_ecdsa_raw_to_der(const uint8_t *raw, size_t raw_len, size_t key_bits, uint8_t **der, size_t *der_len)
{
    if (raw == NULL || raw_len == 0 || key_bits == 0 || der == NULL || der_len == NULL) {
        OSAL_LOGE(TAG, "Invalid arguments: raw=%p, raw_len=%" PRIu32 ", key_bits=%" PRIu32 ", der=%p, der_len=%p", raw, (uint32_t)raw_len, (uint32_t)key_bits, der, der_len);
        return ESP_RMAKER_INVALID_ARG;
    }

    size_t n = (key_bits + 7) / 8;
    if (raw_len != 2 * n) {
        OSAL_LOGE(TAG, "Invalid raw signature length: %" PRIu32 " != %" PRIu32, (uint32_t)raw_len, (uint32_t)(2 * n));
        return ESP_RMAKER_INVALID_ARG;
    }

    const uint8_t *r = raw;
    const uint8_t *s = raw + n;

    size_t r_vlen = __crypto_der_int_content_len_from_fixed(r, n);
    size_t s_vlen = __crypto_der_int_content_len_from_fixed(s, n);

    // INTEGER overhead: tag(1)+len(1)+value(vlen) (assuming vlen < 128)
    size_t r_tlv = 2 + r_vlen;
    size_t s_tlv = 2 + s_vlen;
    size_t seq_len = r_tlv + s_tlv;

    // SEQUENCE overhead: tag(1)+len(1) (assuming seq_len < 128)
    if (seq_len >= 128) {
        OSAL_LOGE(TAG, "Sequence length too long: %" PRIu32 " >= 128", (uint32_t)seq_len);
        return ESP_RMAKER_INVALID_STATE; // extend if you ever hit huge curves
    }

    size_t out_len = 2 + seq_len;
    uint8_t *out = OSAL_MALLOC_EXTRAM(out_len);
    if (out == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for DER signature");
        return ESP_RMAKER_NO_MEM;
    }

    size_t off = 0;
    out[off++] = 0x30;              // SEQUENCE
    out[off++] = (uint8_t)seq_len;  // length

    __crypto_der_write_int_from_fixed(out, &off, r, n);
    __crypto_der_write_int_from_fixed(out, &off, s, n);

    *der = out;
    *der_len = out_len;
    return ESP_RMAKER_OK;
}

static bool __crypto_is_key_supported(const uint8_t *private_key, size_t private_key_len, psa_key_usage_t usage)
{
    if (private_key == NULL || private_key_len == 0) {
        OSAL_LOGE(TAG, "Invalid arguments: private_key=%p, private_key_len=%" PRIu32, private_key, (uint32_t)private_key_len);
        return false;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    esp_rmaker_error_t err = __crypto_key_into_psa(private_key, private_key_len, usage, &attributes, &pk);
    mbedtls_pk_free(&pk);
    if (err != ESP_RMAKER_OK) {
        // Failed to convert PEM private key into PSA key attributes
        return false;
    }

    psa_key_type_t key_type = psa_get_key_type(&attributes);
    return PSA_KEY_TYPE_IS_RSA(key_type) || PSA_KEY_TYPE_IS_ECC(key_type);
}

/* Public function definitions *************************************************/

bool esp_rmaker_crypto_is_key_supported_tls(const uint8_t *private_key, size_t private_key_len)
{
    return __crypto_is_key_supported(private_key, private_key_len, PSA_KEY_USAGE_SIGN_HASH);
}

esp_rmaker_error_t esp_rmaker_crypto_gen_sha256(const uint8_t *data, size_t data_len, uint8_t hash[RMAKER_CRYPTO_SHA256_HASH_LEN])
{
    if (data == NULL || hash == NULL) {
        OSAL_LOGE(TAG, "Invalid arguments: data=%p, hash=%p", data, hash);
        return ESP_RMAKER_INVALID_ARG;
    }

    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        OSAL_LOGE(TAG, "Failed to initialize PSA crypto: %" PRId32, (int32_t)status);
        return ESP_RMAKER_INVALID_STATE;
    }

    size_t hash_len = 0;
    status = psa_hash_compute(PSA_ALG_SHA_256, data, data_len, hash, RMAKER_CRYPTO_SHA256_HASH_LEN, &hash_len);
    if (status != PSA_SUCCESS) {
        OSAL_LOGE(TAG, "Failed to compute SHA-256 hash: %" PRId32, (int32_t)status);
        return ESP_RMAKER_FAIL;
    }
    if (hash_len != RMAKER_CRYPTO_SHA256_HASH_LEN) {
        OSAL_LOGE(TAG, "Failed to compute SHA-256 hash: hash length mismatch %" PRIu32 " != %" PRIu32, (uint32_t) hash_len, (uint32_t) RMAKER_CRYPTO_SHA256_HASH_LEN);
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_crypto_sign_data(const uint8_t *private_key,
        size_t private_key_len,
        const uint8_t *data,
        size_t data_len,
        uint8_t **signature,
        size_t *signature_len)
{
    if (private_key == NULL || private_key_len == 0 || data == NULL || signature == NULL || signature_len == NULL) {
        OSAL_LOGE(TAG, "Invalid arguments: private_key=%p, private_key_len=%" PRIu32 ", data=%p, signature=%p, signature_len=%p", private_key, (uint32_t)private_key_len, data, signature, signature_len);
        return ESP_RMAKER_INVALID_ARG;
    }

    int ret = 0;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    // signature buffer (handled by key type)
    uint8_t *signature_buf = NULL;
    size_t signature_buf_available = 0, signature_buf_written = 0;
    *signature_len = 0;

    mbedtls_svc_key_id_t key_id = 0;

    do {
        psa_key_usage_t usage = PSA_KEY_USAGE_SIGN_MESSAGE;
        psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

        /* Parse PEM private key */
        if (__crypto_key_into_psa(private_key, private_key_len, usage, &attributes, &pk) != ESP_RMAKER_OK) {
            ret = -1; break;
        }

        /* Check if key is supported for signing */
        if (!(psa_get_key_usage_flags(&attributes) & usage)) {
            OSAL_LOGE(TAG, "Key is not supported for signing");
            ret = -1; break;
        }

        /* Force the signing algorithm based on the key type */
        psa_key_type_t key_type = psa_get_key_type(&attributes);
        bool is_ecdsa = false;
        psa_algorithm_t alg = psa_get_key_algorithm(&attributes);
        if (PSA_KEY_TYPE_IS_RSA(key_type)) {
            alg = PSA_ALG_RSA_PSS(PSA_ALG_SHA_256);
        } else if (PSA_KEY_TYPE_IS_ECC(key_type)) {
            alg = PSA_ALG_ECDSA(PSA_ALG_SHA_256);
            is_ecdsa = true;
        } else {
            OSAL_LOGE(TAG, "Key type is not supported for signing: %" PRIu32, (uint32_t)key_type);
            ret = -1; break;
        }
        psa_set_key_algorithm(&attributes, alg);

        /* Load key into PSA */
        ret = mbedtls_pk_import_into_psa(&pk, &attributes, &key_id);
        if (ret != 0) {
            break;
        }

        /* Get signature algorithm and size */
        size_t key_bits = psa_get_key_bits(&attributes);

        signature_buf_available = PSA_SIGN_OUTPUT_SIZE(key_type, key_bits, alg);
        if (signature_buf_available == 0) {
            OSAL_LOGE(TAG, "Failed to get signature size");
            ret = -1; break;
        }

        /* Allocate signature buffer and sign */
        signature_buf = (uint8_t *)OSAL_CALLOC_EXTRAM(signature_buf_available, sizeof(uint8_t));
        if (signature_buf == NULL) {
            OSAL_LOGE(TAG, "OSAL_CALLOC_EXTRAM failed");
            ret = -1; break;
        }

        psa_status_t status = psa_sign_message(key_id,
                                               alg,
                                               data,
                                               data_len,
                                               signature_buf,
                                               signature_buf_available,
                                               &signature_buf_written);
        if (status != PSA_SUCCESS) {
            OSAL_LOGE(TAG, "psa_sign_message failed: %" PRId32, (int32_t)status);
            ret = -1; break;
        }

        /* Convert ECDSA raw signature to DER format */
        if (is_ecdsa) {
            uint8_t *der_signature_buf = NULL;
            size_t der_signature_buf_len = 0;
            if (__crypto_ecdsa_raw_to_der(signature_buf, signature_buf_written, key_bits, &der_signature_buf, &der_signature_buf_len) != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to convert ECDSA raw signature to DER");
                ret = -1; break;
            }
            free(signature_buf);
            signature_buf = der_signature_buf;
            signature_buf_available = der_signature_buf_len;
            signature_buf_written = der_signature_buf_len;
        }
    } while (0);

    /* Reallocate signature buffer if necessary */
    if (signature_buf_written > 0) {
        if (signature_buf_written < signature_buf_available) {
            uint8_t *new_signature_buf = (uint8_t *)OSAL_REALLOC_EXTRAM(signature_buf, signature_buf_written);
            // If the new buffer is not the same as the old one (old one is freed by realloc)
            if (new_signature_buf != NULL) {
                signature_buf = new_signature_buf;
            }
            // If the new buffer is NULL, realloc failed but we can still use the old one
        }

        /* Transfer ownership of signature buffer to caller */
        *signature = signature_buf;
        *signature_len = signature_buf_written;
    } else {
        OSAL_LOGE(TAG, "Failed to sign data: no signature buffer written");
        if (signature_buf) {
            free(signature_buf);
        }
        *signature = NULL;
        *signature_len = 0;
        ret = -1;
    }

    if (ret != 0) {
        return ESP_RMAKER_FAIL;
    }

    OSAL_LOGI(TAG, "passed crypto sign data");
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_crypto_esp_key_bin_to_der(const uint8_t *key, size_t key_len, uint8_t **der, size_t *der_len)
{
    if (key == NULL || der == NULL || der_len == NULL) {
        OSAL_LOGE(TAG, "Invalid arguments: key=%p, der=%p, der_len=%p", key, der, der_len);
        return ESP_RMAKER_INVALID_ARG;
    }

    if (key_len != 32) {
        OSAL_LOGE(TAG, "Invalid key length: %" PRIu32 " != 32", (uint32_t)key_len);
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    psa_status_t status;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id;

    // 1. Initialize PSA (required before any crypto calls)
    status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        OSAL_LOGE(TAG, "Failed to initialize PSA crypto: %" PRId32, (int32_t)status);
        return ESP_RMAKER_INVALID_STATE;
    }

    // 2. Configure Key Attributes
    // We specify ECC SECP256R1 and allow exporting to DER
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_ANY_HASH));

    // 3. Get the public key from the private key
    status = psa_import_key(&attributes, key, key_len, &key_id);
    if (status != PSA_SUCCESS) {
        OSAL_LOGE(TAG, "Failed to import key: %" PRId32, (int32_t)status);
        err = ESP_RMAKER_FAIL;
        goto esp_rmaker_crypto_esp_key_bin_to_der_end;
    }
    uint8_t raw_pub[65];
    size_t pub_len = 0;
    status = psa_export_public_key(key_id, raw_pub, sizeof(raw_pub), &pub_len);
    if (status != PSA_SUCCESS) {
        OSAL_LOGE(TAG, "Failed to export public key: %" PRId32, (int32_t)status);
        err = ESP_RMAKER_FAIL;
        goto esp_rmaker_crypto_esp_key_bin_to_der_end;
    }

    // 4. Export the key to DER format
#define DER_BUF_LEN 130 // Generously allocate 130 bytes for the DER buffer
    uint8_t der_buf[DER_BUF_LEN];
    int ret = 0;

    // 4.1 Setup ASN.1 writing (mbedTLS writes right-to-left)
    uint8_t *p = der_buf + DER_BUF_LEN;
    size_t total_der_len = 0;

    do {
        // Step A: Write Public Key (Context Tag [1])
        size_t temp_len = 0;
        ret = mbedtls_asn1_write_bitstring(&p, der_buf, raw_pub, pub_len * 8);
        if (ret < 0) {
            OSAL_LOGE(TAG, "Failed to write public key: -0x%04X", -ret);
            break;
        }
        temp_len = ret;
        total_der_len += ret;
        ret = mbedtls_asn1_write_len(&p, der_buf, temp_len);
        if (ret < 0) {
            OSAL_LOGE(TAG, "Failed to write public key length: -0x%04X", -ret);
            break;
        }
        total_der_len += ret;
        ret = mbedtls_asn1_write_tag(&p, der_buf, MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_ASN1_CONSTRUCTED | 1);
        if (ret < 0) {
            OSAL_LOGE(TAG, "Failed to write public key tag: -0x%04X", -ret);
            break;
        }
        total_der_len += ret;

        // Step B: Write Curve OID (Context Tag [0]) for secp256r1
        // OID: 1.2.840.10045.3.1.7 (hex: 2A 86 48 CE 3D 03 01 07)
        const char secp256r1_oid[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
        ret = mbedtls_asn1_write_oid(&p, der_buf, secp256r1_oid, sizeof(secp256r1_oid));
        if (ret < 0) {
            OSAL_LOGE(TAG, "Failed to write curve OID: -0x%04X", -ret);
            break;
        }
        temp_len = ret;
        total_der_len += ret;
        ret = mbedtls_asn1_write_len(&p, der_buf, temp_len);
        if (ret < 0) {
            OSAL_LOGE(TAG, "Failed to write curve OID length: -0x%04X", -ret);
            break;
        }
        total_der_len += ret;
        ret = mbedtls_asn1_write_tag(&p, der_buf, MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_ASN1_CONSTRUCTED | 0);
        if (ret < 0) {
            OSAL_LOGE(TAG, "Failed to write curve OID tag: -0x%04X", -ret);
            break;
        }
        total_der_len += ret;

        // Step C: Write Private Key as Octet String
        ret = mbedtls_asn1_write_octet_string(&p, der_buf, key, key_len);
        if (ret < 0) {
            OSAL_LOGE(TAG, "Failed to write private key: -0x%04X", -ret);
            break;
        }
        total_der_len += ret;

        // Step D: Write Version (Integer 1)
        ret = mbedtls_asn1_write_int(&p, der_buf, 1);
        if (ret < 0) {
            OSAL_LOGE(TAG, "Failed to write private key length: -0x%04X", -ret);
            break;
        }
        total_der_len += ret;

        // Step E: Final Sequence Wrap
        ret = mbedtls_asn1_write_len(&p, der_buf, total_der_len);
        if (ret < 0) {
            OSAL_LOGE(TAG, "Failed to write private key tag: -0x%04X", -ret);
            break;
        }
        total_der_len += ret;
        ret = mbedtls_asn1_write_tag(&p, der_buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
        if (ret < 0) {
            OSAL_LOGE(TAG, "Failed to write sequence wrap: -0x%04X", -ret);
            break;
        }
        total_der_len += ret;
    } while (0);

    if (ret < 0) {
        err = ESP_RMAKER_FAIL;
        goto esp_rmaker_crypto_esp_key_bin_to_der_end;
    }

    /* Allocate memory for the DER key */
    *der = (uint8_t *)OSAL_MALLOC_EXTRAM(total_der_len * sizeof(uint8_t));
    if (*der == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for DER key (length: %" PRIu32 ")", (uint32_t)total_der_len);
        err = ESP_RMAKER_NO_MEM;
        goto esp_rmaker_crypto_esp_key_bin_to_der_end;
    }
    /* Copy the DER key to the output buffer; the DER buffer starts writing from the back of the buffer */
    memcpy(*der, der_buf + (sizeof(der_buf) - total_der_len), total_der_len);
    *der_len = total_der_len;
    err = ESP_RMAKER_OK;

esp_rmaker_crypto_esp_key_bin_to_der_end:
    // 5. Clean up the temporary key slot
    psa_destroy_key(key_id);
    psa_reset_key_attributes(&attributes);

    return err;
}

#ifndef RMAKER_CRYPTO_HAVE_CSR_WRITE

esp_rmaker_error_t esp_rmaker_crypto_gen_key_pem(esp_rmaker_crypto_key_type_t key_type,
        uint8_t **key_pem,
        size_t *key_pem_len)
{
    (void)key_type;
    (void)key_pem;
    (void)key_pem_len;
    OSAL_LOGE(TAG, "Key generation unavailable: mbedTLS PK/X.509 writing support is disabled");
    return ESP_RMAKER_NOT_SUPPORTED;
}

esp_rmaker_error_t esp_rmaker_crypto_gen_csr_pem(const uint8_t *key_pem,
        size_t key_pem_len,
        const char *common_name,
        uint8_t **csr_pem,
        size_t *csr_pem_len)
{
    (void)key_pem;
    (void)key_pem_len;
    (void)common_name;
    (void)csr_pem;
    (void)csr_pem_len;
    OSAL_LOGE(TAG, "CSR generation unavailable: mbedTLS PK/X.509 writing support is disabled");
    return ESP_RMAKER_NOT_SUPPORTED;
}

#else /* RMAKER_CRYPTO_HAVE_CSR_WRITE */

esp_rmaker_error_t esp_rmaker_crypto_gen_key_pem(esp_rmaker_crypto_key_type_t key_type,
        uint8_t **key_pem,
        size_t *key_pem_len)
{
    if (key_pem == NULL || key_pem_len == NULL) {
        OSAL_LOGE(TAG, "Invalid arguments: key_pem=%p, key_pem_len=%p", key_pem, key_pem_len);
        return ESP_RMAKER_INVALID_ARG;
    }
    *key_pem = NULL;
    *key_pem_len = 0;

    /* Map the requested key type onto PSA attributes. The PEM buffer is sized per key
     * type: an RSA-2048 PKCS#8 PEM runs ~1.7 kB, a P-256 one ~250 bytes. */
    psa_key_type_t psa_type;
    size_t key_bits;
    psa_algorithm_t alg;
    size_t pem_buf_len;
    switch (key_type) {
    case RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256:
        psa_type = PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1);
        key_bits = 256;
        alg = PSA_ALG_ECDSA(PSA_ALG_SHA_256);
        pem_buf_len = 512;
        break;
    case RMAKER_CRYPTO_KEY_TYPE_RSA_2048:
        psa_type = PSA_KEY_TYPE_RSA_KEY_PAIR;
        key_bits = 2048;
        alg = PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256);
        pem_buf_len = 2048;
        break;
    default:
        OSAL_LOGE(TAG, "Unsupported key type: %d", (int)key_type);
        return ESP_RMAKER_INVALID_ARG;
    }

    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        OSAL_LOGE(TAG, "Failed to initialize PSA crypto: %" PRId32, (int32_t)status);
        return ESP_RMAKER_INVALID_STATE;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, psa_type);
    psa_set_key_bits(&attributes, key_bits);
    psa_set_key_algorithm(&attributes, alg);
    /* EXPORT is required by mbedtls_pk_copy_from_psa() below. */
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);

    OSAL_LOGW(TAG, "Generating a %s private key. This may take time.",
              (key_type == RMAKER_CRYPTO_KEY_TYPE_RSA_2048) ? "RSA-2048" : "ECDSA P-256");

    mbedtls_svc_key_id_t key_id = 0;
    status = psa_generate_key(&attributes, &key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        OSAL_LOGE(TAG, "psa_generate_key failed: %" PRId32, (int32_t)status);
        return ESP_RMAKER_FAIL;
    }

    /* Move the key out of PSA into a transparent pk context so it can be serialised.
     * mbedtls_pk_copy_from_psa() is present and unchanged in both mbedTLS 3.6 and 4.x,
     * so this needs no version guard. */
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    uint8_t *pem = NULL;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_copy_from_psa(key_id, &pk);
    psa_destroy_key(key_id);
    if (ret != 0) {
        OSAL_LOGE(TAG, "mbedtls_pk_copy_from_psa failed: -0x%04X", -ret);
        err = ESP_RMAKER_FAIL;
        goto esp_rmaker_crypto_gen_key_pem_end;
    }

    pem = (uint8_t *)OSAL_CALLOC_EXTRAM(pem_buf_len, sizeof(uint8_t));
    if (pem == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate %" PRIu32 " bytes for the PEM key", (uint32_t)pem_buf_len);
        err = ESP_RMAKER_NO_MEM;
        goto esp_rmaker_crypto_gen_key_pem_end;
    }

    /* Writes a NUL-terminated PEM string into the buffer and returns 0 on success. */
    ret = mbedtls_pk_write_key_pem(&pk, pem, pem_buf_len);
    if (ret != 0) {
        OSAL_LOGE(TAG, "mbedtls_pk_write_key_pem failed: -0x%04X", -ret);
        err = ESP_RMAKER_FAIL;
        goto esp_rmaker_crypto_gen_key_pem_end;
    }

    /* Report the length including the NUL terminator, matching the convention of the
     * factory-partition credential getters (see __get_pem_adjusted_credential). */
    *key_pem = pem;
    *key_pem_len = strlen((const char *)pem) + 1;
    pem = NULL; /* ownership transferred */

esp_rmaker_crypto_gen_key_pem_end:
    if (pem != NULL) {
        free(pem);
    }
    mbedtls_pk_free(&pk);
    return err;
}

esp_rmaker_error_t esp_rmaker_crypto_gen_csr_pem(const uint8_t *key_pem,
        size_t key_pem_len,
        const char *common_name,
        uint8_t **csr_pem,
        size_t *csr_pem_len)
{
    if (key_pem == NULL || key_pem_len == 0 || common_name == NULL || csr_pem == NULL || csr_pem_len == NULL) {
        OSAL_LOGE(TAG, "Invalid arguments: key_pem=%p, key_pem_len=%" PRIu32 ", common_name=%p, csr_pem=%p, csr_pem_len=%p",
                  key_pem, (uint32_t)key_pem_len, common_name, csr_pem, csr_pem_len);
        return ESP_RMAKER_INVALID_ARG;
    }
    *csr_pem = NULL;
    *csr_pem_len = 0;

    /* "CN=" + the Common Name. Node IDs are well under this, but bail rather than
     * silently emitting a CSR with a truncated subject. */
    char subject_name[80];
    int subject_len = snprintf(subject_name, sizeof(subject_name), "CN=%s", common_name);
    if (subject_len < 0 || (size_t)subject_len >= sizeof(subject_name)) {
        OSAL_LOGE(TAG, "Common name too long for the CSR subject: %d bytes", subject_len);
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    uint8_t *pem = NULL;
    int ret = 0;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    mbedtls_x509write_csr csr;
    mbedtls_x509write_csr_init(&csr);

    err = __crypto_parse_key(&pk, key_pem, key_pem_len);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to parse the private key for the CSR");
        goto esp_rmaker_crypto_gen_csr_pem_end;
    }

    /* Size the PEM buffer from the key type rather than from one constant for every key: an
     * ECDSA CSR needs a quarter of what RSA does. Taken from the key just parsed, so this
     * costs a set of PSA attributes rather than a second parse of the same PEM. */
    size_t pem_len = RMAKER_CRYPTO_CSR_PEM_LEN_MAX;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    if (mbedtls_pk_get_psa_attributes(&pk, PSA_KEY_USAGE_SIGN_HASH, &attributes) == 0) {
        pem_len = esp_rmaker_crypto_csr_pem_max_len(__crypto_key_type_from_attributes(&attributes));
        psa_reset_key_attributes(&attributes);
    } else {
        /* Keep going with the largest bound: whatever stops the attributes being derived will
         * stop the CSR being written too, and that error is the more useful one to report. */
        OSAL_LOGW(TAG, "Could not classify the key for CSR sizing; using the largest bound.");
    }

    mbedtls_x509write_csr_set_md_alg(&csr, MBEDTLS_MD_SHA256);
    mbedtls_x509write_csr_set_key(&csr, &pk);

    ret = mbedtls_x509write_csr_set_subject_name(&csr, subject_name);
    if (ret != 0) {
        OSAL_LOGE(TAG, "mbedtls_x509write_csr_set_subject_name failed: -0x%04X", -ret);
        err = ESP_RMAKER_FAIL;
        goto esp_rmaker_crypto_gen_csr_pem_end;
    }

    pem = (uint8_t *)OSAL_CALLOC_EXTRAM(pem_len, sizeof(uint8_t));
    if (pem == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate %" PRIu32 " bytes for the PEM CSR", (uint32_t)pem_len);
        err = ESP_RMAKER_NO_MEM;
        goto esp_rmaker_crypto_gen_csr_pem_end;
    }

    /* mbedTLS 4.0 dropped the RNG callback pair; before that, use the PSA-backed RNG
     * (PSA is already initialised by __crypto_parse_key above). */
#if MBEDTLS_VERSION_MAJOR >= 4
    ret = mbedtls_x509write_csr_pem(&csr, pem, pem_len);
#else
    ret = mbedtls_x509write_csr_pem(&csr, pem, pem_len,
                                    mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
#endif /* MBEDTLS_VERSION_MAJOR >= 4 */
    if (ret < 0) {
        OSAL_LOGE(TAG, "mbedtls_x509write_csr_pem failed: -0x%04X", -ret);
        err = ESP_RMAKER_FAIL;
        goto esp_rmaker_crypto_gen_csr_pem_end;
    }

    *csr_pem = pem;
    *csr_pem_len = strlen((const char *)pem) + 1;
    pem = NULL; /* ownership transferred */

esp_rmaker_crypto_gen_csr_pem_end:
    if (pem != NULL) {
        free(pem);
    }
    mbedtls_x509write_csr_free(&csr);
    mbedtls_pk_free(&pk);
    return err;
}

#endif /* RMAKER_CRYPTO_HAVE_CSR_WRITE */

esp_rmaker_error_t esp_rmaker_crypto_get_key_type(const uint8_t *private_key,
        size_t private_key_len,
        esp_rmaker_crypto_key_type_t *p_key_type)
{
    if (private_key == NULL || private_key_len == 0 || p_key_type == NULL) {
        OSAL_LOGE(TAG, "Invalid arguments: private_key=%p, private_key_len=%" PRIu32 ", p_key_type=%p",
                  private_key, (uint32_t)private_key_len, p_key_type);
        return ESP_RMAKER_INVALID_ARG;
    }
    *p_key_type = RMAKER_CRYPTO_KEY_TYPE_UNKNOWN;

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    esp_rmaker_error_t err = __crypto_key_into_psa(private_key, private_key_len, PSA_KEY_USAGE_SIGN_HASH, &attributes, &pk);
    if (err != ESP_RMAKER_OK) {
        mbedtls_pk_free(&pk);
        return err;
    }

    *p_key_type = __crypto_key_type_from_attributes(&attributes);

    psa_reset_key_attributes(&attributes);
    mbedtls_pk_free(&pk);
    return ESP_RMAKER_OK;
}

size_t esp_rmaker_crypto_csr_pem_max_len(esp_rmaker_crypto_key_type_t key_type)
{
    switch (key_type) {
    case RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256:
        return RMAKER_CRYPTO_CSR_PEM_LEN_ECDSA_P256;
    case RMAKER_CRYPTO_KEY_TYPE_RSA_2048:
        return RMAKER_CRYPTO_CSR_PEM_LEN_RSA_2048;
    default:
        /* A key this module does not classify can still sign a CSR, so answer with the
         * largest bound rather than refusing to size a buffer for it. */
        return RMAKER_CRYPTO_CSR_PEM_LEN_MAX;
    }
}
