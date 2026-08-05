/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_timeseries.c
 * @brief Timeseries functions for the data model.
 */

/* Includes *******************************************************/

/* RMNG includes */
#include "timeseries.h"
#include "data_model_internal.h"

/* Standard includes */
#include <stddef.h>

/* Platform includes */
#include "osal_log.h"

/* Global variables *******************************************************/

static const char *TAG = "rmng_dm_timeseries";

/* Function definitions *******************************************************/

esp_rmaker_error_t data_model_timeseries_enabled_for_update_id(const esp_rmaker_state_update_id_t update_id, bool *enabled, bool *is_cumulative)
{
    /* Reset the output parameters */
    *enabled = false;
    *is_cumulative = false;

    /* Check if the update ID is valid */
    const _esp_rmaker_state_update_id_t *u = (const _esp_rmaker_state_update_id_t *)update_id;
    if (!u) {
        OSAL_LOGE(TAG, "Invalid update ID");
        return ESP_RMAKER_INVALID_ARG;
    }
    if (!u->param) {
        OSAL_LOGE(TAG, "Invalid parameter");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Check if the parameter is a time series */
    bool is_ts = u->param->prop_flags & PROP_FLAG_TIME_SERIES;
    bool is_ts_cumulative = u->param->prop_flags & PROP_FLAG_TS_CUMULATIVE;

    /* Set the output parameters */
    *enabled = is_ts | is_ts_cumulative;
    *is_cumulative = is_ts_cumulative;
    return ESP_RMAKER_OK;
}
