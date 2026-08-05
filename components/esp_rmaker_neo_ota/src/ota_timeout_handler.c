/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_timeout_handler.c
 * @brief Implementation of the timeout handler
 */

/* Includes ******************************************************************/

/* Declarations */
#include "ota_timeout_handler.h"

/* Platform common includes */
#include "osal_scheduler.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Standard includes */
#include <inttypes.h>

/* Types ******************************************************************/

/**
 * @brief Timeout handler context
 */
typedef struct {
    /** Timeout value in milliseconds */
    uint32_t timeout_ms;
    /** Callback function */
    rmaker_ota_timeout_handler_callback_t callback;
    /** Private data to be passed to the callback function */
    void *priv_data;
    /** Scheduled task handle */
    osal_scheduler_task_handle_t task_handle;
} rmaker_ota_timeout_handler_ctx_t;

/* Constants ******************************************************************/

/**
 * @brief Tag for logging
 */
static const char *TAG = "rmng_ota_timeout";

/* Private function declarations ****************************************************/

/**
 * @brief Timeout handler task
 *
 * @param[in] ctx_arg Context argument
 */
static void timeout_handler_task(void *ctx_arg);

/* Private function definitions ****************************************************/

static void timeout_handler_task(void *ctx_arg)
{
    rmaker_ota_timeout_handler_ctx_t *ctx = (rmaker_ota_timeout_handler_ctx_t *) ctx_arg;
    OSAL_LOGD(TAG, "Timeout handler task called for callback %p", ctx->callback);
    ctx->callback(ctx->priv_data);
}

/* Public function definitions ****************************************************/

esp_rmaker_error_t rmaker_ota_timeout_handler_init(const rmaker_ota_timeout_handler_config_t *config, rmaker_ota_timeout_handler_handle_t *p_handle)
{
    if (!config || !p_handle) {
        return ESP_RMAKER_INVALID_ARG;
    }

    rmaker_ota_timeout_handler_ctx_t *ctx = (rmaker_ota_timeout_handler_ctx_t *) OSAL_CALLOC_EXTRAM(1, sizeof(rmaker_ota_timeout_handler_ctx_t));
    if (!ctx) {
        return ESP_RMAKER_NO_MEM;
    }

    ctx->timeout_ms = config->timeout_ms;
    ctx->callback = config->callback;
    ctx->priv_data = config->priv_data;

    *p_handle = (rmaker_ota_timeout_handler_handle_t) ctx;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t rmaker_ota_timeout_handler_deinit(rmaker_ota_timeout_handler_handle_t handle)
{
    if (!handle) {
        return ESP_RMAKER_INVALID_ARG;
    }

    rmaker_ota_timeout_handler_ctx_t *ctx = (rmaker_ota_timeout_handler_ctx_t *) handle;

    /* Cancel the scheduled task */
    if (ctx->task_handle) {
        osal_scheduler_cancel_task(&ctx->task_handle);
    }
    free(ctx);

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t rmaker_ota_timeout_handler_restart(rmaker_ota_timeout_handler_handle_t handle)
{
    if (!handle) {
        return ESP_RMAKER_INVALID_ARG;
    }

    rmaker_ota_timeout_handler_ctx_t *ctx = (rmaker_ota_timeout_handler_ctx_t *) handle;

    osal_err_t err;
    if (ctx->task_handle) {
        err = osal_scheduler_reset_timer(ctx->task_handle, ctx->timeout_ms);
    } else {
        err = osal_scheduler_schedule_task(&ctx->task_handle, ctx->timeout_ms, timeout_handler_task, ctx);
    }
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to restart timeout handler");
        return ESP_RMAKER_FAIL;
    }
    OSAL_LOGD(TAG, "Timeout handler restarted with timeout: %" PRIu32 " ms", ctx->timeout_ms);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t rmaker_ota_timeout_handler_stop(rmaker_ota_timeout_handler_handle_t handle)
{
    OSAL_LOGD(TAG, "Stopping timeout handler for handle: %p", handle);
    if (!handle) {
        return ESP_RMAKER_INVALID_ARG;
    }

    rmaker_ota_timeout_handler_ctx_t *ctx = (rmaker_ota_timeout_handler_ctx_t *) handle;
    if (ctx->task_handle) {
        return osal_scheduler_stop_timer(ctx->task_handle) == OSAL_ERR_OK ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}
