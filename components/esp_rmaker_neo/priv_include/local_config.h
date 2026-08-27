/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file local_config.h
 * @brief Local configuration for RainMaker Neo
 */

#ifndef __LOCAL_CONFIG_H__
#define __LOCAL_CONFIG_H__

/* Standard includes */
#include <stddef.h>
#include <stdbool.h>

/* Error types includes */
#include "esp_rmaker_error_types.h"

/* Constants includes */
#include "constants/cloud.h"
#include "constants/nvs.h"

/* NVS includes */
#include "util/esp_rmaker_nvs.h"

/* Credentials includes */
#include "esp_rmaker_credentials.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Variables **********************************************************************/

extern osal_storage_handle_t esp_rmaker_local_config_nvs_handle;

/* Public functions **************************************************************/

/**
 * @brief Initialize the local configuration in NVS.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_local_config_init(void);

/**
 * @brief Deinitialize the local configuration in NVS.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_local_config_deinit(void);

/* --- Group information -------------------------------------------------------- */

/**
 * @brief Format the group information as a string for use in MQTT topics/shadow names/etc.
 * @note Format is "<primary_group>[-<subgroup1>-<subgroup2>-...]".
 * @param[in] primary The primary group
 * @param[in] subgroups The subgroups. Must be an array of RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE strings. NULL if no subgroups are set.
 * @param[in] num_subgroups The number of subgroups. 0 if no subgroups are set.
 * @param[out] p_group_str Pointer to the string buffer to return the group information. Must be freed by the caller.
 * @return
 * - ESP_RMAKER_OK on success
 * - ESP_RMAKER_NO_MEM if the group string cannot be allocated
 */
esp_rmaker_error_t esp_rmaker_local_config_format_group_info_str(const char *primary, char subgroups[][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE], size_t num_subgroups, char **p_group_str);

/**
 * @brief Parse a group information string into primary and subgroups.
 * @note Expects format "<primary>[-<subgroup1>-<subgroup2>-...]". Subgroup IDs must not contain '-'.
 * @param[in] group_info_str The group information string (may be NULL or empty).
 * @param[out] primary_out Buffer to store the primary group ID.
 * @param[in] primary_size Size of primary_out (use RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE).
 * @param[out] subgroups Array to store subgroup IDs (may be NULL if max_subgroups is 0).
 * @param[in] max_subgroups Maximum number of subgroups to parse (e.g. RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT).
 * @param[out] num_subgroups_out On success, set to the number of subgroups parsed.
 * @return
 * - ESP_RMAKER_OK on success
 * - ESP_RMAKER_INVALID_ARG if group_info_str is NULL, primary_out is NULL, primary_size is 0, or num_subgroups_out is NULL
 */
esp_rmaker_error_t esp_rmaker_local_config_parse_group_info_str(const char *group_info_str, char *primary_out, size_t primary_size, char subgroups[][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE], size_t max_subgroups, size_t *num_subgroups_out);

/**
 * @brief Get the group information string. Must be freed by the caller.
 * @return The group information string. Must be freed by the caller if not NULL.
 */
#define esp_rmaker_local_config_get_group_info_str() esp_rmaker_nvs_get_string_with_handle(esp_rmaker_local_config_nvs_handle, RMAKER_NVS_LOCAL_CONFIG_KEY_GROUP_INFO_STRING)

/**
 * @brief Set the group information string
 * @param[in] group_info_str (const char *) The group information string.
 * @return
 * - ESP_RMAKER_OK on success
 * - ESP_RMAKER_INVALID_ARG if the group string is NULL
 * - ESP_RMAKER_FAIL if the group string cannot be set in NVS
 */
#define esp_rmaker_local_config_set_group_info_str(group_info_str) esp_rmaker_nvs_update_string_with_handle(esp_rmaker_local_config_nvs_handle, RMAKER_NVS_LOCAL_CONFIG_KEY_GROUP_INFO_STRING, group_info_str)

/* --- Alexa configuration ----------------------------------------------------- */

/**
 * @brief Get the Alexa configuration
 * @return The Alexa configuration
 */
bool esp_rmaker_local_config_get_alexa_en(void);

/**
 * @brief Set the Alexa configuration
 * @param[in] alexa_en True if Alexa is enabled, false otherwise
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
#define esp_rmaker_local_config_set_alexa_en(alexa_en) esp_rmaker_nvs_update_bool_with_handle(esp_rmaker_local_config_nvs_handle, RMAKER_NVS_LOCAL_CONFIG_KEY_ALEXA_EN, alexa_en)

/* --- GVA configuration ------------------------------------------------------- */

/**
 * @brief Get the GVA (Google Voice Assistant) notification enable flag
 * @return True if GVA notifications are enabled, false otherwise
 */
bool esp_rmaker_local_config_get_gva_en(void);

/**
 * @brief Set the GVA notification enable flag
 * @param[in] gva_en True if GVA is enabled, false otherwise
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
#define esp_rmaker_local_config_set_gva_en(gva_en) esp_rmaker_nvs_update_bool_with_handle(esp_rmaker_local_config_nvs_handle, RMAKER_NVS_LOCAL_CONFIG_KEY_GVA_EN, gva_en)

/* --- SmartThings configuration ----------------------------------------------- */

/**
 * @brief Get the SmartThings enable flag
 * @return True if SmartThings is enabled, false otherwise
 */
bool esp_rmaker_local_config_get_st_en(void);

/**
 * @brief Set the SmartThings enable flag
 * @param[in] st_en True if SmartThings is enabled, false otherwise
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
#define esp_rmaker_local_config_set_st_en(st_en) esp_rmaker_nvs_update_bool_with_handle(esp_rmaker_local_config_nvs_handle, RMAKER_NVS_LOCAL_CONFIG_KEY_ST_EN, st_en)

/* --- Versioning -------------------------------------------------------------- */

/**
 * @brief Get the version number
 * @param[in] key The key of the version
 * @return The version number, if not found, -1 is returned
 */
#define esp_rmaker_local_config_get_version(key) esp_rmaker_nvs_get_int_default_with_handle(esp_rmaker_local_config_nvs_handle, key, -1)

/**
 * @brief Set the version number
 * @param[in] key The key of the version
 * @param[in] version The version number
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
#define esp_rmaker_local_config_set_version(key, version) esp_rmaker_nvs_update_int_with_handle(esp_rmaker_local_config_nvs_handle, key, version)

/**
 * @brief Get a details string
 * @param[in] key The key of the details entry
 * @return The details string, or NULL if not found. Must be freed by the caller.
 */
#define esp_rmaker_local_config_get_details(key) esp_rmaker_nvs_get_string_with_handle(esp_rmaker_local_config_nvs_handle, key)

/**
 * @brief Set a details string
 * @param[in] key The key of the details entry
 * @param[in] details The details string
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
#define esp_rmaker_local_config_set_details(key, details) esp_rmaker_nvs_update_string_with_handle(esp_rmaker_local_config_nvs_handle, key, details)

/* --- Schedule configuration -------------------------------------------------- */

/**
 * @brief Get the schedule version number
 * @return The schedule version number, if not found, -1 is returned
 */
#define esp_rmaker_local_config_get_sched_ver() esp_rmaker_local_config_get_version(RMAKER_NVS_LOCAL_CONFIG_KEY_SCHED_VER)

/**
 * @brief Set the schedule version number
 * @param[in] sched_ver (int) The schedule version number
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
#define esp_rmaker_local_config_set_sched_ver(sched_ver) esp_rmaker_local_config_set_version(RMAKER_NVS_LOCAL_CONFIG_KEY_SCHED_VER, sched_ver)


/**
 * @brief Get the schedule details
 * @return The schedule details. Must be freed by the caller if not NULL.
 */
#define esp_rmaker_local_config_get_sched_details() esp_rmaker_local_config_get_details(RMAKER_NVS_LOCAL_CONFIG_KEY_SCHED_DETAILS)

/**
 * @brief Set the schedule details
 * @param[in] sched_details (const char *) The schedule details.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
#define esp_rmaker_local_config_set_sched_details(sched_details) esp_rmaker_local_config_set_details(RMAKER_NVS_LOCAL_CONFIG_KEY_SCHED_DETAILS, sched_details)

/* --- Trigger configuration --------------------------------------------------- */

/**
 * @brief Get the trigger version number
 * @return The trigger version number, if not found, -1 is returned
 */
#define esp_rmaker_local_config_get_trigger_ver() esp_rmaker_local_config_get_version(RMAKER_NVS_LOCAL_CONFIG_KEY_TRIGGER_VER)

/**
 * @brief Set the trigger version number
 * @param[in] trigger_ver (int) The trigger version number
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
#define esp_rmaker_local_config_set_trigger_ver(trigger_ver) esp_rmaker_local_config_set_version(RMAKER_NVS_LOCAL_CONFIG_KEY_TRIGGER_VER, trigger_ver)

/**
 * @brief Get the trigger details (compact binary codec blob).
 * @param[out] p_len (size_t *) Set to the blob length on success.
 * @return (uint8_t *) malloc'd blob (caller frees) or NULL if absent.
 */
#define esp_rmaker_local_config_get_trigger_details(p_len) \
    esp_rmaker_nvs_get_binary_with_handle(esp_rmaker_local_config_nvs_handle, RMAKER_NVS_LOCAL_CONFIG_KEY_TRIGGER_DETAILS, (p_len))

/**
 * @brief Set the trigger details (compact binary codec blob).
 * @param[in] data (const void *) The blob bytes.
 * @param[in] len (size_t) Number of bytes.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
#define esp_rmaker_local_config_set_trigger_details(data, len) \
    esp_rmaker_nvs_update_binary_with_handle(esp_rmaker_local_config_nvs_handle, RMAKER_NVS_LOCAL_CONFIG_KEY_TRIGGER_DETAILS, (data), (len))

#ifdef __cplusplus
}
#endif

#endif /* __LOCAL_CONFIG_H__ */
