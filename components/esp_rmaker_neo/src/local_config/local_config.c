/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file local_config.c
 * @brief Local configuration manager using an NVS factory partition.
 * @note Generate the factory partition with `tools/factory_nvs_gen`.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "local_config.h"

/* Standard includes */
#include <stdlib.h>
#include <string.h>

/* platform */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* NVS */
#include "osal_storage.h"

/* Constants **********************************************************************/

static const char *TAG = "rmng_local_config";

/* Variables **********************************************************************/

/**
 * @brief NVS handle for the local configuration partition.
 */
osal_storage_handle_t esp_rmaker_local_config_nvs_handle = NULL;

/* Public function definitions ****************************************************/

/* --- Initialize and deinitialize ---------------------------------------------- */

esp_rmaker_error_t esp_rmaker_local_config_init(void)
{
    esp_rmaker_error_t err;

    /* Load the local configuration partition handle */
    if (esp_rmaker_local_config_nvs_handle == NULL) {
        err = esp_rmaker_load_nvs_handle(RMAKER_NVS_PART_NAME, RMAKER_NVS_LOCAL_CONFIG_NAMESPACE, &esp_rmaker_local_config_nvs_handle);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to load the local configuration partition handle");
            return err;
        }
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_local_config_deinit(void)
{
    /* Close the local configuration partition */
    if (esp_rmaker_local_config_nvs_handle != NULL) {
        osal_err_t nvs_err = osal_storage_close(esp_rmaker_local_config_nvs_handle);
        if (nvs_err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to close the local configuration partition");
            return ESP_RMAKER_FAIL;
        }
        esp_rmaker_local_config_nvs_handle = NULL;
    }

    return ESP_RMAKER_OK;
}

/* --- Group information -------------------------------------------------------- */

static int __compare_subgroups(const void *a, const void *b)
{
    return strcmp(*(const char (*)[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE]) a, *(const char (*)[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE]) b);
}

esp_rmaker_error_t esp_rmaker_local_config_format_group_info_str(const char *primary, char subgroups[][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE], size_t num_subgroups, char **p_group_str)
{
    if (p_group_str == NULL || primary == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Calculate the length of the group string */
    size_t group_str_len = strlen(primary);
    if (subgroups) {
        for (size_t i = 0; i < num_subgroups; i++) {
            size_t subgroup_len = strlen(subgroups[i]);
            if (subgroup_len > 0) {
                group_str_len += subgroup_len + 1;
            }
        }
    }

    /* Allocate the group string */
    *p_group_str = OSAL_CALLOC_EXTRAM(group_str_len + 1, sizeof(char));
    if (*p_group_str == NULL) {
        return ESP_RMAKER_NO_MEM;
    }

    /* Return early if there is no group string length is 0 */
    if (group_str_len == 0) {
        (*p_group_str)[0] = '\0';
        return ESP_RMAKER_OK;
    }

    /* Copy the primary group, then each subgroup, tracking the write cursor so the
     * bytes written here cannot drift from the bytes counted above. */
    size_t off = strlen(primary);
    memcpy(*p_group_str, primary, off);
    if (subgroups) {
        /* Sort the subgroups */
        qsort(subgroups, num_subgroups, sizeof(subgroups[0]), __compare_subgroups);

        /* Copy the subgroups */
        for (size_t i = 0; i < num_subgroups; i++) {
            size_t subgroup_len = strlen(subgroups[i]);
            /* Skip empties exactly as the sizing pass does. */
            if (subgroup_len == 0) {
                continue;
            }
            (*p_group_str)[off++] = '-';
            memcpy(*p_group_str + off, subgroups[i], subgroup_len);
            off += subgroup_len;
        }
    }
    (*p_group_str)[off] = '\0';

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_local_config_parse_group_info_str(const char *group_info_str, char *primary_out, size_t primary_size, char subgroups[][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE], size_t max_subgroups, size_t *num_subgroups_out)
{
    if (primary_out == NULL || primary_size == 0 || num_subgroups_out == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    primary_out[0] = '\0';
    *num_subgroups_out = 0;

    if (group_info_str == NULL || group_info_str[0] == '\0') {
        return ESP_RMAKER_OK;
    }

    const char *p = group_info_str;
    const char *seg_start = p;
    size_t seg_len = 0;
    int seg_index = 0; /* 0 = primary, 1+ = subgroup */

    while (1) {
        if (*p == '-' || *p == '\0') {
            seg_len = (size_t)(p - seg_start);
            if (seg_index == 0) {
                if (seg_len >= primary_size) {
                    seg_len = primary_size - 1;
                }
                memcpy(primary_out, seg_start, seg_len);
                primary_out[seg_len] = '\0';
            } else if (seg_len > 0 && subgroups != NULL && *num_subgroups_out < max_subgroups) {
                /* Pack into the next free slot rather than indexing by segment
                 * position: an empty segment ("a--b") is skipped, so position-based
                 * indexing would leave the skipped slot unwritten while still
                 * counting it, handing the caller an uninitialised subgroup. */
                size_t copy_len = seg_len;
                if (copy_len >= RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE) {
                    copy_len = RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE - 1;
                }
                memcpy(subgroups[*num_subgroups_out], seg_start, copy_len);
                subgroups[*num_subgroups_out][copy_len] = '\0';
                (*num_subgroups_out)++;
            }
            seg_index++;
            if (*p == '\0') {
                break;
            }
            seg_start = p + 1;
        }
        p++;
    }

    return ESP_RMAKER_OK;
}

/* --- Alexa configuration ----------------------------------------------------- */

bool esp_rmaker_local_config_get_alexa_en(void)
{
    bool value = false;
    esp_rmaker_error_t err = esp_rmaker_nvs_get_bool_with_handle(esp_rmaker_local_config_nvs_handle, RMAKER_NVS_LOCAL_CONFIG_KEY_ALEXA_EN, &value);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to get Alexa en from NVS, defaulting to false");
        value = false;
    }
    return value;
}

/* --- GVA configuration -------------------------------------------------------- */

bool esp_rmaker_local_config_get_gva_en(void)
{
    bool value = false;
    esp_rmaker_error_t err = esp_rmaker_nvs_get_bool_with_handle(esp_rmaker_local_config_nvs_handle, RMAKER_NVS_LOCAL_CONFIG_KEY_GVA_EN, &value);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to get GVA en from NVS, defaulting to false");
        value = false;
    }
    return value;
}
