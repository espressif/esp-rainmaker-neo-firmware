/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file esp_rmaker_ota_error_reasons.c
 * @brief Implementation of the OTA error reasons.
 */

#include "esp_rmaker_ota_error_reasons.h"

const char *esp_rmaker_ota_error_reason_to_string(esp_rmaker_ota_error_reason_t reason)
{
    switch (reason) {
    case OTA_ERROR_NONE:
        return "NONE";
    case OTA_ERROR_SUBSCRIPTION_FAILED:
        return "SUBSCRIPTION_FAILED";
    case OTA_ERROR_RETRY_WITH_BACKOFF:
        return "RETRY_WITH_BACKOFF";
    case OTA_ERROR_TIMEOUT_HANDLER_RESTART_FAILED:
        return "TIMEOUT_HANDLER_RESTART_FAILED";
    case OTA_ERROR_NO_PENDING_JOBS:
        return "NO_PENDING_JOBS";
    case OTA_ERROR_GET_PENDING_INVALID_FORMAT:
        return "GET_PENDING_INVALID_FORMAT";
    case OTA_ERROR_GET_PENDING_REJECTED:
        return "GET_PENDING_REJECTED";
    case OTA_ERROR_DESCRIBE_JOB_REJECTED:
        return "DESCRIBE_JOB_REJECTED";
    case OTA_ERROR_JOB_DOC_PARSE_FAILED:
        return "JOB_DOC_PARSE_FAILED";
    default:
        return "UNKNOWN";
    }
}
