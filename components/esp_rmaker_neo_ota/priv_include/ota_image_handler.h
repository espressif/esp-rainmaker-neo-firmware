/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_image_handler.h
 * @brief Private header for handling the downloaded OTA image
 */

#ifndef __OTA_IMAGE_HANDLER_H__
#define __OTA_IMAGE_HANDLER_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_rmaker_error_types.h"
#include "ota_signature_verify.h"
#include "esp_rmaker_ota_filetype_handler.h"


/* Types ******************************************************************/

/**
 * @brief Image handler context
 */
typedef struct {
    esp_rmaker_ota_ft_download_handle_t handle; /**< Download handle */
    ota_signature_verify_buffer_t signature; /**< Signature of the image */
    size_t image_size; /**< Size of the image */
    const esp_rmaker_ota_ft_ctx_t *ft_handler; /**< Filetype handler */
    char *expected_fw_version; /**< Firmware version declared in the job document (heap copy, owned by ctx) */
    char *expected_md5; /**< MD5 hex declared in the job document (heap copy, owned by ctx); NULL if none */
} image_handler_ctx_t;

typedef enum {
    IMAGE_HANDLER_STATUS_SUCCESS = 0,
    IMAGE_HANDLER_STATUS_TIMEOUT,
    IMAGE_HANDLER_STATUS_FAILED_SETUP,
    IMAGE_HANDLER_STATUS_FAILED_STREAM_SUBSCRIPTION,
    IMAGE_HANDLER_STATUS_FAILED_UNKNOWN_ERROR,
} image_handler_status_t;

/**
 * @brief Function prototype for the image handler cleanup function
 */
typedef void (*image_handler_cleanup_fn_t)(void);

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Begin handling a downloaded OTA image
 *
 * @param[in] signature_base64 Base64 encoded signature of the image
 * @param[in] image_size Size of the image
 * @param[in] ft_handler Filetype handler
 * @param[in] fw_version Firmware version declared in the job document. The image's embedded version
 *                       must match this exactly before reboot. May be NULL when unknown; the ft
 *                       handler's verify_image_header decides policy.
 * @param[in] file_md5 MD5 hex declared in the job document, or NULL. When non-NULL it is verified
 *                     against the written image on completion.
 * @param[in] resume When true, attempt to resume an interrupted download (no erase) at resume_offset
 *                   via the ft handler's on_download_resume; on any failure this falls back to a
 *                   fresh on_download_begin (full re-download).
 * @param[in] resume_offset Byte offset to resume from (sequential transports); 0 for MQTT.
 * @param[out] p_ctx Image handler context to populate
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
esp_rmaker_error_t image_handler_begin(const char *signature_base64, size_t image_size, const esp_rmaker_ota_ft_ctx_t *ft_handler, const char *fw_version, const char *file_md5, bool resume, size_t resume_offset, image_handler_ctx_t *p_ctx);

/**
 * @brief Write a chunk of data to the partition
 *
 * @param[in] p_ctx Image handler context
 * @param[in] data Pointer to the data to write
 * @param[in] size Size of the data to write
 * @param[in] offset Offset to write the data to
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
esp_rmaker_error_t image_handler_write_chunk(const image_handler_ctx_t *p_ctx, const uint8_t *data, size_t size, uint32_t offset);

/**
 * @brief End the image download
 *
 * @param[in] p_ctx Image handler context
 * @param[in] status Status of the image download
 * @param[in] cleanup_fn Function to call to cleanup the image handler
 */
void image_handler_end_and_cleanup(const image_handler_ctx_t *p_ctx, image_handler_status_t status, image_handler_cleanup_fn_t cleanup_fn);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_IMAGE_HANDLER_H__ */
