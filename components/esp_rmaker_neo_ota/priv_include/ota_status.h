/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_status.h
 * @brief Private header for OTA status management
 */

#ifndef __OTA_STATUS_H__
#define __OTA_STATUS_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Job includes */
#include "jobs.h"

/* Types ************************************************************************/

/**
 * @brief OTA status object
 */
typedef struct {
    JobCurrentStatus_t status;
    char job_id[JOBID_MAX_LENGTH + 1];
    size_t job_id_len;
    char *status_details_str;
    size_t status_details_str_len;
} ota_status_update_t;

/**
 * @brief Return values for the OTA status update response
 */
typedef struct {
    char job_id[JOBID_MAX_LENGTH + 1];
    size_t job_id_len;
    bool is_terminal;
    /** Rejected responses only: the reject code means this job execution can never
     * accept another update (deleted, already terminal, or our request was malformed),
     * so the caller must stop waiting for an acceptance instead of retrying forever. */
    bool is_unrecoverable_reject;
} ota_status_update_response_return_t;

/* Public function declarations **************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the OTA status cache
 *
 * @param[in] thing_name The pointer to the thing name to use for the OTA status cache
 * @param[in] thing_name_length The length of the thing name
 * @note The thing name must remain valid for the lifetime of the OTA status manager, i.e., until ota_status_deinit() is called.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t ota_status_init(const char *thing_name, size_t thing_name_length);

/**
 * @brief Deinitialize the OTA status cache
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t ota_status_deinit(void);

/**
 * @brief Set the initial expected version for a job
 *
 * @param[in] job_id The job ID to set
 * @param[in] job_id_len The length of the job ID
 * @param[in] initial_version The initial expected version
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t ota_status_set_initial_expected_version(const char *job_id, size_t job_id_len, int32_t initial_version);

/**
 * @brief Clear the job entries for a job
 *
 * @param[in] job_id The job ID to clear
 * @param[in] job_id_len The length of the job ID
 */
void ota_status_clear_job_entries(const char *job_id, size_t job_id_len);

/**
 * @brief Check if the OTA status cache is empty
 *
 * @return true if the OTA status cache is empty, false otherwise
 */
bool ota_status_is_cache_empty(void);

/**
 * @brief Send an OTA status to the AWS IoT Jobs API
 * This function will add the status to the cache and attempt to send it to the AWS IoT Jobs API.
 * - Terminal statuses: It will keep retrying until the status is successfully acknowledged by ota_status_on_update_response().
 * - Non-terminal statuses: It will only send the status once, and return the error code for the first attempt.
 *
 * @param[in] status The OTA status to send
 * @param[out] p_next_version Receives the version to use for the next update. Can be NULL if not needed.
 * @return ESP_RMAKER_OK on success (or successful retry task scheduling), otherwise error code.
 */
esp_rmaker_error_t ota_status_send(const ota_status_update_t *status, int32_t *p_next_version);

/**
 * @brief Handle an update response event
 *
 * @param[in] payload The payload of the update response event
 * @param[in] payload_len The length of the payload
 * @param[in] accepted Whether the update was accepted or rejected
 * @param[out] p_return The return values for the OTA status update response. Can be NULL if not needed.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t ota_status_on_update_response(const char *payload, size_t payload_len, bool accepted, ota_status_update_response_return_t *p_return);

/**
 * @brief Re-arm an immediate retry for every job with a cached terminal status.
 *
 * Call this after the jobs topics are (re-)subscribed, e.g. on SUBACK following
 * a reconnect. A terminal update published during the re-subscribe gap can have
 * its accepted/rejected response dropped as an unsolicited publish, leaving the
 * status manager waiting. This resets the backoff and fires the retry now so the
 * update is republished while the subscription is live and the response is
 * delivered. Safe no-op when there are no cached terminal statuses.
 */
void ota_status_resend_pending_terminals(void);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_STATUS_H__ */
