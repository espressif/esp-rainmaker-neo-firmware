/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_mqtt_downloader_bitmap.c
 * @brief Bit-per-block received/pending tracking for the MQTT OTA downloader.
 */

/* Includes *************************************************************/

#include "ota_mqtt_downloader_bitmap.h"

#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Constants ************************************************************/

static const char *TAG = "rmng_ota_dl_bitmap";

/* Public function definitions ******************************************/

esp_rmaker_error_t mqtt_downloader_bitmap_init(uint32_t block_count, mqtt_downloader_bitmap_t *bitmap)
{
    /* Check parameters */
    if (!bitmap || block_count == 0) {
        OSAL_LOGE(TAG, "Invalid parameters: bitmap: %p, block_count: %" PRIu32, bitmap, block_count);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Initialize bitmap */
    uint32_t cell_count = (block_count + 7) / 8;
    bitmap->bitmap = (uint8_t *)OSAL_MALLOC_EXTRAM(cell_count);
    if (!bitmap->bitmap) {
        OSAL_LOGE(TAG, "Failed to allocate bitmap: %" PRIu32, cell_count);
        return ESP_RMAKER_NO_MEM;
    }

    /* Initialize bitmap */
    memset(bitmap->bitmap, 0xFF, cell_count);
    bitmap->block_count = block_count;
    bitmap->unprocessed_block_count = block_count;

    /* Success */
    return ESP_RMAKER_OK;
}

void mqtt_downloader_bitmap_deinit(mqtt_downloader_bitmap_t *bitmap)
{
    /* Free bitmap */
    free(bitmap->bitmap);
    bitmap->bitmap = NULL;
    bitmap->block_count = 0;
    bitmap->unprocessed_block_count = 0;
}

bool mqtt_downloader_bitmap_is_processed(const mqtt_downloader_bitmap_t *bitmap, int32_t block_id)
{
    /* Check parameters */
    if (!bitmap || bitmap->bitmap == NULL || block_id < 0 || block_id >= bitmap->block_count) {
        OSAL_LOGE(TAG, "Invalid parameters: bitmap: %p, block_id: %" PRId32, bitmap, block_id);
        return false;
    }

    uint32_t cell_index = block_id / 8;
    uint32_t bit_mask = 1 << (block_id % 8);
    return (bitmap->bitmap[cell_index] & bit_mask) == 0;
}

esp_rmaker_error_t mqtt_downloader_bitmap_set_processed(mqtt_downloader_bitmap_t *bitmap, int32_t block_id)
{
    /* Check parameters */
    if (!bitmap || bitmap->bitmap == NULL || block_id < 0 || block_id >= bitmap->block_count) {
        OSAL_LOGE(TAG, "Invalid parameters: bitmap: %p, block_id: %" PRId32, bitmap, block_id);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Set block as processed */
    uint32_t cell_index = block_id / 8;
    uint32_t bit_mask = 1 << (block_id % 8);
    if (!(bitmap->bitmap[cell_index] & bit_mask)) {
        OSAL_LOGW(TAG, "Block %" PRIu32 " already processed", block_id);
        return ESP_RMAKER_INVALID_STATE;
    }
    bitmap->bitmap[cell_index] &= ~bit_mask;
    bitmap->unprocessed_block_count--;

    /* Success */
    return ESP_RMAKER_OK;
}
