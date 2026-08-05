/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_state.h
 * @brief State maintenance for the ESP RainMaker Neo SDK.
 */

#ifndef __ESP_RMAKER_STATE_H__
#define __ESP_RMAKER_STATE_H__

/* Includes *******************************************************/

/* Standard includes. */
#include <stdint.h>
#include <stdbool.h>

/* Error includes. */
#include "esp_rmaker_error_types.h"

/* Value includes. */
#include "esp_rmaker_val.h"

/* Public types *******************************************************/

/**
 * @brief ESP RainMaker Neo Update ID
 *
 * Opaque handle identifying one pending state change (one parameter of one node). Created by the
 * data model, not by application code.
 */
typedef void *esp_rmaker_state_update_id_t;

/** Parameter read/write request source */
typedef enum {
    /** Request triggered in the init sequence i.e. when a value is found
     * in persistent memory for parameters with PROP_FLAG_PERSIST.
     */
    ESP_RMAKER_REQ_SRC_INIT,
    /** Request received from cloud */
    ESP_RMAKER_REQ_SRC_CLOUD,
    /** Request received when a schedule has triggered */
    ESP_RMAKER_REQ_SRC_SCHEDULE,
    /** Request received when a scene has been activated */
    ESP_RMAKER_REQ_SRC_SCENE_ACTIVATE,
    /** Request received when a scene has been deactivated */
    ESP_RMAKER_REQ_SRC_SCENE_DEACTIVATE,
    /** Request received from a local controller */
    ESP_RMAKER_REQ_SRC_LOCAL,
    /** Request initiated from firmware/console commands */
    ESP_RMAKER_REQ_SRC_FIRMWARE,
    /** This will always be the last value. Any value equal to or
     * greater than this should be considered invalid.
     */
    ESP_RMAKER_REQ_SRC_MAX,
} esp_rmaker_req_src_t;

/* Public functions *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert a request source to a string.
 *
 * @param[in] src Request source.
 *
 * @return String representation of the request source.
 */
const char *esp_rmaker_req_src_to_string(esp_rmaker_req_src_t src);

/**
 * @brief Mark an update ID for update.
 *
 * Evaluates any automation triggers registered for the change, adds it to the pending-report list
 * and schedules a (delayed) report to the cloud.
 *
 * @note Ownership of `update_id` transfers to the SDK. The caller must not dereference or free it
 *       after this call.
 *
 * @param[in] update_id Update ID.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if update_id is NULL.
 * @return error code in case of failure.
 */
esp_rmaker_error_t esp_rmaker_state_mark_for_update(esp_rmaker_state_update_id_t update_id);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_STATE_H__ */
