/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_error_types.h
 * @brief Error type for RainMaker Neo. Alias of the portable osal_err_t, with
 *        ESP_RMAKER_* spellings of the codes the SDK returns.
 */

#ifndef __ESP_RMAKER_ERROR_TYPES_H__
#define __ESP_RMAKER_ERROR_TYPES_H__

#include "osal_err.h"

/** ESP RainMaker Neo error type. Alias of the portable OSAL error type. */
typedef osal_err_t esp_rmaker_error_t;

#define ESP_RMAKER_OK                OSAL_ERR_OK
#define ESP_RMAKER_FAIL              OSAL_ERR_FAIL
#define ESP_RMAKER_TIMEOUT           OSAL_ERR_TIMEOUT
#define ESP_RMAKER_INVALID_STATE     OSAL_ERR_INVALID_STATE
#define ESP_RMAKER_NO_MEM            OSAL_ERR_NO_MEM
#define ESP_RMAKER_INVALID_ARG       OSAL_ERR_INVALID_ARG
#define ESP_RMAKER_NOT_SUPPORTED     OSAL_ERR_NOT_SUPPORTED
#define ESP_RMAKER_NOT_FOUND         OSAL_ERR_NOT_FOUND

#define ESP_RMAKER_NOT_INITIALIZED        OSAL_ERR_RMAKER_NOT_INITIALIZED
#define ESP_RMAKER_ALREADY_INITIALIZED    OSAL_ERR_RMAKER_ALREADY_INITIALIZED
#define ESP_RMAKER_NOT_CONNECTED          OSAL_ERR_RMAKER_NOT_CONNECTED
#define ESP_RMAKER_ALREADY_CONNECTED      OSAL_ERR_RMAKER_ALREADY_CONNECTED
#define ESP_RMAKER_NOT_DISCONNECTED       OSAL_ERR_RMAKER_NOT_DISCONNECTED
#define ESP_RMAKER_ALREADY_EXISTS         OSAL_ERR_RMAKER_ALREADY_EXISTS

#endif /* __ESP_RMAKER_ERROR_TYPES_H__ */
