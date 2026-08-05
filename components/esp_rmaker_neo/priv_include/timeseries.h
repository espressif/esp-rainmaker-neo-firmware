/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file timeseries.h
 * @brief Private timeseries implementation for RainMaker Neo.
 */

#ifndef __TIMESERIES_INTERNAL_H__
#define __TIMESERIES_INTERNAL_H__

/* Includes *******************************************************/

/* RMNG includes */
#include "esp_rmaker_error_types.h"
#include "esp_rmaker_val.h"
#include "esp_rmaker_state.h"
#include "network/mqtt_topics.h"

/* Standard includes. */
#include <stdbool.h>
#include <time.h>

/* Public function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize timeseries peripherals.
 * - Underlying queue is initialized.
 * @return ESP_RMAKER_OK on success, error on failure.
 */
esp_rmaker_error_t timeseries_init(void);

/**
 * @brief Deinitialize timeseries peripherals.
 * - Underlying queue is deinitialized.
 * @return ESP_RMAKER_OK on success, error on failure.
 */
esp_rmaker_error_t timeseries_deinit(void);

/**
 * @brief Stop timeseries background activity without tearing down the queue.
 * - Cancels any pending publish retry (flow-stop). Buffered data is retained;
 *   a later start() resumes publishing.
 * @return ESP_RMAKER_OK on success, error on failure.
 */
esp_rmaker_error_t timeseries_stop(void);

/**
 * @brief Push timeseries data from an update ID and value to the queue.
 * @param[in] topic_ctx Topic ctx identifying the Thing whose ts shadow
 *                      the entry targets. Pass ``NULL`` for self.
 * @param[in] update_id Update ID.
 * @param[in] p_val Pointer to the parameter value.
 * @param[in] timestamp_ms Timestamp of the data, in UTC milliseconds since epoch.
 * @param[in] is_cumulative True if the timeseries data is cumulative, false otherwise.
 * @return ESP_RMAKER_OK on success, error on failure.
 */
esp_rmaker_error_t timeseries_push_data_new(const esp_rmaker_topic_ctx_t *topic_ctx, const esp_rmaker_state_update_id_t update_id, const esp_rmaker_param_val_t *p_val, uint64_t timestamp_ms, bool is_cumulative);

#ifdef __cplusplus
}
#endif

#endif /* __TIMESERIES_INTERNAL_H__ */
