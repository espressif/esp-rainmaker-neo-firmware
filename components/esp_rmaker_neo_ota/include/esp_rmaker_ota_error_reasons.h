/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file esp_rmaker_ota_error_reasons.h
 * @brief Public header for the OTA error reasons.
 */

#ifndef __ESP_RMAKER_OTA_ERROR_REASONS_H__
#define __ESP_RMAKER_OTA_ERROR_REASONS_H__

/**
 * @brief OTA error reasons
 */
typedef enum {
    /* Non-reported errors.
     * These errors are not reported to the event loop.
     * They are used internally as error codes for internal state handling.
     */
    /** No error */
    OTA_ERROR_NONE = 0,
    /** No pending jobs - will not be posted to the event loop */
    OTA_ERROR_NO_PENDING_JOBS,
    /** Retry with backoff - e.g., no MQTT connection */
    OTA_ERROR_RETRY_WITH_BACKOFF,

    /* Irrecoverable errors.
     * These errors are fatal and the engine will not be able to recover from them.
     * This usually indicates a critical issue that requires firmware modification to fix.
     */
    /** Initialization failed */
    OTA_ERROR_FATAL_INIT_FAILED,
    /** Unexpected format (critical firmware issue) */
    OTA_ERROR_FATAL_UNEXPECTED_FORMAT,

    /* Network initialization errors.
     * Resetting will cause the engine to re-initialize required network resources from scratch.
     * You should fix the main issue before recovering.
     * e.g., network issues preventing MQTT connection.
     */
    /** Subscription failed */
    OTA_ERROR_SUBSCRIPTION_FAILED,

    /* Job retrieval and execution errors.
     * Resetting will cause the engine to start from a fetch request, i.e., viewing all possible jobs again.
     * You should fix the main issue before recovering.
     * e.g., network issues preventing job document retrieval.
     */
    /** Timeout handler used for getting pending jobs / job document retrieval failed to (re)start.
     * Treating this as a terminal error because the engine might hang indefinitely if there is no way to timeout.
     */
    OTA_ERROR_TIMEOUT_HANDLER_RESTART_FAILED,
    /** Get pending jobs returned invalid JSON format */
    OTA_ERROR_GET_PENDING_INVALID_FORMAT,
    /** Get pending rejected */
    OTA_ERROR_GET_PENDING_REJECTED,
    /** Describe job rejected */
    OTA_ERROR_DESCRIBE_JOB_REJECTED,
    /** Job doc parse failed */
    OTA_ERROR_JOB_DOC_PARSE_FAILED,
} esp_rmaker_ota_error_reason_t;

/* Public function declarations ****************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns a string representation of an OTA error reason
 *
 * @param[in] reason The OTA error reason
 *
 * @return A string representing the OTA error reason
 */
const char *esp_rmaker_ota_error_reason_to_string(esp_rmaker_ota_error_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_OTA_ERROR_REASONS_H__ */
