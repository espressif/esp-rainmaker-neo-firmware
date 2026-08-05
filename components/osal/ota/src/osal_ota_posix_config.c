/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ota_posix_config.c
 * @brief POSIX OTA configuration management - binary config file with partition flags
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osal_mem_alloc.h"
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>
#include "osal_ota.h"
#include "osal_ota_posix_shared.h"

// Configuration file structure - binary format
typedef struct {
    uint32_t magic;                    // Magic number to validate config
    uint8_t partition_count;           // Number of partitions
    uint8_t boot_partition_idx;        // Index of boot partition (0-255)
    uint8_t last_valid_partition_idx;  // Index of last valid partition (0-255)
    osal_ota_img_states_t partition_states[OSAL_OTA_POSIX_PART_COUNT];  // States for each partition
} ota_posix_config_t;

#define OTA_POSIX_CONFIG_MAGIC 0x4F544143  // "OTAC"

static const char *g_config_filename = OSAL_OTA_POSIX_BASE_DIR "/" OSAL_OTA_POSIX_CONFIG_FILE;

static osal_err_t ensure_config_dir(void)
{
    struct stat st;
    char *config_path_copy = OSAL_STRDUP_EXTRAM(g_config_filename);
    if (!config_path_copy) {
        return OSAL_ERR_NO_MEM;
    }

    char *dir_path = dirname(config_path_copy);
    if (!dir_path) {
        free(config_path_copy);
        return OSAL_ERR_FAIL;
    }

    if (stat(dir_path, &st) == -1) {
        if (mkdir(dir_path, 0700) != 0) {
            free(config_path_copy);
            return OSAL_ERR_FAIL;
        }
    }
    free(config_path_copy);
    return OSAL_ERR_OK;
}

static osal_err_t load_config(ota_posix_config_t *config)
{
    if (!config) {
        return OSAL_ERR_INVALID_ARG;
    }

    FILE *file = fopen(g_config_filename, "rb");
    if (!file) {
        // Config doesn't exist, return default
        goto load_config_return_default;
    }

    size_t read_bytes = fread(config, 1, sizeof(*config), file);
    fclose(file);

    if (read_bytes != sizeof(*config)) {
        // Invalid config, return defaults
        goto load_config_return_default;
    }

    // Validate magic
    if (config->magic != OTA_POSIX_CONFIG_MAGIC) {
        // Invalid magic, return error
        memset(config, 0, sizeof(*config));
        return OSAL_ERR_NOT_SUPPORTED;
    }

    return OSAL_ERR_OK;

load_config_return_default:
    memset(config, 0, sizeof(*config));
    config->magic = OTA_POSIX_CONFIG_MAGIC;
    config->partition_count = OSAL_OTA_POSIX_PART_COUNT;
    config->boot_partition_idx = 0;
    config->last_valid_partition_idx = 0;
    // Initialize all partition states to VALID
    for (int i = 0; i < OSAL_OTA_POSIX_PART_COUNT; i++) {
        config->partition_states[i] = (uint8_t)OSAL_OTA_IMG_VALID;
    }
    return OSAL_ERR_OK;
}

static osal_err_t save_config(const ota_posix_config_t *config)
{
    if (!config) {
        return OSAL_ERR_INVALID_ARG;
    }

    osal_err_t rc = ensure_config_dir();
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    FILE *file = fopen(g_config_filename, "wb");
    if (!file) {
        return OSAL_ERR_FAIL;
    }

    size_t written = fwrite(config, 1, sizeof(*config), file);
    fclose(file);

    if (written != sizeof(*config)) {
        return OSAL_ERR_FAIL;
    }

    return OSAL_ERR_OK;
}

osal_err_t osal_ota_posix_config_get_boot_partition(uint8_t *partition_idx)
{
    if (!partition_idx) {
        return OSAL_ERR_INVALID_ARG;
    }

    ota_posix_config_t config;
    osal_err_t rc = load_config(&config);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    *partition_idx = config.boot_partition_idx;
    return OSAL_ERR_OK;
}

osal_err_t osal_ota_posix_config_set_boot_partition(uint8_t partition_idx)
{
    if (partition_idx >= OSAL_OTA_POSIX_PART_COUNT) {
        return OSAL_ERR_INVALID_ARG;
    }

    ota_posix_config_t config;
    osal_err_t rc = load_config(&config);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    config.boot_partition_idx = partition_idx;

    return save_config(&config);
}

osal_err_t osal_ota_posix_config_get_last_valid_partition(uint8_t *partition_idx)
{
    if (!partition_idx) {
        return OSAL_ERR_INVALID_ARG;
    }

    ota_posix_config_t config;
    osal_err_t rc = load_config(&config);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    *partition_idx = config.last_valid_partition_idx;
    return OSAL_ERR_OK;
}

osal_err_t osal_ota_posix_config_set_last_valid_partition(uint8_t partition_idx)
{
    if (partition_idx >= OSAL_OTA_POSIX_PART_COUNT) {
        return OSAL_ERR_INVALID_ARG;
    }

    ota_posix_config_t config;
    osal_err_t rc = load_config(&config);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    config.last_valid_partition_idx = partition_idx;
    config.partition_states[partition_idx] = OSAL_OTA_IMG_VALID;

    return save_config(&config);
}

osal_err_t osal_ota_posix_config_mark_invalid_rollback(uint8_t current_partition_idx)
{
    if (current_partition_idx >= OSAL_OTA_POSIX_PART_COUNT) {
        return OSAL_ERR_INVALID_ARG;
    }

    ota_posix_config_t config;
    osal_err_t rc = load_config(&config);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    // Set the boot partition to the last valid partition
    config.boot_partition_idx = config.last_valid_partition_idx;

    // Set the current partition to INVALID
    config.partition_states[current_partition_idx] = OSAL_OTA_IMG_INVALID;

    return save_config(&config);
}

osal_err_t osal_ota_posix_config_get_partition_state(uint8_t partition_idx, osal_ota_img_states_t *state)
{
    if (partition_idx >= OSAL_OTA_POSIX_PART_COUNT || !state) {
        return OSAL_ERR_INVALID_ARG;
    }

    ota_posix_config_t config;
    osal_err_t rc = load_config(&config);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    *state = config.partition_states[partition_idx];
    return OSAL_ERR_OK;
}

osal_err_t osal_ota_posix_config_set_partition_state(uint8_t partition_idx, osal_ota_img_states_t state)
{
    if (partition_idx >= OSAL_OTA_POSIX_PART_COUNT) {
        return OSAL_ERR_INVALID_ARG;
    }

    ota_posix_config_t config;
    osal_err_t rc = load_config(&config);
    if (rc != OSAL_ERR_OK) {
        return rc;
    }

    config.partition_states[partition_idx] = state;

    return save_config(&config);
}
