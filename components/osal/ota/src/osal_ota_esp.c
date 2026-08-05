/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ota_esp.c
 * @brief OTA common implementation for ESP-IDF platform.
 */

#include <string.h>
#include "osal_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "bootloader_common.h"
#include "esp_idf_version.h"
#include "psa/crypto.h"

static const char *TAG = "osal_ota";

/**
 * @brief Convert ESP error code to OTA common error code
 */
static osal_err_t esp_err_to_os_err(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return OSAL_ERR_OK;
    case ESP_FAIL:
        return OSAL_ERR_FAIL;
    case ESP_ERR_INVALID_ARG:
        return OSAL_ERR_INVALID_ARG;
    case ESP_ERR_INVALID_SIZE:
        return OSAL_ERR_OTA_INVALID_SIZE;
    case ESP_ERR_NOT_FOUND:
        return OSAL_ERR_NOT_FOUND;
    case ESP_ERR_NOT_SUPPORTED:
        return OSAL_ERR_NOT_SUPPORTED;
    case ESP_ERR_TIMEOUT:
        return OSAL_ERR_TIMEOUT;
    case ESP_ERR_NO_MEM:
        return OSAL_ERR_NO_MEM;
    case ESP_ERR_OTA_PARTITION_CONFLICT:
        return OSAL_ERR_OTA_PARTITION_CONFLICT;
    case ESP_ERR_OTA_SELECT_INFO_INVALID:
        return OSAL_ERR_OTA_SELECT_INFO_INVALID;
    case ESP_ERR_OTA_VALIDATE_FAILED:
        return OSAL_ERR_OTA_VALIDATE_FAILED;
    case ESP_ERR_OTA_SMALL_SEC_VER:
        return OSAL_ERR_OTA_SMALL_SEC_VER;
    case ESP_ERR_OTA_ROLLBACK_FAILED:
        return OSAL_ERR_OTA_ROLLBACK_FAILED;
    case ESP_ERR_OTA_ROLLBACK_INVALID_STATE:
        return OSAL_ERR_OTA_ROLLBACK_INVALID_STATE;
    default:
        return OSAL_ERR_FAIL;
    }
}

/**
 * @brief Convert ESP OTA image state to OTA common image state
 */
static osal_ota_img_states_t esp_ota_state_to_ota_common(esp_ota_img_states_t state)
{
    switch (state) {
    case ESP_OTA_IMG_VALID:
        return OSAL_OTA_IMG_VALID;
    case ESP_OTA_IMG_UNDEFINED:
        return OSAL_OTA_IMG_UNDEFINED;
    case ESP_OTA_IMG_INVALID:
        return OSAL_OTA_IMG_INVALID;
    case ESP_OTA_IMG_ABORTED:
        return OSAL_OTA_IMG_ABORTED;
    case ESP_OTA_IMG_NEW:
        return OSAL_OTA_IMG_NEW;
    case ESP_OTA_IMG_PENDING_VERIFY:
        return OSAL_OTA_IMG_PENDING_VERIFY;
    default:
        return OSAL_OTA_IMG_UNDEFINED;
    }
}

/**
 * @brief Convert ESP partition to OTA common partition
 */
static void esp_partition_to_ota_common(const esp_partition_t *esp_part, osal_ota_partition_t *ota_part)
{
    if (!esp_part || !ota_part) {
        return;
    }

    ota_part->label = esp_part->label;
    ota_part->address = esp_part->address;
    ota_part->size = esp_part->size;
    ota_part->type = esp_part->type;
    ota_part->subtype = esp_part->subtype;
    ota_part->encrypted = esp_part->encrypted;
}

/**
 * @brief Convert ESP app desc to OTA common app desc
 */
static void esp_app_desc_to_ota_common(const esp_app_desc_t *esp_desc, osal_ota_app_desc_t *ota_desc)
{
    if (!esp_desc || !ota_desc) {
        return;
    }

    ota_desc->magic_word = esp_desc->magic_word;
    ota_desc->secure_version = esp_desc->secure_version;
    memcpy(ota_desc->version, esp_desc->version, sizeof(esp_desc->version));
    memcpy(ota_desc->project_name, esp_desc->project_name, sizeof(esp_desc->project_name));
    memcpy(ota_desc->app_elf_sha256, esp_desc->app_elf_sha256, sizeof(esp_desc->app_elf_sha256));
}

osal_err_t osal_ota_begin(const osal_ota_partition_t *partition,
                          size_t image_size,
                          osal_ota_handle_t *out_handle)
{
    if (!partition || !out_handle) {
        return OSAL_ERR_INVALID_ARG;
    }

    // For ESP, we need to find the actual ESP partition by label
    const esp_partition_t *esp_partition = esp_partition_find_first(
            partition->type, partition->subtype, partition->label);

    if (!esp_partition) {
        ESP_LOGE(TAG, "Partition not found: %s", partition->label);
        return OSAL_ERR_NOT_FOUND;
    }

    esp_err_t err = esp_ota_begin(esp_partition, image_size, (esp_ota_handle_t *)out_handle);
    return esp_err_to_os_err(err);
}

osal_err_t osal_ota_resume(const osal_ota_partition_t *partition,
                           const size_t erase_size,
                           const size_t image_offset,
                           osal_ota_handle_t *out_handle)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    if (!partition || !out_handle) {
        return OSAL_ERR_INVALID_ARG;
    }

    // For ESP, we need to find the actual ESP partition by label
    const esp_partition_t *esp_partition = esp_partition_find_first(
            partition->type, partition->subtype, partition->label);

    if (!esp_partition) {
        ESP_LOGE(TAG, "Partition not found: %s", partition->label);
        return OSAL_ERR_NOT_FOUND;
    }

    esp_err_t err = esp_ota_resume(esp_partition, erase_size, image_offset, (esp_ota_handle_t *)out_handle);
    return esp_err_to_os_err(err);
#else
    ESP_LOGE(TAG, "OTA resume is not supported in this version of ESP-IDF");
    return OSAL_ERR_NOT_SUPPORTED;
#endif /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0) */
}

osal_err_t osal_ota_write_with_offset(osal_ota_handle_t handle,
                                      const void *data,
                                      size_t size,
                                      uint32_t offset)
{
    if (!data) {
        return OSAL_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_ota_write_with_offset((esp_ota_handle_t)handle, data, size, offset);
    return esp_err_to_os_err(err);
}

osal_err_t osal_ota_end(osal_ota_handle_t handle)
{
    esp_err_t err = esp_ota_end((esp_ota_handle_t)handle);
    return esp_err_to_os_err(err);
}

osal_err_t osal_ota_abort(osal_ota_handle_t handle)
{
    esp_err_t err = esp_ota_abort((esp_ota_handle_t)handle);
    return esp_err_to_os_err(err);
}

osal_err_t osal_ota_set_boot_partition(const osal_ota_partition_t *partition)
{
    if (!partition) {
        return OSAL_ERR_INVALID_ARG;
    }

    // Find the actual ESP partition
    const esp_partition_t *esp_partition = esp_partition_find_first(
            partition->type, partition->subtype, partition->label);

    if (!esp_partition) {
        ESP_LOGE(TAG, "Partition not found: %s", partition->label);
        return OSAL_ERR_NOT_FOUND;
    }

    esp_err_t err = esp_ota_set_boot_partition(esp_partition);
    return esp_err_to_os_err(err);
}

const osal_ota_partition_t *osal_ota_get_boot_partition(void)
{
    const esp_partition_t *esp_partition = esp_ota_get_boot_partition();
    if (!esp_partition) {
        return NULL;
    }

    // We need to return a static instance since we can't allocate memory here
    static osal_ota_partition_t ota_partition;
    esp_partition_to_ota_common(esp_partition, &ota_partition);
    return &ota_partition;
}

const osal_ota_partition_t *osal_ota_get_running_partition(void)
{
    const esp_partition_t *esp_partition = esp_ota_get_running_partition();
    if (!esp_partition) {
        return NULL;
    }

    // We need to return a static instance since we can't allocate memory here
    static osal_ota_partition_t ota_partition;
    esp_partition_to_ota_common(esp_partition, &ota_partition);
    return &ota_partition;
}

const osal_ota_partition_t *osal_ota_get_next_update_partition(const osal_ota_partition_t *start_from)
{
    const esp_partition_t *esp_start_from = NULL;

    if (start_from) {
        // Find the actual ESP partition
        esp_start_from = esp_partition_find_first(
                             start_from->type, start_from->subtype, start_from->label);
    }

    const esp_partition_t *esp_partition = esp_ota_get_next_update_partition(esp_start_from);
    if (!esp_partition) {
        return NULL;
    }

    // We need to return a static instance since we can't allocate memory here
    static osal_ota_partition_t ota_partition;
    esp_partition_to_ota_common(esp_partition, &ota_partition);
    return &ota_partition;
}

osal_err_t osal_ota_get_partition_description(const osal_ota_partition_t *partition,
        osal_ota_app_desc_t *app_desc)
{
    if (!partition || !app_desc) {
        return OSAL_ERR_INVALID_ARG;
    }

    // Find the actual ESP partition
    const esp_partition_t *esp_partition = esp_partition_find_first(
            partition->type, partition->subtype, partition->label);

    if (!esp_partition) {
        return OSAL_ERR_NOT_FOUND;
    }

    esp_app_desc_t esp_desc;
    esp_err_t err = esp_ota_get_partition_description(esp_partition, &esp_desc);
    if (err == ESP_OK) {
        esp_app_desc_to_ota_common(&esp_desc, app_desc);
    }

    return esp_err_to_os_err(err);
}

/**
 * @brief Compute the MD5 of the first size_to_hash bytes of a partition.
 *
 * SHA256 has a dedicated bootloader helper; MD5 does not, so read the partition
 * in chunks and feed a PSA hash operation. Used for the optional end-to-end
 * file_md5 integrity check on OTA images.
 */
static osal_err_t osal_ota_get_partition_md5(const esp_partition_t *esp_partition,
        size_t size_to_hash,
        uint8_t *hash)
{
    psa_status_t st = psa_crypto_init(); /* safe to call multiple times */
    if (st != PSA_SUCCESS) {
        return OSAL_ERR_FAIL;
    }

    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    st = psa_hash_setup(&op, PSA_ALG_MD5);
    if (st != PSA_SUCCESS) {
        return OSAL_ERR_NOT_SUPPORTED;
    }

    uint8_t buffer[1024];
    size_t offset = 0;
    while (size_to_hash > 0) {
        size_t chunk = size_to_hash > sizeof(buffer) ? sizeof(buffer) : size_to_hash;
        esp_err_t err = esp_partition_read(esp_partition, offset, buffer, chunk);
        if (err != ESP_OK) {
            psa_hash_abort(&op);
            return esp_err_to_os_err(err);
        }
        st = psa_hash_update(&op, buffer, chunk);
        if (st != PSA_SUCCESS) {
            psa_hash_abort(&op);
            return OSAL_ERR_FAIL;
        }
        offset += chunk;
        size_to_hash -= chunk;
    }

    size_t out_len = 0;
    st = psa_hash_finish(&op, hash, OSAL_OTA_HASH_LEN_MD5, &out_len);
    if (st != PSA_SUCCESS || out_len != OSAL_OTA_HASH_LEN_MD5) {
        psa_hash_abort(&op);
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}

osal_err_t osal_ota_get_partition_hash(const osal_ota_partition_t *partition,
                                       size_t size_to_hash,
                                       osal_ota_hash_type_t hash_type,
                                       uint8_t *hash,
                                       size_t hash_len)
{
    // Check if the hash type is supported and the hash buffer is big enough
    size_t required_len;
    switch (hash_type) {
    case OSAL_OTA_HASH_SHA256:
        required_len = OSAL_OTA_HASH_LEN_SHA256;
        break;
    case OSAL_OTA_HASH_MD5:
        required_len = OSAL_OTA_HASH_LEN_MD5;
        break;
    default:
        return OSAL_ERR_NOT_SUPPORTED;
    }

    // Check if the partition and hash buffer are valid and hash size is valid
    if (!partition || !hash || hash_len < required_len || size_to_hash > partition->size) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (size_to_hash == 0) {
        size_to_hash = partition->size;
    }

    // Find the actual ESP partition
    const esp_partition_t *esp_partition = esp_partition_find_first(
            partition->type, partition->subtype, partition->label);

    if (!esp_partition) {
        return OSAL_ERR_NOT_FOUND;
    }

    if (hash_type == OSAL_OTA_HASH_MD5) {
        return osal_ota_get_partition_md5(esp_partition, size_to_hash, hash);
    }

    // Get the SHA256 of the partition (treat as data partition to hash all contents)
    esp_err_t err = bootloader_common_get_sha256_of_partition(
                        partition->address,
                        size_to_hash,
                        PART_TYPE_DATA,
                        hash
                    );
    return esp_err_to_os_err(err);
}

osal_err_t osal_ota_mark_app_valid_cancel_rollback(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    return esp_err_to_os_err(err);
}

osal_err_t osal_ota_mark_app_invalid_rollback_and_reboot(void)
{
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    return esp_err_to_os_err(err);
}

osal_err_t osal_ota_get_state_partition(const osal_ota_partition_t *partition,
                                        osal_ota_img_states_t *ota_state)
{
    if (!partition || !ota_state) {
        return OSAL_ERR_INVALID_ARG;
    }

    // Find the actual ESP partition
    const esp_partition_t *esp_partition = esp_partition_find_first(
            partition->type, partition->subtype, partition->label);

    if (!esp_partition) {
        return OSAL_ERR_NOT_FOUND;
    }

    esp_ota_img_states_t esp_state;
    esp_err_t err = esp_ota_get_state_partition(esp_partition, &esp_state);
    if (err == ESP_OK) {
        *ota_state = esp_ota_state_to_ota_common(esp_state);
    }

    return esp_err_to_os_err(err);
}

const osal_ota_partition_t *osal_ota_get_last_invalid_partition(void)
{
    const esp_partition_t *esp_partition = esp_ota_get_last_invalid_partition();
    if (!esp_partition) {
        return NULL;
    }

    // We need to return a static instance since we can't allocate memory here
    static osal_ota_partition_t ota_partition;
    esp_partition_to_ota_common(esp_partition, &ota_partition);
    return &ota_partition;
}

uint8_t osal_ota_get_app_partition_count(void)
{
    return esp_ota_get_app_partition_count();
}

osal_err_t osal_ota_erase_last_boot_app_partition(void)
{
    esp_err_t err = esp_ota_erase_last_boot_app_partition();
    return esp_err_to_os_err(err);
}

bool osal_ota_check_rollback_is_possible(void)
{
    return esp_ota_check_rollback_is_possible();
}
