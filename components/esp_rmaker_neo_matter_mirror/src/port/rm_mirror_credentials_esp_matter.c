/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_credentials_esp_matter.c
 * @brief RainMaker credential providers backed by the Matter DAC held in the chip-factory NVS
 *        namespace, so one factory image serves both stacks.
 */

#include <stdlib.h>
#include <string.h>

#include <esp_log.h>
#include <nvs_flash.h>

#include <mbedtls/oid.h>
#include <mbedtls/x509_crt.h>

#include <sdkconfig.h>

#include "esp_rmaker_matter_mirror.h"
#include "util/esp_rmaker_crypto.h"

#define RM_MIRROR_FACTORY_PARTITION CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL
#define RM_MIRROR_FACTORY_NAMESPACE "chip-factory"
#define RM_MIRROR_NVS_KEY_DAC_KEY   "dac-key"
#define RM_MIRROR_NVS_KEY_DAC_CERT  "dac-cert"

/* CHIP stores the DAC private key as a raw big-endian P-256 scalar */
#define RM_P256_PRIV_LEN 32

static const char *TAG = "rm_mirror_credentials";

/** DAC subject common name; resolved once, then handed out as a copy per call. */
static char *__cached_client_id;

static esp_rmaker_error_t __to_rmaker_err(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return ESP_RMAKER_OK;
    case ESP_ERR_NVS_NOT_FOUND:
    case ESP_ERR_NVS_PART_NOT_FOUND:
        return ESP_RMAKER_NOT_FOUND;
    case ESP_ERR_INVALID_ARG:
        return ESP_RMAKER_INVALID_ARG;
    case ESP_ERR_INVALID_STATE:
        return ESP_RMAKER_INVALID_STATE;
    case ESP_ERR_NO_MEM:
        return ESP_RMAKER_NO_MEM;
    default:
        return ESP_RMAKER_FAIL;
    }
}

/* No-op once the partition is up, so it is safe to call from every provider. */
static esp_rmaker_error_t __init_factory_partition(void)
{
    esp_err_t err = nvs_flash_init_partition(RM_MIRROR_FACTORY_PARTITION);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialise the '%s' partition: %s", RM_MIRROR_FACTORY_PARTITION,
                 esp_err_to_name(err));
    }
    return __to_rmaker_err(err);
}

static esp_rmaker_error_t __read_blob(const char *key, bool required, esp_rmaker_credential_t *p_credential)
{
    if (!key || !p_credential) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t rerr = __init_factory_partition();
    if (rerr != ESP_RMAKER_OK) {
        return rerr;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(RM_MIRROR_FACTORY_PARTITION, RM_MIRROR_FACTORY_NAMESPACE,
                                            NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open '%s' in '%s': %s", RM_MIRROR_FACTORY_NAMESPACE,
                 RM_MIRROR_FACTORY_PARTITION, esp_err_to_name(err));
        return __to_rmaker_err(err);
    }

    size_t len = 0;
    err = nvs_get_blob(handle, key, NULL, &len);
    if (err != ESP_OK || len == 0) {
        if (required) {
            ESP_LOGE(TAG, "No '%s' entry in the factory data: %s", key, esp_err_to_name(err));
        }
        nvs_close(handle);
        return (err == ESP_OK) ? ESP_RMAKER_NOT_FOUND : __to_rmaker_err(err);
    }

    uint8_t *buf = malloc(len);
    if (!buf) {
        nvs_close(handle);
        return ESP_RMAKER_NO_MEM;
    }

    err = nvs_get_blob(handle, key, buf, &len);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read '%s': %s", key, esp_err_to_name(err));
        free(buf);
        return __to_rmaker_err(err);
    }

    p_credential->credential = buf;
    p_credential->len = len;
    return ESP_RMAKER_OK;
}

/* The DAC is the leaf: the chain may carry the PAI first, whose CN is a different name. */
static char *__dup_leaf_common_name(const mbedtls_x509_crt *chain)
{
    const mbedtls_x509_crt *leaf = chain;
    while (leaf->next) {
        leaf = leaf->next;
    }

    static const unsigned char oid_cn[] = MBEDTLS_OID_AT_CN;
    const size_t oid_cn_len = sizeof(oid_cn) - 1; /* drop the string terminator */

    for (const mbedtls_x509_name *name = &leaf->subject; name; name = name->next) {
        if (name->oid.p && name->oid.len == oid_cn_len && memcmp(name->oid.p, oid_cn, oid_cn_len) == 0) {
            char *cn = malloc(name->val.len + 1);
            if (!cn) {
                return NULL;
            }
            memcpy(cn, name->val.p, name->val.len);
            cn[name->val.len] = '\0';
            return cn;
        }
    }
    return NULL;
}

static esp_rmaker_error_t __client_key_provider(esp_rmaker_credential_t *p_credential)
{
    esp_rmaker_credential_t raw = {};
    esp_rmaker_error_t err = __read_blob(RM_MIRROR_NVS_KEY_DAC_KEY, true, &raw);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Tell the raw scalar apart by length, never by its first byte: about 1 scalar in 256 starts with 0x30, the DER tag. */
    if (raw.len != RM_P256_PRIV_LEN) {
        *p_credential = raw;
        return ESP_RMAKER_OK;
    }

    /* CHIP keeps the DAC keypair raw and loads it with HazardousOperationLoadKeypairFromRaw, but
     * mbedTLS and esp-tls both want PEM or DER, so the scalar has to be converted first. */
    err = esp_rmaker_crypto_esp_key_bin_to_der(raw.credential, raw.len, &p_credential->credential,
            &p_credential->len);
    esp_rmaker_credentials_free_credential(&raw);
    if (err != ESP_RMAKER_OK) {
        ESP_LOGE(TAG, "Failed to convert the DAC key to DER: %d", err);
    }
    return err;
}

static esp_rmaker_error_t __client_cert_provider(esp_rmaker_credential_t *p_credential)
{
    return __read_blob(RM_MIRROR_NVS_KEY_DAC_CERT, true, p_credential);
}

static esp_rmaker_error_t __client_id_provider(char **p_client_id)
{
    if (!p_client_id) {
        return ESP_RMAKER_INVALID_ARG;
    }

    if (!__cached_client_id) {
        esp_rmaker_credential_t cert = {};
        esp_rmaker_error_t rerr = __client_cert_provider(&cert);
        if (rerr != ESP_RMAKER_OK) {
            return rerr;
        }

        mbedtls_x509_crt chain;
        mbedtls_x509_crt_init(&chain);
        int ret = mbedtls_x509_crt_parse(&chain, cert.credential, cert.len);
        esp_rmaker_credentials_free_credential(&cert);
        if (ret != 0) {
            ESP_LOGE(TAG, "DAC parse failed: -0x%04X", (unsigned) - ret);
            mbedtls_x509_crt_free(&chain);
            return ESP_RMAKER_INVALID_STATE;
        }

        __cached_client_id = __dup_leaf_common_name(&chain);
        mbedtls_x509_crt_free(&chain);
        if (!__cached_client_id) {
            ESP_LOGE(TAG, "No common name in the DAC subject.");
            return ESP_RMAKER_NOT_FOUND;
        }
        ESP_LOGI(TAG, "Node id from the DAC common name: %s", __cached_client_id);
    }

    char *copy = strdup(__cached_client_id);
    if (!copy) {
        return ESP_RMAKER_NO_MEM;
    }
    *p_client_id = copy;
    return ESP_RMAKER_OK;
}

/* mqtt_host and random stay on the defaults: they live in the RainMaker namespace of the same
 * factory partition. */
static const esp_rmaker_credentials_providers_t __dac_credentials_providers = {
    .client_cert = __client_cert_provider,
    .client_key = __client_key_provider,
    .client_id = __client_id_provider,
};

const esp_rmaker_credentials_providers_t *esp_rmaker_matter_mirror_get_dac_credentials(void)
{
    return &__dac_credentials_providers;
}
