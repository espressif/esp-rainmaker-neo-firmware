/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_jobs.h
 * @brief Private header for OTA Jobs state machine
 */

#ifndef __OTA_JOBS_H__
#define __OTA_JOBS_H__

#include <time.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_rmaker_error_types.h"
#include "jobs.h"
#include "job_parser.h"
#include "esp_rmaker_ota.h"
#include "ota_timeout_handler.h"
#include "esp_rmaker_ota_status_details.h"
#include "esp_rmaker_ota_filetype_handler.h"
#include "sdkconfig.h"

/**
 * @brief Timeout values (in milliseconds)
 */
#define OTA_RESPONSE_TIMEOUT_MS 10000  // 10 seconds

/**
 * @brief Filetype max length
 */
#define OTA_FILETYPE_MAX_LENGTH 32

/**
 * @brief OTA Job state machine states
 */
typedef enum {
    OTA_JOB_STATE_UNINITIALIZED = 0,
    OTA_JOB_STATE_NETWORK_INIT,
    OTA_JOB_STATE_REBOOT_CHECK,
    OTA_JOB_STATE_IDLE,
    OTA_JOB_STATE_JOBS_CHANGED,
    OTA_JOB_STATE_FETCHING_PENDING_JOBS,
    OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS,
    OTA_JOB_STATE_PENDING_JOBS_RECEIVED,
    OTA_JOB_STATE_FETCHING_JOB_DOC,
    OTA_JOB_STATE_WAITING_FOR_JOB_DOC,
    OTA_JOB_STATE_JOB_DOC_RECEIVED,
    OTA_JOB_STATE_JOB_EXECUTION,
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
    OTA_JOB_STATE_CUSTOM_JOB_EXECUTION,
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */
    OTA_JOB_STATE_POST_DOWNLOAD,
    OTA_JOB_STATE_ERROR
} ota_job_state_t;

/**
 * @brief OTA Job events
 */
typedef enum {
    OTA_JOB_EVENT_EMPTY_TRANSITION = -2,
    OTA_JOB_EVENT_ERROR_OCCURRED = -1,
    OTA_JOB_EVENT_REBOOT_CHECK_REQUESTED = 0,
    OTA_JOB_EVENT_FINAL_STATUS_REPORT_REQUESTED,
    OTA_JOB_EVENT_FETCH_REQUESTED,
    OTA_JOB_EVENT_JOBS_CHANGED,
    OTA_JOB_EVENT_PENDING_JOBS_ACCEPTED,
    OTA_JOB_EVENT_PENDING_JOBS_REJECTED,
    OTA_JOB_EVENT_JOB_DOC_ACCEPTED,
    OTA_JOB_EVENT_JOB_DOC_REJECTED,
    OTA_JOB_EVENT_UPDATE_ACCEPTED,
    OTA_JOB_EVENT_UPDATE_REJECTED,
    OTA_JOB_EVENT_TIMEOUT,
    OTA_JOB_EVENT_IMAGE_DOWNLOAD_PROGRESS,
    OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED,
    OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_SETUP,
    OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_STREAM_SUBSCRIPTION,
    OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_POST_DOWNLOAD_CHECKS,
    OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_IMAGE_HEADER_INVALID,
    OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_SIGNATURE_INVALID,
    OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_MD5_INVALID,
    OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_UNKNOWN_ERROR,
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
    OTA_JOB_EVENT_CUSTOM_JOB_EXECUTION_REQUESTED,
    OTA_JOB_EVENT_CUSTOM_JOB_PROGRESS,
    OTA_JOB_EVENT_CUSTOM_JOB_SUCCEEDED,
    OTA_JOB_EVENT_CUSTOM_JOB_FAILED,
    OTA_JOB_EVENT_CUSTOM_JOB_REJECTED,
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */
    OTA_JOB_EVENT_RECOVERY_REQUESTED,
} ota_job_event_t;

typedef struct {
    /** Image download configuration */
    struct {
        /** Image download callback */
        esp_rmaker_ota_cb_t ota_cb;
        /** Optional image-reference validator paired with @ref ota_cb. May be NULL. */
        esp_rmaker_ota_validate_image_ref_t validate_image_ref;
        /** Private data for image download callback */
        void *priv;
    } image_download;
    /** Filetype handler lookup function */
    esp_rmaker_ota_ft_lookup_handler_t filetype_handler_lookup;
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
    /** Custom job callback */
    esp_rmaker_ota_custom_job_cb_t custom_job_cb;
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */
} ota_job_config_t;

#if CONFIG_RMNG_OTA_TIME_SUPPORT
/**
 * @brief OTA job download window structure
 */
typedef struct {
    /** Daily window within the validity period. All times in minutes since midnight. Negative time means not enforced. */
    struct {
        int16_t start;
        int16_t end;
    } daily;
    /** Validity period. All times in seconds since epoch. 0 means not enforced. */
    struct {
        time_t start;
        time_t end;
    } validity;
} ota_job_download_window_t;
#endif /* CONFIG_RMNG_OTA_TIME_SUPPORT */

/**
 * @brief Job information structure
 */
typedef struct {
    char job_id[JOBID_MAX_LENGTH + 1];
    bool has_pending_job;
    bool has_active_job;
    bool should_reboot;
    bool final_status_reported;
    bool final_status_queued;
    esp_rmaker_ota_data_t ota_data;
    char filetype[OTA_FILETYPE_MAX_LENGTH + 1];
    const esp_rmaker_ota_ft_ctx_t *filetype_handler;
    uint32_t fw_version;
    int32_t expected_version;
    JobCurrentStatus_t current_status;
    esp_rmaker_ota_status_details_t current_status_details;
} ota_job_info_t;

/**
 * @brief OTA Job custom fields structure. These are fields alongside the standard AWS IoT Job fields.
 */
typedef struct {
    /** The firmware version of the OTA image **/
    const char *fw_version;
    size_t fw_version_len;
    /** Minimum firmware version required for the OTA image **/
    const char *min_fw_version; // Will be NULL if not specified
    size_t min_fw_version_len;
    /** MD5 of the OTA image (hex string). Will be NULL if not specified.
     * Enables OTA auto-resume and the end-to-end MD5 integrity check. */
    const char *file_md5;
    size_t file_md5_len;
    /** Filetype of the OTA image **/
    const char *filetype;
    size_t filetype_len;
    /** Metadata of the OTA image **/
    const char *metadata;
    size_t metadata_len;
#if CONFIG_RMNG_OTA_TIME_SUPPORT
    /** Download window for the OTA image */
    ota_job_download_window_t download_window;
#endif /* CONFIG_RMNG_OTA_TIME_SUPPORT */
} ota_job_info_custom_fields_t;

/**
 * @brief Event data payload for job events
 */
typedef struct {
    void *data;
    size_t len;
} ota_job_event_data_payload_t;

/**
 * @brief Event data for job events
 */
typedef struct {
    ota_job_event_t event;
    ota_job_event_data_payload_t *payload;
    /* Small inline scalar carried by value with the event. Unlike @ref payload it is
     * never heap-allocated or dereferenced by the event machinery, so an event using
     * only fixed_data stays payload-free and can be copied from the static event pool
     * without any heap allocation (surviving heap exhaustion). */
    void *fixed_data;
} ota_job_event_data_t;

/**
 * @brief OTA Job state machine context
 */
typedef struct {
    /** Image download configuration */
    struct {
        esp_rmaker_ota_cb_t ota_cb;
        esp_rmaker_ota_validate_image_ref_t validate_image_ref;
        void *priv;
    } image_download;
    /** Filetype handler lookup function */
    esp_rmaker_ota_ft_lookup_handler_t filetype_handler_lookup;
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
    /** Custom job callback */
    esp_rmaker_ota_custom_job_cb_t custom_job_cb;
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

    char thing_name[THINGNAME_MAX_LENGTH + 1];
    uint16_t thing_name_length;
    ota_job_state_t state;
    ota_job_info_t current_job;
    /** True once a SUBACK has confirmed the jobs subscription. */
    bool subscribed;
    /** True between subscribe-intent and explicit unsubscribe. Governs
     *  reconnect-driven re-subscribe behaviour. */
    bool sub_intended;
    rmaker_ota_timeout_handler_handle_t timeout_handler_handle;
    esp_rmaker_ota_error_reason_t last_error;

    /**
     * @brief Recovery information
     * event_data is used first if not NULL, otherwise event with NULL payload is used.
     */
    struct {
        ota_job_state_t state;
        ota_job_event_data_t *event_data;
        ota_job_event_t event;
    } recovery;
} ota_job_state_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize critical components for OTA Jobs state machine.
 * This sets up the following critical components:
 * - Pre-init event queue: Enqueue events that should happen the moment the state machine is initialized.
 *
 * @note Call this as early as possible.
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
esp_rmaker_error_t ota_job_critical_init(void);

/**
 * @brief Initialize OTA Jobs state machine
 *
 * @param[in] ota_job_config The OTA job state machine configuration
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
esp_rmaker_error_t ota_job_state_init(const ota_job_config_t *ota_job_config);

/**
 * @brief Deinitialize OTA Jobs state machine
 *
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
esp_rmaker_error_t ota_job_state_deinit(void);

/**
 * @brief Fetch OTA jobs immediately.
 * This performs state checks before attempting to fetch OTA jobs.
 * Use this instead of posting the OTA_JOB_EVENT_FETCH_REQUESTED event directly to avoid flooding the work queue.
 *
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
esp_rmaker_error_t ota_job_state_fetch(void);

/**
 * @brief Trigger a reboot check event to the state machine.
 * If the state machine is not yet initialized, this will set a flag to trigger the reboot check event when the state machine is initialized.
 * Else it will post the event to the state machine immediately.
 *
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
esp_rmaker_error_t ota_job_state_reboot_check(void);

/**
 * @brief Recover from an error state.
 * This will transition the state machine to the recovery state and post the recovery event to the state machine.
 *
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 * @return ESP_RMAKER_INVALID_STATE if the recovery state is not valid (e.g., no state or event to recover to)
 */
esp_rmaker_error_t ota_job_state_recover(void);

/**
 * @brief Post event to OTA Jobs state machine
 * @note Use ota_job_state_fetch instead of this function to fetch OTA jobs immediately.
 *
 * @param[in] event_data The event data to post
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
esp_rmaker_error_t ota_job_state_post_event(const ota_job_event_data_t *event_data);

/**
 * @brief Post a terminal download->FSM event, guaranteeing eventual delivery.
 *
 * Unlike @ref ota_job_state_post_event, if the initial post fails (e.g. a transient
 * work-queue enqueue failure under memory pressure) the event is stashed and
 * re-delivered on the shared backoff scheduler until it succeeds. This prevents a
 * dropped terminal event (IMAGE_DOWNLOAD_SUCCEEDED / *_FAILED_* / TIMEOUT) from
 * stranding the state machine in JOB_EXECUTION.
 *
 * @note Only for payload-free terminal events; @ref ota_job_event_data_t.payload is
 *       ignored (treated as NULL) because the stashed copy is heap-free by design.
 *       Reboot and other scalar signalling must use @ref ota_job_event_data_t.fixed_data.
 *
 * @param[in] event_data The terminal event to post (payload is ignored)
 * @return ESP_RMAKER_OK if posted directly or a backoff re-delivery was scheduled
 */
esp_rmaker_error_t ota_job_state_post_terminal_event(const ota_job_event_data_t *event_data);

/**
 * @brief State machine run once function (called from work queue)
 *
 * @param[in] priv_data Private data (state machine context)
 */
void ota_job_state_run_once(void *priv_data);

/**
 * @brief Get current state machine context
 *
 * @return ota_job_state_ctx_t* Pointer to state machine context
 */
ota_job_state_ctx_t *ota_job_state_get_context(void);

/**
 * @brief Publish update job status to AWS IoT Jobs to the current job.
 *
 * @param[in] status The status to publish
 * @param[in] status_details The status details to publish. Can be NULL if no status details are available.
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
esp_rmaker_error_t ota_jobs_mqtt_publish_update_job_status(JobCurrentStatus_t status, const esp_rmaker_ota_status_details_t *status_details);

/**
 * @brief Publish update job status to AWS IoT Jobs to the current job.
 * This function accepts a JSON string representation of the status details. Must be a valid JSON string.
 *
 * @param[in] status The status to publish
 * @param[in] status_details_json The status details to publish. Can be NULL if no status details are available.
 * @param[in] status_details_json_len The length of the status details JSON string.
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 * @return ESP_RMAKER_INVALID_ARG if the status details JSON is not a valid JSON string
 */
esp_rmaker_error_t ota_jobs_mqtt_publish_update_job_status_with_string(JobCurrentStatus_t status, const char *status_details_json, size_t status_details_json_len);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_JOBS_H__ */
