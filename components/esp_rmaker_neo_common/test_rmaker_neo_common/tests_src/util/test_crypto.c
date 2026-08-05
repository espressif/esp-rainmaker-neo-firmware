/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmng_common_prototypes.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "util/esp_rmaker_crypto.h"

/* mbedTLS includes for key generation and verification */
#include "psa/crypto.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_csr.h"

/* Test data ******************************************************************/

#define TEST_DATA_COUNT 5

/* Test key types - using PSA constants for unified API */
static const psa_key_type_t test_key_types[2] = {
    PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1),
    PSA_KEY_TYPE_RSA_KEY_PAIR
};

static const char *test_data_input[TEST_DATA_COUNT] = {
    "tF8xXGL73zKcnGftsWty6COFEgt3QyA4i5P8ykK1GV3NyyxPmWl35LSXDIUUXGjkDIkmD6212k9k1oK6KEtFx2SxZckDEUttBpKM7vLmd2cTcfQSeYF4EdoC98DcQOYwOM56j1W55ivcuyDbqWtNW85wclDPwKyFAbuNRkVfao7WtD9B7pLoUQyCRt3cSPn9jizXvgL9",
    "j3bmYHKyTtzWk9UnkqojbV5nA9QmU0QAO1DpmhqdbOByf7EuodyumwlYBg4nrIItnnZETHsZekNsDAy2xESfytdTKrtEvFOOzPWcuMee6FjiEvPHjNwThf8ORnZZoVUvh93S",
    "OEqcrDhDApURJrORRGnJtTPDumNYLkPfcFqGgeVYgCF31KT2zHoby1roVGpo1H80fYzjQL1Bq6MWpe74b72ht2VWPD8Sfm12X8cp0lHi3F27csSGsm2u2X8W",
    "o5pUjwqDqBYEDgAjQfGpZhQSlLJCzNVBvK2SgLIwtMGGJm9WzZxoU323eUYH57oo8WSyj3XtzuY0UV0RUpQ4timIZ7KclET9T25HYfmiI70KGTMQtCRXYcGlyYIOLaM4ZmPQYlRupZsDvLqCWEvHLDV60Q4v",
    "SXjcTKHvCSJ2vA2BPfxda0rK7YHwtcDtbocH4DeuNc7OQQVV8ndaMDltzGsj5Wa6aGYUOykRPHQB65r9m3rxAfu9qVpcP2HjHGdX7svxHwfu02zobMcUPV4UQ3bKvABbtmQs"
};

static const uint8_t test_data_expected_sha256[TEST_DATA_COUNT][32] = {
    {
        0x64, 0xea, 0x3c, 0x6e, 0x59, 0x21, 0x23, 0xb9,
        0xf6, 0xdd, 0x62, 0x66, 0xa2, 0x1b, 0x4c, 0x81,
        0x93, 0x89, 0x24, 0x93, 0xfd, 0x91, 0xa2, 0x7b,
        0x66, 0x73, 0xa8, 0x4e, 0x61, 0xb7, 0x84, 0x1f
    },
    {
        0xd6, 0xcf, 0x5e, 0x68, 0x58, 0x09, 0xa4, 0xfe,
        0xff, 0x5b, 0x39, 0xf1, 0x09, 0xe0, 0xed, 0xa6,
        0x97, 0x84, 0x43, 0xfe, 0x11, 0x7a, 0x08, 0x02,
        0x17, 0x4e, 0xa4, 0x34, 0x3c, 0x87, 0xe9, 0xdf
    },
    {
        0xf6, 0xfe, 0x9d, 0x0a, 0x18, 0x59, 0xcd, 0xe6,
        0xff, 0x8b, 0x39, 0xaf, 0x8d, 0xe1, 0x15, 0x1f,
        0x19, 0xc8, 0x0f, 0x95, 0xeb, 0xb0, 0xd9, 0x7e,
        0xea, 0x25, 0xba, 0x5a, 0x71, 0xe6, 0x51, 0x59
    },
    {
        0xef, 0xca, 0x5e, 0x82, 0xca, 0x20, 0xbb, 0x7f,
        0xbc, 0x35, 0x8c, 0x8c, 0xd8, 0x82, 0x78, 0x64,
        0x5f, 0xa4, 0x64, 0x9d, 0x0b, 0x51, 0x8f, 0xdc,
        0xbe, 0x20, 0xee, 0x03, 0x9a, 0x1c, 0x3d, 0x36
    },
    {
        0xf7, 0x3f, 0xb9, 0x02, 0x72, 0x75, 0x17, 0xbe,
        0xd9, 0x74, 0x61, 0x55, 0xf0, 0x26, 0xa0, 0xf8,
        0x52, 0xbf, 0x09, 0x5e, 0x83, 0xa6, 0xfd, 0xd5,
        0x40, 0x97, 0xb6, 0xcb, 0x18, 0xb8, 0xa5, 0xfc
    }
};

static int __asn1_read_len(const uint8_t *buf, size_t buf_len, size_t *off, size_t *len)
{
    if (*off >= buf_len) {
        return -1;
    }
    uint8_t b = buf[(*off)++];

    if ((b & 0x80) == 0) {          // short form
        *len = b;
        return (*off + *len <= buf_len) ? 0 : -1;
    }

    size_t num = b & 0x7F;          // long form
    if (num == 0 || num > sizeof(size_t) || *off + num > buf_len) {
        return -1;
    }

    size_t v = 0;
    for (size_t i = 0; i < num; i++) {
        v = (v << 8) | buf[(*off)++];
    }

    *len = v;
    return (*off + *len <= buf_len) ? 0 : -1;
}

static int __asn1_read_integer_bytes(const uint8_t *buf, size_t buf_len, size_t *off,
                                     const uint8_t **val, size_t *val_len)
{
    if (*off >= buf_len || buf[*off] != 0x02) {
        return -1;    // INTEGER
    }
    (*off)++;

    size_t len = 0;
    if (__asn1_read_len(buf, buf_len, off, &len) != 0) {
        return -1;
    }
    if (*off + len > buf_len) {
        return -1;
    }

    *val = &buf[*off];
    *val_len = len;
    *off += len;
    return 0;
}

/**
 * Convert DER ECDSA signature to raw r||s.
 * key_bits is the curve size (e.g., 256/384/521), n = ceil(key_bits/8).
 */
int __ecdsa_sig_der_to_raw_rs(const uint8_t *der, size_t der_len,
                              size_t key_bits,
                              uint8_t **raw, size_t *raw_len)
{
    if (!der || !raw || !raw_len) {
        return -1;
    }

    size_t n = (key_bits + 7) / 8;
    *raw = NULL; *raw_len = 0;

    size_t off = 0;
    if (der_len < 2 || der[off++] != 0x30) {
        return -1;    // SEQUENCE
    }

    size_t seq_len = 0;
    if (__asn1_read_len(der, der_len, &off, &seq_len) != 0) {
        return -1;
    }
    if (off + seq_len != der_len) {
        // allow trailing bytes only if you want; strict DER typically equals
        return -1;
    }

    const uint8_t *r_ptr, *s_ptr;
    size_t r_len, s_len;
    if (__asn1_read_integer_bytes(der, der_len, &off, &r_ptr, &r_len) != 0) {
        return -1;
    }
    if (__asn1_read_integer_bytes(der, der_len, &off, &s_ptr, &s_len) != 0) {
        return -1;
    }
    if (off != der_len) {
        return -1;
    }

    // INTEGER may have a leading 0x00 to force positive.
    while (r_len > 0 && *r_ptr == 0x00) {
        r_ptr++;
        r_len--;
    }
    while (s_len > 0 && *s_ptr == 0x00) {
        s_ptr++;
        s_len--;
    }

    if (r_len > n || s_len > n) {
        return -1;    // doesn't fit expected curve size
    }

    uint8_t *out = (uint8_t *)malloc(2 * n);
    if (!out) {
        return -1;
    }
    memset(out, 0, 2 * n);

    // left-pad into fixed n bytes
    memcpy(out + (n - r_len), r_ptr, r_len);
    memcpy(out + n + (n - s_len), s_ptr, s_len);

    *raw = out;
    *raw_len = 2 * n;
    return 0;
}

static void __sha256_basic(const char *input, const uint8_t *expected_output)
{
    uint8_t output[32];
    esp_rmaker_error_t err = esp_rmaker_crypto_gen_sha256((const uint8_t *) input, strlen(input), output);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "SHA-256 generation failed");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_output, output, 32, "SHA-256 mismatch");
}

void test_rmaker_sha256_basic(void)
{
    for (int i = 0; i < TEST_DATA_COUNT; i++) {
        __sha256_basic(test_data_input[i], test_data_expected_sha256[i]);
    }
}

void test_rmaker_sha256_errors(void)
{
    esp_rmaker_error_t err;
    uint8_t output[32];

    err = esp_rmaker_crypto_gen_sha256(NULL, 0, output);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "SHA-256 generation should fail with NULL data");

    err = esp_rmaker_crypto_gen_sha256((const uint8_t *) "test", 0, NULL);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "SHA-256 generation should fail with NULL output");
}

/* Signing and verification test ************************************************/

static void __gen_key(psa_key_type_t type, psa_key_id_t *key_id, psa_key_attributes_t *attributes,
                      unsigned char *priv_pem, size_t priv_pem_size)
{
    psa_status_t status;

    /* Ensure PSA subsystem is initialized */
    status = psa_crypto_init();
    TEST_ASSERT_EQUAL(PSA_SUCCESS, status);

    /* Configure Key Attributes */
    psa_set_key_type(attributes, type);
    psa_set_key_usage_flags(attributes, PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE | PSA_KEY_USAGE_EXPORT);

    if (PSA_KEY_TYPE_IS_RSA(type)) {
        /* Match legacy RSA generation: 2048 bits [1] */
        psa_set_key_bits(attributes, 2048);

        /* Match legacy padding: MBEDTLS_RSA_PKCS_V21 (PSS) with SHA-256 [1] */
        psa_set_key_algorithm(attributes, PSA_ALG_RSA_PSS(PSA_ALG_SHA_256));
    } else if (PSA_KEY_TYPE_IS_ECC(type)) {
        /* Match legacy EC generation: SECP256R1 (P-256) [1] */
        psa_set_key_bits(attributes, 256);
        psa_set_key_algorithm(attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    }

    /* Generate the key */
    status = psa_generate_key(attributes, key_id);
    TEST_ASSERT_EQUAL(PSA_SUCCESS, status);

    /* Copy out to a pk context */
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    status = mbedtls_pk_copy_from_psa(*key_id, &pk);
    TEST_ASSERT_EQUAL(PSA_SUCCESS, status);

    /* Write to PEM */
    int ret = mbedtls_pk_write_key_pem(&pk, priv_pem, priv_pem_size);
    TEST_ASSERT_EQUAL_MESSAGE(0, ret, "PEM conversion failed");

    mbedtls_pk_free(&pk);
}

static void __sign_verify_basic(const char *input)
{
    size_t input_len = strlen(input);

    /* Initialize PSA crypto */
    psa_status_t status = psa_crypto_init();
    TEST_ASSERT_EQUAL_MESSAGE(PSA_SUCCESS, status, "PSA crypto initialization failed");

    /* Helper lambda to sign and verify for a given key type */
    for (int kt = 0; kt < sizeof(test_key_types) / sizeof(test_key_types[0]); kt++) {
        psa_key_type_t key_type = test_key_types[kt];

        psa_key_id_t key_id = 0;
        psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
        size_t priv_pem_size = 4096;
        unsigned char *priv_pem = (unsigned char *) malloc(priv_pem_size);
        TEST_ASSERT_NOT_NULL_MESSAGE(priv_pem, "malloc priv_pem failed");
        __gen_key(key_type, &key_id, &attributes, priv_pem, priv_pem_size);
        size_t priv_pem_len = strlen((const char *) priv_pem) + 1;

        /* Sign using esp_rmaker_crypto_sign_data */
        unsigned char *signature = NULL;
        size_t signature_len = 0;
        esp_rmaker_error_t err = esp_rmaker_crypto_sign_data(priv_pem,
                                 priv_pem_len,
                                 (const uint8_t *) input, input_len,
                                 &signature, &signature_len);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "sign_data failed");
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, signature_len, "empty signature");

        /* Verify signature */
        psa_algorithm_t alg = psa_get_key_algorithm(&attributes);
        if (PSA_KEY_TYPE_IS_ECC(key_type)) {
            uint8_t *raw_signature = NULL;
            size_t raw_signature_len = 0;
            if (__ecdsa_sig_der_to_raw_rs(signature, signature_len, 256, &raw_signature, &raw_signature_len) != 0) {
                TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "ecdsa_sig_der_to_raw_rs failed");
            }
            free(signature);
            signature = raw_signature;
            signature_len = raw_signature_len;
        }
        status = psa_verify_message(key_id, alg,
                                    (const uint8_t *) input, input_len,
                                    signature, signature_len);
        TEST_ASSERT_EQUAL_MESSAGE(PSA_SUCCESS, status, "psa_verify_message failed");

        /* Clean up PSA key */
        if (key_id != 0) {
            psa_destroy_key(key_id);
        }
        psa_reset_key_attributes(&attributes);

        free(priv_pem);
        free(signature);
    }
}

void test_rmaker_sign_verify_basic(void)
{
    for (int i = 0; i < TEST_DATA_COUNT; i++) {
        __sign_verify_basic(test_data_input[i]);
    }
}

void test_rmaker_sign_verify_errors(void)
{
    esp_rmaker_error_t err;
    const unsigned char *test_data = (unsigned char *)test_data_input[0];
    size_t test_data_len = strlen(test_data_input[0]);
    uint8_t *signature = NULL;
    size_t signature_len = 0;

    err = esp_rmaker_crypto_sign_data(NULL, 0, test_data, test_data_len, &signature, &signature_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "sign_data should fail with NULL private key");

    err = esp_rmaker_crypto_sign_data((const uint8_t *) "test", 0, test_data, test_data_len, &signature, &signature_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "sign_data should fail with zero private key length");

    err = esp_rmaker_crypto_sign_data((const uint8_t *) "test", sizeof("test"), NULL, test_data_len, &signature, &signature_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "sign_data should fail with NULL data");

    err = esp_rmaker_crypto_sign_data((const uint8_t *) "test", sizeof("test"), test_data, test_data_len, NULL, &signature_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "sign_data should fail with NULL signature buffer");

    err = esp_rmaker_crypto_sign_data((const uint8_t *) "test", sizeof("test"), test_data, test_data_len, &signature, NULL);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "sign_data should fail with NULL signature length");

    err = esp_rmaker_crypto_sign_data((const uint8_t *) "key that should not work", sizeof("key that should not work"), test_data, test_data_len, &signature, &signature_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_FAIL, err, "sign_data should fail with invalid private key");

    if (signature) {
        free(signature);
    }
}

void test_esp_key_bin_to_der_basic(void)
{
    uint8_t key[32] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
    };
    uint8_t *der = NULL;
    size_t der_len = 0;
    esp_rmaker_error_t err = esp_rmaker_crypto_esp_key_bin_to_der(key, sizeof(key), &der, &der_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "esp_key_bin_to_der failed");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, der_len, "empty DER");

    /* Parse the DER key */
    bool is_supported = esp_rmaker_crypto_is_key_supported_tls(der, der_len);
    TEST_ASSERT_EQUAL_MESSAGE(true, is_supported, "is_key_supported_tls failed");
    free(der);
}

/* Key and CSR generation (used by claiming) *************************************/

static const esp_rmaker_crypto_key_type_t test_claim_key_types[2] = {
    RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256,
    RMAKER_CRYPTO_KEY_TYPE_RSA_2048,
};

static void __gen_key_pem_roundtrip(esp_rmaker_crypto_key_type_t want)
{
    uint8_t *key_pem = NULL;
    size_t key_pem_len = 0;
    esp_rmaker_error_t err = esp_rmaker_crypto_gen_key_pem(want, &key_pem, &key_pem_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "gen_key_pem failed");
    TEST_ASSERT_NOT_NULL_MESSAGE(key_pem, "gen_key_pem returned NULL");

    /* The reported length includes the NUL terminator. */
    TEST_ASSERT_EQUAL_MESSAGE(strlen((const char *)key_pem) + 1, key_pem_len,
                              "key_pem_len should include the NUL terminator");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr((const char *)key_pem, "-----BEGIN"), "key is not PEM");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr((const char *)key_pem, "-----END"), "key is not PEM");

    /* The type must round-trip. */
    esp_rmaker_crypto_key_type_t got = RMAKER_CRYPTO_KEY_TYPE_UNKNOWN;
    err = esp_rmaker_crypto_get_key_type(key_pem, key_pem_len, &got);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "get_key_type failed");
    TEST_ASSERT_EQUAL_MESSAGE(want, got, "get_key_type returned the wrong type");

    /* The key must be usable for TLS and for signing. */
    TEST_ASSERT_TRUE_MESSAGE(esp_rmaker_crypto_is_key_supported_tls(key_pem, key_pem_len),
                             "generated key rejected by is_key_supported_tls");

    uint8_t *signature = NULL;
    size_t signature_len = 0;
    err = esp_rmaker_crypto_sign_data(key_pem, key_pem_len, (const uint8_t *)"payload", 7,
                                      &signature, &signature_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "generated key could not sign");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, signature_len, "empty signature from generated key");
    free(signature);

    free(key_pem);
}

void test_rmaker_gen_key_pem_basic(void)
{
    for (int i = 0; i < sizeof(test_claim_key_types) / sizeof(test_claim_key_types[0]); i++) {
        __gen_key_pem_roundtrip(test_claim_key_types[i]);
    }
}

void test_rmaker_gen_key_pem_errors(void)
{
    uint8_t *key_pem = NULL;
    size_t key_pem_len = 0;
    esp_rmaker_error_t err;

    err = esp_rmaker_crypto_gen_key_pem(RMAKER_CRYPTO_KEY_TYPE_UNKNOWN, &key_pem, &key_pem_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "gen_key_pem should reject an unknown key type");

    err = esp_rmaker_crypto_gen_key_pem(RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256, NULL, &key_pem_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "gen_key_pem should reject a NULL output pointer");

    err = esp_rmaker_crypto_gen_key_pem(RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256, &key_pem, NULL);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "gen_key_pem should reject a NULL length pointer");
}

static void __gen_csr_pem_for_key(esp_rmaker_crypto_key_type_t want, const char *common_name)
{
    uint8_t *key_pem = NULL;
    size_t key_pem_len = 0;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK,
                              esp_rmaker_crypto_gen_key_pem(want, &key_pem, &key_pem_len),
                              "gen_key_pem failed");

    uint8_t *csr_pem = NULL;
    size_t csr_pem_len = 0;
    esp_rmaker_error_t err = esp_rmaker_crypto_gen_csr_pem(key_pem, key_pem_len, common_name,
                             &csr_pem, &csr_pem_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "gen_csr_pem failed");
    TEST_ASSERT_NOT_NULL_MESSAGE(csr_pem, "gen_csr_pem returned NULL");
    TEST_ASSERT_EQUAL_MESSAGE(strlen((const char *)csr_pem) + 1, csr_pem_len,
                              "csr_pem_len should include the NUL terminator");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr((const char *)csr_pem, "-----BEGIN CERTIFICATE REQUEST-----"),
                                 "CSR is not PEM");

    /* The per-key-type bound callers size their buffers from must hold, and hold for the
     * escaped form too -- claiming stores the CSR with its newlines escaped as \n, one extra
     * byte per line. Checked here rather than left to a hardware run, since RSA is the tight
     * case and this is where the numbers are known. */
    size_t lines = 0;
    for (const char *p = (const char *)csr_pem; *p != '\0'; p++) {
        if (*p == '\n') {
            lines++;
        }
    }
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(esp_rmaker_crypto_csr_pem_max_len(want), csr_pem_len + lines,
                                      "escaped CSR exceeds the advertised bound for its key type");

    /* The CSR must parse, and carry exactly the requested Common Name. Parsing also
     * verifies the CSR's self-signature. */
    mbedtls_x509_csr csr;
    mbedtls_x509_csr_init(&csr);
    int ret = mbedtls_x509_csr_parse(&csr, csr_pem, csr_pem_len);
    TEST_ASSERT_EQUAL_MESSAGE(0, ret, "generated CSR does not parse");

    char subject[128] = { 0 };
    ret = mbedtls_x509_dn_gets(subject, sizeof(subject), &csr.subject);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, ret, "could not read the CSR subject");

    char expected[160];
    snprintf(expected, sizeof(expected), "CN=%s", common_name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, subject, "CSR subject mismatch");

    mbedtls_x509_csr_free(&csr);
    free(csr_pem);
    free(key_pem);
}

void test_rmaker_gen_csr_pem_basic(void)
{
    /* A 12-hex-char MAC-style node ID and a longer cloud-assigned one. */
    __gen_csr_pem_for_key(RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256, "3C71BF1A2B3C");
    __gen_csr_pem_for_key(RMAKER_CRYPTO_KEY_TYPE_RSA_2048, "node-abc123XYZ");
}

void test_rmaker_csr_pem_max_len(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(RMAKER_CRYPTO_CSR_PEM_LEN_ECDSA_P256,
                              esp_rmaker_crypto_csr_pem_max_len(RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256),
                              "wrong bound for ECDSA P-256");
    TEST_ASSERT_EQUAL_MESSAGE(RMAKER_CRYPTO_CSR_PEM_LEN_RSA_2048,
                              esp_rmaker_crypto_csr_pem_max_len(RMAKER_CRYPTO_KEY_TYPE_RSA_2048),
                              "wrong bound for RSA-2048");
    /* An unclassified key can still sign a CSR, so the bound falls back rather than to zero. */
    TEST_ASSERT_EQUAL_MESSAGE(RMAKER_CRYPTO_CSR_PEM_LEN_MAX,
                              esp_rmaker_crypto_csr_pem_max_len(RMAKER_CRYPTO_KEY_TYPE_UNKNOWN),
                              "UNKNOWN should fall back to the largest bound");
    TEST_ASSERT_EQUAL_MESSAGE(RMAKER_CRYPTO_CSR_PEM_LEN_MAX,
                              esp_rmaker_crypto_csr_pem_max_len((esp_rmaker_crypto_key_type_t)999),
                              "an out-of-range type should fall back to the largest bound");
    /* The fallback has to be the largest, or a future key type would silently under-size. */
    TEST_ASSERT_GREATER_OR_EQUAL(RMAKER_CRYPTO_CSR_PEM_LEN_RSA_2048, RMAKER_CRYPTO_CSR_PEM_LEN_MAX);
    TEST_ASSERT_GREATER_OR_EQUAL(RMAKER_CRYPTO_CSR_PEM_LEN_ECDSA_P256, RMAKER_CRYPTO_CSR_PEM_LEN_RSA_2048);
}

void test_rmaker_gen_csr_pem_errors(void)
{
    uint8_t *csr_pem = NULL;
    size_t csr_pem_len = 0;
    esp_rmaker_error_t err;

    /* A PEM-shaped but structurally invalid key must be rejected, not crash. */
    const char *garbage = "-----BEGIN EC PRIVATE KEY-----\nbm90YWtleQ==\n-----END EC PRIVATE KEY-----\n";
    err = esp_rmaker_crypto_gen_csr_pem((const uint8_t *)garbage, strlen(garbage) + 1, "node",
                                        &csr_pem, &csr_pem_len);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "gen_csr_pem should reject a malformed key");
    TEST_ASSERT_NULL_MESSAGE(csr_pem, "gen_csr_pem should not allocate on failure");

    /* A truncated PEM must be rejected by the type probe too. */
    esp_rmaker_crypto_key_type_t key_type = RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256;
    err = esp_rmaker_crypto_get_key_type((const uint8_t *)garbage, strlen(garbage) + 1, &key_type);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "get_key_type should reject a malformed key");
    TEST_ASSERT_EQUAL_MESSAGE(RMAKER_CRYPTO_KEY_TYPE_UNKNOWN, key_type,
                              "get_key_type should report UNKNOWN on failure");

    /* NULL argument handling. */
    err = esp_rmaker_crypto_gen_csr_pem(NULL, 0, "node", &csr_pem, &csr_pem_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "gen_csr_pem should reject a NULL key");

    /* A Common Name too long for the subject buffer must be rejected rather than silently
     * truncated, since the CN is the node's identity. */
    uint8_t *key_pem = NULL;
    size_t key_pem_len = 0;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK,
                              esp_rmaker_crypto_gen_key_pem(RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256, &key_pem, &key_pem_len),
                              "gen_key_pem failed");
    char long_cn[200];
    memset(long_cn, 'x', sizeof(long_cn) - 1);
    long_cn[sizeof(long_cn) - 1] = '\0';
    err = esp_rmaker_crypto_gen_csr_pem(key_pem, key_pem_len, long_cn, &csr_pem, &csr_pem_len);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, err, "gen_csr_pem should reject an over-long CN");
    free(key_pem);
}
