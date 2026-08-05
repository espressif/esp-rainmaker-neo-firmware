/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file ota_custom_mock.h
 * @brief Sample custom OTA filetype handler + custom job callback for this example.
 *
 * Demonstrates two OTA extension points:
 *  - A filetype handler for custom (non-firmware) OTA filetypes ("mock",
 *    "mock_no_ver"), plumbed in via esp_rmaker_ota_config_t::custom_filetype_handler_lookup.
 *  - A custom job callback for non-OTA jobs, plumbed in via
 *    esp_rmaker_ota_config_t::custom_job_cb (guarded by CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT).
 */

#ifndef __OTA_CUSTOM_MOCK_H__
#define __OTA_CUSTOM_MOCK_H__

/* Standard includes */
#include <stddef.h>

/* OTA includes */
#include "esp_rmaker_ota_filetype_handler.h"

/* Configuration includes */
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lookup the custom mock filetype handler.
 *
 * @param[in] filetype The filetype to lookup.
 * @param[in] filetype_len The length of the filetype.
 * @return The custom mock filetype handler, or NULL if not found.
 */
const esp_rmaker_ota_ft_ctx_t *custom_mock_lookup(const char *filetype, size_t filetype_len);

#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
/**
 * @brief Custom job callback.
 *
 * @param[in] job_doc The job document to be used for the custom job.
 * @param[in] job_doc_len The length of the job document.
 * @return ESP_RMAKER_OK if the job document is recognized and processed.
 * @return ESP_RMAKER_INVALID_ARG if the job document is invalid.
 * @return any other error code if the job document cannot be processed.
 */
esp_rmaker_error_t custom_mock_custom_job_cb(const char *job_doc, size_t job_doc_len);
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

#ifdef __cplusplus
}
#endif

#endif /* __OTA_CUSTOM_MOCK_H__ */
