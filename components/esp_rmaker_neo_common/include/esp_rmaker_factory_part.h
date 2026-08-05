/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_factory_part.h
 * @brief Common functions for reading the ESP RainMaker Neo factory partition.
 */

#ifndef __ESP_RMAKER_FACTORY_PART_H__
#define __ESP_RMAKER_FACTORY_PART_H__


/* Includes **********************************************************************/

/* Error types includes */
#include "esp_rmaker_error_types.h"

/* Public functions **************************************************************/

#ifdef __cplusplus
extern "C" {
#endif


/* --- Initialize and deinitialize ---------------------------------------------- */

/**
 * @brief Initialize the factory partition.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_init(void);

/**
 * @brief Deinitialize the factory partition.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_deinit(void);

/* Remaining accessors are now in esp_rmaker_credentials.h */

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_FACTORY_PART_H__ */
