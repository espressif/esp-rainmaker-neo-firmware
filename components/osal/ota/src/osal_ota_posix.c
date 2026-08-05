/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ota_posix.c
 * @brief OTA common implementation for POSIX platform.
 *
 * This POSIX shim simulates fixed OTA partitions backed by files under
 * `partitions/<label>.bin`, rotating slots round-robin for updates. Reboot and
 * real flash operations are stubbed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdint.h>
#include "psa/crypto.h"
#include "osal_ota_posix_shared.h"
#include "osal_ota_posix_config.h"
#include "osal_app_desc_posix.h"
#include "osal_crc.h"

// Handle structure for POSIX implementation
typedef struct {
    FILE *file;
    char filename[512];
    size_t total_size;
    size_t written_size;
    int partition_index;
    bool valid;
} ota_posix_handle_t;

static const char *k_ota_base_dir = OSAL_OTA_POSIX_BASE_DIR;
static pthread_mutex_t g_ota_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_ota_inited = false;
static bool g_partitions_inited = false;
static uint8_t g_current_partition_idx = 0;

static char g_partition_labels[OSAL_OTA_POSIX_PART_COUNT][OSAL_OTA_POSIX_MAX_LABEL_LEN];
static osal_ota_partition_t g_partitions[OSAL_OTA_POSIX_PART_COUNT];


static osal_err_t ensure_ota_base_dir(void)
{
    struct stat st;
    if (stat(k_ota_base_dir, &st) == -1) {
        if (mkdir(k_ota_base_dir, 0700) != 0) {
            return OSAL_ERR_FAIL;
        }
    }
    return OSAL_ERR_OK;
}

static int find_partition_index_by_label(const char *label)
{
    if (!label) {
        return -1;
    }
    for (int i = 0; i < OSAL_OTA_POSIX_PART_COUNT; ++i) {
        if (strcmp(label, g_partition_labels[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static osal_err_t ensure_partitions_initialized(void)
{
    if (g_partitions_inited) {
        return OSAL_ERR_OK;
    }

    for (int i = 0; i < OSAL_OTA_POSIX_PART_COUNT; ++i) {
        int written = snprintf(g_partition_labels[i], OSAL_OTA_POSIX_MAX_LABEL_LEN, "%s%d", OSAL_OTA_POSIX_PART_PREFIX, i);
        if (written < 0 || written >= OSAL_OTA_POSIX_MAX_LABEL_LEN) {
            return OSAL_ERR_FAIL;
        }
        g_partitions[i].label = g_partition_labels[i];
        g_partitions[i].address = (uint32_t)(i * OSAL_OTA_POSIX_DEFAULT_PART_SIZE);
        g_partitions[i].size = OSAL_OTA_POSIX_DEFAULT_PART_SIZE;
        g_partitions[i].type = 0;
        g_partitions[i].subtype = 0;
        g_partitions[i].encrypted = false;
    }

    /* Set current partition index to current boot partition */
    uint8_t boot_idx;
    osal_err_t rc = osal_ota_posix_config_get_boot_partition(&boot_idx);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }
    g_current_partition_idx = boot_idx;

    /* Set partitions initialized */
    g_partitions_inited = true;
    return OSAL_ERR_OK;
}

osal_err_t osal_ota_build_partition_path(uint8_t partition_idx, char *out_filename, size_t max_len)
{
    if (!out_filename) {
        return OSAL_ERR_INVALID_ARG;
    }
    osal_err_t rc = ensure_partitions_initialized();
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    int written = snprintf(out_filename, max_len, "%s/%s", k_ota_base_dir, g_partition_labels[partition_idx]);
    if (written < 0 || (size_t)written >= max_len) {
        return OSAL_ERR_FAIL;
    }

    return OSAL_ERR_OK;
}

static const osal_ota_partition_t *get_partition_by_index(uint8_t idx)
{
    if (idx >= OSAL_OTA_POSIX_PART_COUNT) {
        return NULL;
    }
    return &g_partitions[idx];
}

static osal_err_t compute_file_hash(const char *filename, size_t size_to_hash, psa_algorithm_t alg, uint8_t *hash_out, size_t expected_len)
{
    if (!filename || !hash_out) {
        return OSAL_ERR_INVALID_ARG;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        return OSAL_ERR_NOT_FOUND;
    }

    psa_status_t st;
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;

    st = psa_crypto_init(); /* safe to call multiple times */
    if (st != PSA_SUCCESS) {
        fclose(file);
        return OSAL_ERR_FAIL;
    }

    st = psa_hash_setup(&op, alg);
    if (st != PSA_SUCCESS) {
        fclose(file);
        return OSAL_ERR_NOT_SUPPORTED;
    }

    uint8_t buffer[4096];
    while (size_to_hash > 0) {
        size_t chunk = size_to_hash > sizeof(buffer) ? sizeof(buffer) : size_to_hash;
        size_t n = fread(buffer, 1, chunk, file);
        if (n == 0) {
            /* EOF or error before hashing requested size */
            psa_hash_abort(&op);
            fclose(file);
            return OSAL_ERR_FAIL;
        }

        st = psa_hash_update(&op, buffer, n);
        if (st != PSA_SUCCESS) {
            psa_hash_abort(&op);
            fclose(file);
            return OSAL_ERR_FAIL;
        }

        size_to_hash -= n;
    }

    fclose(file);

    size_t hash_len = 0;
    st = psa_hash_finish(&op, hash_out, expected_len, &hash_len);
    if (st != PSA_SUCCESS || hash_len != expected_len) {
        psa_hash_abort(&op);
        return OSAL_ERR_FAIL;
    }

    return OSAL_ERR_OK;
}

osal_err_t osal_ota_begin(const osal_ota_partition_t *partition,
                          size_t image_size,
                          osal_ota_handle_t *out_handle)
{
    if (!out_handle) {
        return OSAL_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&g_ota_mutex);

    // Initialize if not already done
    if (!g_ota_inited) {
        osal_err_t rc = ensure_partitions_initialized();
        if (rc != OSAL_ERR_OK) {
            pthread_mutex_unlock(&g_ota_mutex);
            return rc;
        }
        rc = ensure_ota_base_dir();
        if (rc != OSAL_ERR_OK) {
            pthread_mutex_unlock(&g_ota_mutex);
            return rc;
        }
        g_ota_inited = true;
    }

    int part_idx = -1;
    if (partition && partition->label) {
        part_idx = find_partition_index_by_label(partition->label);
    } else {
        part_idx = g_current_partition_idx;
    }
    if (part_idx < 0 || part_idx >= OSAL_OTA_POSIX_PART_COUNT) {
        pthread_mutex_unlock(&g_ota_mutex);
        return OSAL_ERR_INVALID_ARG;
    }

    char filename[512];
    osal_err_t rc = osal_ota_build_partition_path(part_idx, filename, sizeof(filename));
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_ota_mutex);
        return rc;
    }

    // Allocate new handle
    ota_posix_handle_t *handle = (ota_posix_handle_t *)calloc(1, sizeof(ota_posix_handle_t));
    if (!handle) {
        pthread_mutex_unlock(&g_ota_mutex);
        return OSAL_ERR_NO_MEM;
    }

    // Open file for writing (truncate/create)
    FILE *file = fopen(filename, "wb");
    if (!file) {
        free(handle);
        pthread_mutex_unlock(&g_ota_mutex);
        return OSAL_ERR_FAIL;
    }

    // Initialize handle
    handle->file = file;
    strncpy(handle->filename, filename, sizeof(handle->filename) - 1);
    handle->filename[sizeof(handle->filename) - 1] = '\0';
    handle->total_size = (image_size == OSAL_OTA_SIZE_UNKNOWN) ? 0 : image_size;
    handle->written_size = 0;
    handle->partition_index = part_idx;
    handle->valid = true;

    *out_handle = (osal_ota_handle_t)(uintptr_t)handle;

    pthread_mutex_unlock(&g_ota_mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_ota_resume(const osal_ota_partition_t *partition,
                           const size_t erase_size,
                           const size_t image_offset,
                           osal_ota_handle_t *out_handle)
{
    if (!out_handle) {
        return OSAL_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&g_ota_mutex);

    if (!g_ota_inited) {
        osal_err_t rc = ensure_partitions_initialized();
        if (rc != OSAL_ERR_OK) {
            pthread_mutex_unlock(&g_ota_mutex);
            return rc;
        }
        rc = ensure_ota_base_dir();
        if (rc != OSAL_ERR_OK) {
            pthread_mutex_unlock(&g_ota_mutex);
            return rc;
        }
        g_ota_inited = true;
    }

    int part_idx = -1;
    if (partition && partition->label) {
        part_idx = find_partition_index_by_label(partition->label);
    } else {
        part_idx = g_current_partition_idx;
    }
    if (part_idx < 0 || part_idx >= OSAL_OTA_POSIX_PART_COUNT) {
        pthread_mutex_unlock(&g_ota_mutex);
        return OSAL_ERR_INVALID_ARG;
    }

    char filename[512];
    osal_err_t rc = osal_ota_build_partition_path(part_idx, filename, sizeof(filename));
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_ota_mutex);
        return rc;
    }

    FILE *file = fopen(filename, "r+b");
    if (!file) {
        pthread_mutex_unlock(&g_ota_mutex);
        return OSAL_ERR_NOT_FOUND;
    }

    ota_posix_handle_t *handle = (ota_posix_handle_t *)calloc(1, sizeof(ota_posix_handle_t));
    if (!handle) {
        fclose(file);
        pthread_mutex_unlock(&g_ota_mutex);
        return OSAL_ERR_NO_MEM;
    }

    // Move to requested offset if provided
    if (fseek(file, (long)image_offset, SEEK_SET) != 0) {
        fclose(file);
        free(handle);
        pthread_mutex_unlock(&g_ota_mutex);
        return OSAL_ERR_FAIL;
    }

    handle->file = file;
    strncpy(handle->filename, filename, sizeof(handle->filename) - 1);
    handle->filename[sizeof(handle->filename) - 1] = '\0';
    handle->total_size = (erase_size == OSAL_OTA_SIZE_UNKNOWN) ? 0 : erase_size;
    handle->written_size = image_offset;
    handle->partition_index = part_idx;
    handle->valid = true;

    *out_handle = (osal_ota_handle_t)(uintptr_t)handle;

    pthread_mutex_unlock(&g_ota_mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_ota_write_with_offset(osal_ota_handle_t handle,
                                      const void *data,
                                      size_t size,
                                      uint32_t offset)
{
    ota_posix_handle_t *h = (ota_posix_handle_t *)(uintptr_t)handle;

    if (!h || !h->valid || !h->file || !data) {
        return OSAL_ERR_INVALID_ARG;
    }

    // Seek to the specified offset
    if (fseek(h->file, offset, SEEK_SET) != 0) {
        return OSAL_ERR_FAIL;
    }

    size_t written = fwrite(data, 1, size, h->file);
    if (written != size) {
        return OSAL_ERR_FAIL;
    }

    // Update written_size if this write extends beyond current position
    size_t end_pos = offset + size;
    if (end_pos > h->written_size) {
        h->written_size = end_pos;
    }

    // Check if we've exceeded expected size (if known)
    if (h->total_size > 0 && h->written_size > h->total_size) {
        return OSAL_ERR_OTA_INVALID_SIZE;
    }

    return OSAL_ERR_OK;
}

osal_err_t osal_ota_end(osal_ota_handle_t handle)
{
    ota_posix_handle_t *h = (ota_posix_handle_t *)(uintptr_t)handle;

    if (!h || !h->valid) {
        return OSAL_ERR_INVALID_ARG;
    }

    if (h->file) {
        fflush(h->file);
        fclose(h->file);
        h->file = NULL;
    }

    // Make the file executable
    if (h->filename[0] != '\0') {
        chmod(h->filename, 0755);
    }

    // Set partition state to PENDING_VERIFY
    osal_err_t rc = osal_ota_posix_config_set_partition_state((uint8_t)h->partition_index, OSAL_OTA_IMG_PENDING_VERIFY);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    // For POSIX, we keep the file for verification
    // In a real implementation, you might verify the file here
    h->valid = false;

    // Keep filename so it can be accessed for verification
    // Free the handle
    free(h);

    return OSAL_ERR_OK;
}

osal_err_t osal_ota_abort(osal_ota_handle_t handle)
{
    ota_posix_handle_t *h = (ota_posix_handle_t *)(uintptr_t)handle;

    if (!h || !h->valid) {
        return OSAL_ERR_INVALID_ARG;
    }

    if (h->file) {
        fclose(h->file);
        h->file = NULL;
    }

    // Delete the file
    if (h->filename[0] != '\0') {
        unlink(h->filename);
    }

    // Free the handle
    free(h);

    return OSAL_ERR_OK;
}

osal_err_t osal_ota_set_boot_partition(const osal_ota_partition_t *partition)
{
    if (!partition || !partition->label) {
        return OSAL_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&g_ota_mutex);
    osal_err_t rc = ensure_partitions_initialized();
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_ota_mutex);
        return rc;
    }

    int part_idx = find_partition_index_by_label(partition->label);
    if (part_idx < 0) {
        pthread_mutex_unlock(&g_ota_mutex);
        return OSAL_ERR_INVALID_ARG;
    }

    rc = osal_ota_posix_config_set_boot_partition((uint8_t)part_idx);

    pthread_mutex_unlock(&g_ota_mutex);
    return rc;
}

const osal_ota_partition_t *osal_ota_get_boot_partition(void)
{
    pthread_mutex_lock(&g_ota_mutex);
    osal_err_t rc = ensure_partitions_initialized();
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_ota_mutex);
        return NULL;
    }

    uint8_t boot_idx;
    rc = osal_ota_posix_config_get_boot_partition(&boot_idx);
    const osal_ota_partition_t *p = NULL;
    if (rc == OSAL_ERR_OK) {
        p = get_partition_by_index(boot_idx);
    } else {
        p = get_partition_by_index(0);  // fallback to partition 0
    }
    pthread_mutex_unlock(&g_ota_mutex);
    return p;
}

const osal_ota_partition_t *osal_ota_get_running_partition(void)
{
    // For POSIX, running partition is the same as boot partition
    return osal_ota_get_boot_partition();
}

const osal_ota_partition_t *osal_ota_get_next_update_partition(const osal_ota_partition_t *start_from)
{
    pthread_mutex_lock(&g_ota_mutex);
    if (!g_partitions_inited) {
        osal_err_t rc = ensure_partitions_initialized();
        if (rc != OSAL_ERR_OK) {
            pthread_mutex_unlock(&g_ota_mutex);
            return NULL;
        }
    }

    uint8_t start_idx = g_current_partition_idx;
    if (start_from && start_from->label) {
        int idx = find_partition_index_by_label(start_from->label);
        if (idx >= 0) {
            start_idx = (uint8_t)idx;
        }
    }

    g_current_partition_idx = (uint8_t)((start_idx + 1) % OSAL_OTA_POSIX_PART_COUNT);
    const osal_ota_partition_t *p = get_partition_by_index(g_current_partition_idx);
    pthread_mutex_unlock(&g_ota_mutex);
    return p;
}

static osal_err_t find_app_desc_in_buffer(const uint8_t *buf, size_t buf_len,
        osal_app_desc_embed_t *out)
{
    if (buf_len < sizeof(osal_app_desc_embed_t)) {
        return OSAL_ERR_NOT_FOUND;
    }
    const uint64_t magic = OSAL_APP_DESC_MAGIC;
    const size_t struct_sz = sizeof(osal_app_desc_embed_t);
    const size_t last = buf_len - struct_sz;
    for (size_t i = 0; i <= last; ++i) {
        /* Byte-by-byte compare avoids alignment assumptions on the magic. */
        if (memcmp(buf + i, &magic, sizeof(magic)) != 0) {
            continue;
        }
        osal_app_desc_embed_t candidate;
        memcpy(&candidate, buf + i, struct_sz);
        if (osal_crc32_validate((const uint8_t *)&candidate,
                                OSAL_APP_DESC_CRC_PAYLOAD_LEN,
                                candidate.crc32)) {
            *out = candidate;
            return OSAL_ERR_OK;
        }
        /* CRC mismatch - false positive, keep scanning. */
    }
    return OSAL_ERR_NOT_FOUND;
}

osal_err_t osal_ota_get_partition_description(const osal_ota_partition_t *partition,
        osal_ota_app_desc_t *app_desc)
{
    if (!app_desc) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (!partition || !partition->label) {
        return OSAL_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&g_ota_mutex);
    osal_err_t rc = ensure_partitions_initialized();
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_ota_mutex);
        return rc;
    }
    int part_idx = find_partition_index_by_label(partition->label);
    if (part_idx < 0) {
        pthread_mutex_unlock(&g_ota_mutex);
        return OSAL_ERR_INVALID_ARG;
    }
    char filename[512];
    rc = osal_ota_build_partition_path((uint8_t)part_idx, filename, sizeof(filename));
    pthread_mutex_unlock(&g_ota_mutex);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        return OSAL_ERR_NOT_FOUND;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return OSAL_ERR_FAIL;
    }
    long file_size_signed = ftell(file);
    if (file_size_signed < 0) {
        fclose(file);
        return OSAL_ERR_FAIL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return OSAL_ERR_FAIL;
    }
    size_t to_read = (size_t)file_size_signed;
    if (to_read < sizeof(osal_app_desc_embed_t)) {
        fclose(file);
        return OSAL_ERR_NOT_FOUND;
    }
    uint8_t *buf = (uint8_t *)malloc(to_read);
    if (!buf) {
        fclose(file);
        return OSAL_ERR_NO_MEM;
    }
    size_t got = fread(buf, 1, to_read, file);
    fclose(file);
    if (got != to_read) {
        free(buf);
        return OSAL_ERR_FAIL;
    }

    osal_app_desc_embed_t embed;
    rc = find_app_desc_in_buffer(buf, got, &embed);
    free(buf);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    memset(app_desc, 0, sizeof(*app_desc));
    /* Map embedded layout to public osal_ota_app_desc_t. The magic_word field
     * here is a 32-bit truncation of the 64-bit embedded magic - sufficient as
     * a sanity tag; callers compare version/project_name. */
    app_desc->magic_word = (uint32_t)(embed.magic & 0xFFFFFFFFu);
    app_desc->secure_version = embed.secure_version;
    memcpy(app_desc->version, embed.version, sizeof(app_desc->version));
    memcpy(app_desc->project_name, embed.project_name, sizeof(app_desc->project_name));
    memcpy(app_desc->app_elf_sha256, embed.app_elf_sha256, sizeof(app_desc->app_elf_sha256));
    /* Defensive null-termination on the two string fields. */
    app_desc->version[sizeof(app_desc->version) - 1] = '\0';
    app_desc->project_name[sizeof(app_desc->project_name) - 1] = '\0';

    return OSAL_ERR_OK;
}

osal_err_t osal_ota_get_partition_hash(const osal_ota_partition_t *partition,
                                       size_t size_to_hash,
                                       osal_ota_hash_type_t hash_type,
                                       uint8_t *hash,
                                       size_t hash_len)
{
    if (!partition || !partition->label || !hash) {
        return OSAL_ERR_INVALID_ARG;
    }

    psa_algorithm_t alg;
    size_t required_len;
    switch (hash_type) {
    case OSAL_OTA_HASH_SHA256:
        alg = PSA_ALG_SHA_256;
        required_len = OSAL_OTA_HASH_LEN_SHA256;
        break;
    case OSAL_OTA_HASH_MD5:
        alg = PSA_ALG_MD5;
        required_len = OSAL_OTA_HASH_LEN_MD5;
        break;
    default:
        return OSAL_ERR_NOT_SUPPORTED;
    }
    if (hash_len < required_len) {
        return OSAL_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&g_ota_mutex);
    osal_err_t rc = ensure_partitions_initialized();
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_ota_mutex);
        return rc;
    }

    int part_idx = find_partition_index_by_label(partition->label);
    char filename[512];
    if (part_idx < 0 || part_idx >= OSAL_OTA_POSIX_PART_COUNT) {
        pthread_mutex_unlock(&g_ota_mutex);
        return OSAL_ERR_INVALID_ARG;
    }

    rc = osal_ota_build_partition_path(part_idx, filename, sizeof(filename));
    pthread_mutex_unlock(&g_ota_mutex);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    return compute_file_hash(filename, size_to_hash, alg, hash, required_len);
}

osal_err_t osal_ota_mark_app_valid_cancel_rollback(void)
{
    pthread_mutex_lock(&g_ota_mutex);
    osal_err_t rc = ensure_partitions_initialized();
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_ota_mutex);
        return rc;
    }

    // Mark the current boot partition as last valid
    uint8_t boot_idx;
    rc = osal_ota_posix_config_get_boot_partition(&boot_idx);
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_ota_mutex);
        return rc;
    }

    rc = osal_ota_posix_config_set_last_valid_partition(boot_idx);

    pthread_mutex_unlock(&g_ota_mutex);
    return rc;
}

static osal_err_t osal_ota_mark_app_invalid_rollback(void)
{
    pthread_mutex_lock(&g_ota_mutex);
    osal_err_t rc = ensure_partitions_initialized();
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_ota_mutex);
        return rc;
    }

    // Get the current boot partition (which is invalid)
    uint8_t current_boot_idx;
    rc = osal_ota_posix_config_get_boot_partition(&current_boot_idx);
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_ota_mutex);
        return rc;
    }

    // Mark invalid and rollback to last valid partition
    rc = osal_ota_posix_config_mark_invalid_rollback(current_boot_idx);
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_ota_mutex);
        return rc;
    }

    pthread_mutex_unlock(&g_ota_mutex);
    return rc;
}

osal_err_t osal_ota_mark_app_invalid_rollback_and_reboot(void)
{
    osal_err_t rc = osal_ota_mark_app_invalid_rollback();
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    /* Exit the process with reboot code */
    exit(POSIX_EXIT_REBOOT);
    return OSAL_ERR_OK;
}

osal_err_t osal_ota_get_state_partition(const osal_ota_partition_t *partition,
                                        osal_ota_img_states_t *ota_state)
{
    if (!ota_state) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (!partition || !partition->label) {
        return OSAL_ERR_INVALID_ARG;
    }

    int part_idx = find_partition_index_by_label(partition->label);
    if (part_idx < 0) {
        return OSAL_ERR_INVALID_ARG;
    }

    return osal_ota_posix_config_get_partition_state((uint8_t)part_idx, ota_state);
}

const osal_ota_partition_t *osal_ota_get_last_invalid_partition(void)
{
    // No invalid partitions on POSIX
    return NULL;
}

uint8_t osal_ota_get_app_partition_count(void)
{
    return OSAL_OTA_POSIX_PART_COUNT;
}

osal_err_t osal_ota_erase_last_boot_app_partition(void)
{
    // Not supported on POSIX - no flash to erase
    return OSAL_ERR_NOT_SUPPORTED;
}

bool osal_ota_check_rollback_is_possible(void)
{
    // Rollback not possible on POSIX
    return false;
}
