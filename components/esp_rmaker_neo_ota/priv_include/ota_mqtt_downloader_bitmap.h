/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_mqtt_downloader_bitmap.h
 * @brief Bit-per-block received/pending tracking for the MQTT OTA downloader.
 *
 * Convention: bit SET (1) = block NOT yet received; bit CLEAR (0) = received.
 * Block 0 maps to LSB of byte 0; block N to bit (N % 8) of byte (N / 8).
 *
 * The bitmap is always allocated with (block_count + 7) / 8 bytes and
 * initialised to 0xFF (all blocks pending). When block_count is not a multiple
 * of 8 the last byte contains padding bits in the high positions; those are set
 * to 1 by mqtt_downloader_bitmap_init and are never cleared - callers that
 * count set bits must mask them out.
 */

#ifndef __OTA_MQTT_DOWNLOADER_BITMAP_H__
#define __OTA_MQTT_DOWNLOADER_BITMAP_H__

/* Includes ******************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "esp_rmaker_error_types.h"

/* Types *********************************************************************/

/**
 * @brief Bitmap tracking which MQTT blocks are still pending.
 */
typedef struct {
    uint8_t *bitmap;                  /**< Heap-allocated byte array; NULL before init / after deinit */
    size_t   block_count;             /**< Total number of logical blocks */
    size_t   unprocessed_block_count; /**< Remaining unprocessed blocks (excludes padding bits) */
} mqtt_downloader_bitmap_t;

/* Public function declarations **********************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocate and initialise a bitmap for @p block_count blocks.
 *
 * All bits are set to 1 (all blocks pending). The allocation size is
 * ceil(block_count / 8) bytes.
 *
 * @param[in]  block_count Number of logical blocks (must be > 0).
 * @param[out] bitmap      Bitmap to initialise.
 * @return ESP_RMAKER_OK, ESP_RMAKER_INVALID_ARG, or ESP_RMAKER_NO_MEM.
 */
esp_rmaker_error_t mqtt_downloader_bitmap_init(uint32_t block_count, mqtt_downloader_bitmap_t *bitmap);

/**
 * @brief Release the bitmap byte array and zero the struct.
 *
 * @param[in] bitmap Bitmap to deinitialise.
 */
void mqtt_downloader_bitmap_deinit(mqtt_downloader_bitmap_t *bitmap);

/**
 * @brief Return true if @p block_id has been marked processed (bit = 0).
 *
 * Returns false for any out-of-range or invalid argument.
 *
 * @param[in] bitmap   Bitmap to query.
 * @param[in] block_id Block index (0-based).
 */
bool mqtt_downloader_bitmap_is_processed(const mqtt_downloader_bitmap_t *bitmap, int32_t block_id);

/**
 * @brief Mark @p block_id as processed (clear the corresponding bit).
 *
 * Decrements unprocessed_block_count. Returns ESP_RMAKER_INVALID_STATE if the
 * block was already processed.
 *
 * @param[in,out] bitmap   Bitmap to update.
 * @param[in]     block_id Block index (0-based).
 * @return ESP_RMAKER_OK, ESP_RMAKER_INVALID_ARG, or ESP_RMAKER_INVALID_STATE.
 */
esp_rmaker_error_t mqtt_downloader_bitmap_set_processed(mqtt_downloader_bitmap_t *bitmap, int32_t block_id);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_MQTT_DOWNLOADER_BITMAP_H__ */
