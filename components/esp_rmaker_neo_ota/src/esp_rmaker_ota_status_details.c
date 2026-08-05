/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_ota_status_details.c
 * @brief Status details implementation
 */

/* Includes **********************************************************************/

/* Declarations */
#include "esp_rmaker_ota_status_details.h"

/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* Platform common includes */
#include "osal_mem_alloc.h"

/* Types **********************************************************************/

/**
 * @brief Function pointer to convert a status details struct to a JSON string.
 * @param[in] details The details union to convert.
 * @param[out] json_str The JSON string to store the result in.
 * @param[in] json_str_size The size of the JSON string to store the result in.
 * @return The length of the JSON string on success, 0 otherwise.
 */
typedef int (* __to_json_fn_t)(const esp_rmaker_ota_status_details_union_t *details, char *json_str, size_t json_str_size);

/* Private function declarations ***************************************************/

/**
 * @brief Convert an IN_PROGRESS status details struct to a JSON string.
 * @param[in] details The details union to convert.
 * @param[out] json_str The JSON string to store the result in.
 * @param[in] json_str_size The size of the JSON string to store the result in.
 * @return The length of the JSON string on success, 0 otherwise.
 */
static int __in_progress_to_json(const esp_rmaker_ota_status_details_union_t *details, char *json_str, size_t json_str_size)
{
    const esp_rmaker_ota_status_details_in_progress_t *in_progress = &details->in_progress;
    return snprintf(json_str, json_str_size, "{\"downloaded_bytes\":%" PRIu32 ",\"total_bytes\":%" PRIu32 "}", in_progress->downloaded_bytes, in_progress->total_bytes);
}

/**
 * @brief Convert a SUCCEEDED status details struct to a JSON string.
 * @param[in] details The details union to convert.
 * @param[out] json_str The JSON string to store the result in.
 * @param[in] json_str_size The size of the JSON string to store the result in.
 * @return The length of the JSON string on success, 0 otherwise.
 */
static int __succeeded_to_json(const esp_rmaker_ota_status_details_union_t *details, char *json_str, size_t json_str_size)
{
    const esp_rmaker_ota_status_details_succeeded_t *succeeded = &details->succeeded;
    return snprintf(json_str, json_str_size, "{\"fw_version\":\"%s\"}", succeeded->fw_version);
}

/**
 * @brief Convert a FAILED status details struct to a JSON string.
 * @param[in] details The details union to convert.
 * @param[out] json_str The JSON string to store the result in.
 * @param[in] json_str_size The size of the JSON string to store the result in.
 * @return The length of the JSON string on success, 0 otherwise.
 */
static int __failed_to_json(const esp_rmaker_ota_status_details_union_t *details, char *json_str, size_t json_str_size)
{
    const esp_rmaker_ota_status_details_failed_t *failed = &details->failed;
    return snprintf(json_str, json_str_size, "{\"reason\":\"%s\"}", failed->reason);
}

/**
 * @brief Convert a REJECTED status details struct to a JSON string.
 * @param[in] details The details union to convert.
 * @param[out] json_str The JSON string to store the result in.
 * @param[in] json_str_size The size of the JSON string to store the result in.
 * @return The length of the JSON string on success, 0 otherwise.
 */
static int __rejected_to_json(const esp_rmaker_ota_status_details_union_t *details, char *json_str, size_t json_str_size)
{
    const esp_rmaker_ota_status_details_rejected_t *rejected = &details->rejected;
    return snprintf(json_str, json_str_size, "{\"reason\":\"%s\"}", rejected->reason);
}

/* Public function declarations ***************************************************/

void esp_rmaker_ota_status_details_fill_starting(esp_rmaker_ota_status_details_t *status_details, const char *job_id, const char *filetype, const char *fw_version)
{
    if (status_details == NULL) {
        return;
    }

    status_details->type = ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_STARTING;
    status_details->details.starting.job_id = job_id;
    status_details->details.starting.filetype = filetype;
    status_details->details.starting.fw_version = fw_version;
}

void esp_rmaker_ota_status_details_fill_in_progress(esp_rmaker_ota_status_details_t *status_details, uint32_t downloaded_bytes, uint32_t total_bytes)
{
    if (status_details == NULL) {
        return;
    }

    status_details->type = ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_IN_PROGRESS;
    status_details->details.in_progress.downloaded_bytes = downloaded_bytes;
    status_details->details.in_progress.total_bytes = total_bytes;
}

void esp_rmaker_ota_status_details_fill_succeeded(esp_rmaker_ota_status_details_t *status_details, const char *filetype, const char *fw_version)
{
    if (status_details == NULL) {
        return;
    }

    status_details->type = ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_SUCCEEDED;
    status_details->details.succeeded.filetype = filetype;
    status_details->details.succeeded.fw_version = fw_version;
}

void esp_rmaker_ota_status_details_fill_failed(esp_rmaker_ota_status_details_t *status_details, const char *reason)
{
    if (status_details == NULL) {
        return;
    }

    status_details->type = ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_FAILED;
    status_details->details.failed.reason = reason;
}

void esp_rmaker_ota_status_details_fill_rejected(esp_rmaker_ota_status_details_t *status_details, const char *reason)
{
    if (status_details == NULL) {
        return;
    }

    status_details->type = ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_REJECTED;
    status_details->details.rejected.reason = reason;
}

void esp_rmaker_ota_status_details_fill_delayed(esp_rmaker_ota_status_details_t *status_details, const char *reason)
{
    if (status_details == NULL) {
        return;
    }

    status_details->type = ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_DELAYED;
    status_details->details.delayed.reason = reason;
}

esp_rmaker_ota_status_details_t *esp_rmaker_ota_status_details_copy(const esp_rmaker_ota_status_details_t *status_details)
{
    if (status_details == NULL) {
        return NULL;
    }

    esp_rmaker_ota_status_details_t *copy = (esp_rmaker_ota_status_details_t *)OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_ota_status_details_t));
    if (copy == NULL) {
        return NULL;
    }

    copy->type = status_details->type;
    copy->details = status_details->details;
    return copy;
}

char *esp_rmaker_ota_status_details_to_json(const esp_rmaker_ota_status_details_t *status_details)
{
    if (status_details == NULL) {
        return NULL;
    }

    __to_json_fn_t to_json_fn = NULL;
    switch (status_details->type) {
    case ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_IN_PROGRESS:
        to_json_fn = __in_progress_to_json;
        break;
    case ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_SUCCEEDED:
        to_json_fn = __succeeded_to_json;
        break;
    case ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_FAILED:
        to_json_fn = __failed_to_json;
        break;
    case ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_REJECTED:
        to_json_fn = __rejected_to_json;
        break;
    default:
        return NULL;
    }

    /* Calculate required JSON size */
    int len = to_json_fn(&status_details->details, NULL, 0);
    if (len <= 0) {
        return NULL;
    }

    /* Allocate memory for JSON string */
    size_t json_str_size = len + 1;
    char *json_str = (char *)OSAL_CALLOC_EXTRAM(json_str_size, sizeof(char));
    if (json_str == NULL) {
        return NULL;
    }

    /* Generate JSON string */
    len = to_json_fn(&status_details->details, json_str, json_str_size);
    if (len <= 0) {
        free(json_str);
        return NULL;
    }
    return json_str;
}
