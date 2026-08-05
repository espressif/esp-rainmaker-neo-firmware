/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_image_progress.h
 * @brief Private header for progress tracking of the image download
 */

#ifndef __OTA_IMAGE_PROGRESS_H__
#define __OTA_IMAGE_PROGRESS_H__

/* Includes ******************************************************************/

/* Standard includes */
#include <stdint.h>
#include <stdbool.h>

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Types ******************************************************************/

/**
 * @brief Image progress context
 */
typedef struct {
    uint32_t bytes_received; /* Number of bytes received so far */
    struct {
        uint32_t next; /* Next checkpoint to report */
        uint32_t inc; /* Increment for the next checkpoint */
    } checkpoint;
} image_progress_ctx_t;

/* Public function declarations ****************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the image progress context
 *
 * @param[in] p_ctx Image progress context
 * @param[in] filesize Size of the image file
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
esp_rmaker_error_t image_progress_init(image_progress_ctx_t *p_ctx, uint32_t filesize);

/**
 * @brief Add bytes to the image progress
 *
 * @param[in] p_ctx Image handler context
 * @param[in] bytes Number of bytes to add to the progress
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
esp_rmaker_error_t image_progress_add_bytes(image_progress_ctx_t *p_ctx, uint32_t bytes);

/**
 * @brief Seed the progress to an already-completed byte count (for auto-resume).
 *
 * Sets bytes_received and fast-forwards the next checkpoint past it, so progress
 * continues from the resumed position without emitting an event or replaying every
 * checkpoint already passed.
 *
 * @param[in] p_ctx Image progress context
 * @param[in] bytes Number of bytes already received before this session
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
esp_rmaker_error_t image_progress_seed(image_progress_ctx_t *p_ctx, uint32_t bytes);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_IMAGE_PROGRESS_H__ */
