/*
 * SPDX-FileCopyrightText: 2020-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_ota.h
 * @brief Public header for the OTA functionality.
 */

#ifndef __ESP_RMAKER_OTA_H__
#define __ESP_RMAKER_OTA_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_rmaker_error_types.h"
#include "sdkconfig.h"
#include "esp_rmaker_ota_event_loop.h"
#include "esp_rmaker_ota_filetype_handler.h"
#include "esp_rmaker_ota_error_reasons.h"

/** OTA Status to be reported to ESP RainMaker Neo Cloud */
typedef enum {
    /** OTA is in Progress. This can be reported multiple times as the OTA progresses. */
    OTA_STATUS_IN_PROGRESS = 1,
    /** OTA Succeeded. This should be reported only once, at the end of OTA. */
    OTA_STATUS_SUCCESS,
    /** OTA Failed. This should be reported only once, at the end of OTA. */
    OTA_STATUS_FAILED,
    /** OTA was delayed by the application */
    OTA_STATUS_DELAYED,
    /** OTA rejected due to some reason (wrong project, version, etc.) */
    OTA_STATUS_REJECTED,
} ota_status_t;

/** The OTA Handle to be used by the OTA callback */
typedef void *esp_rmaker_ota_handle_t;

/** OTA Data */
typedef struct {
    /** The Stream ID received from job doc */
    char *stream_id;
    /** Size of the OTA File */
    int filesize;
    /** The file ID within the stream, as declared by the job doc. */
    uint32_t file_id;
    /** The firmware version of the OTA image **/
    char *fw_version;
    /** The MD5 of the OTA File (lowercase hex string). NULL if the job did not declare one.
     * When present, enables OTA auto-resume and an end-to-end MD5 integrity check on completion. */
    char *file_md5;
    /** The signature of the OTA File. Base64 encoded. */
    char *file_signature;
    /** The OTA Job ID received from job doc **/
    char *ota_job_id;
    /** The private data passed as esp_rmaker_ota_config_t::priv to esp_rmaker_ota_enable() */
    char *priv;
    /** OTA Metadata, taken from the optional "metadata" field of the job document. NULL if the
     * job did not declare one. */
    char *metadata;
} esp_rmaker_ota_data_t;

/**
 * @brief Function prototype for OTA Callback
 *
 * This function will be invoked by the ESP RainMaker Neo core whenever an OTA is available.
 * The esp_rmaker_ota_report_status() API should be used to indicate the progress and
 * success/fail status.
 *
 * @param[in] handle An OTA handle assigned by the ESP RainMaker Neo Core
 * @param[in] ota_data The data to be used for the OTA
 * @param[in] ft_handler The filetype handler to be used for the OTA
 *
 * @return ESP_RMAKER_OK if the OTA was successful
 * @return ESP_RMAKER_FAIL if the OTA failed.
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_cb_t) (esp_rmaker_ota_handle_t handle,
        esp_rmaker_ota_data_t *ota_data,
        const esp_rmaker_ota_ft_ctx_t *ft_handler);

/**
 * @brief Function prototype for validating the OTA image reference (MQTT stream id).
 *
 * Invoked by the OTA engine after parsing the job document but before any image download
 * is attempted. Must return ESP_RMAKER_OK to accept the reference; any non-OK return
 * causes the job to be REJECTED with reason "Image reference failed".
 *
 * The MQTT stream id must fit within the AWS MQTT file-downloader's fixed-size topic
 * buffer, so the transport ships its own validator alongside its OTA callback.
 *
 * @param[in] image_ref     The image reference (NUL-terminated string).
 * @param[in] image_ref_len Length of @p image_ref (excluding terminator).
 *
 * @return ESP_RMAKER_OK if the reference is acceptable for download.
 * @return Non-OK error code to reject the job.
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_validate_image_ref_t)(const char *image_ref,
        size_t image_ref_len);

#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
/**
 * @brief Function prototype for Custom Job Callback
 *
 * This callback is invoked whenever the OTA engine does not recognize the job document format.
 * - If the job document has the correct keys but invalid values, it is still treated as a default OTA job document, but is rejected automatically.
 * - You should therefore avoid using any keys that are part of the default OTA job document in your custom job callback.
 *
 * The entire job document is passed to the callback.
 * You should report the status of the custom job using esp_rmaker_ota_report_custom_job_status().
 * - Or if the job document cannot be processed, then return the appropriate error code to trigger automatic status reports.
 *
 * @note You should keep this callback implementation minimal and avoid doing work within this callback.
 * @note The job document will not persist after the callback returns. Save any required data from the job document.
 * @note Custom job execution will not post any events to the event loop.
 *
 * @param[in] job_doc The job document to be used for the custom job
 * @param[in] job_doc_len The length of the job document
 *
 * @return ESP_RMAKER_OK if the job document is recognized and processed. The OTA engine will proceed with the custom job execution.
 * @return ESP_RMAKER_INVALID_ARG if the job document is invalid. A Rejected status will be reported automatically.
 * @return any other error code if the job document cannot be processed. A Failed status will be reported automatically.
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_custom_job_cb_t)(const char *job_doc, size_t job_doc_len);
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

/** Outcome reported by the OTA diagnostics (rollback check) callback */
typedef enum {
    /** OTA Diagnostics Failed. Rollback the firmware. */
    OTA_DIAG_STATUS_FAIL,
    /** OTA Diagnostics Pending. Additional validations will be done later. */
    OTA_DIAG_STATUS_PENDING,
    /** OTA Diagnostics Succeeded. Firmware can be considered valid. */
    OTA_DIAG_STATUS_SUCCESS
} esp_rmaker_ota_diag_status_t;

/** Point in the boot sequence at which the OTA diagnostics callback is invoked */
typedef enum {
    /** OTA State: Initialised. */
    OTA_DIAG_STATE_INIT,
    /** OTA state: MQTT has connected. */
    OTA_DIAG_STATE_POST_MQTT
} esp_rmaker_ota_diag_state_t;

/** Context passed to the OTA diagnostics (rollback check) callback */
typedef struct {
    /** OTA diagnostic state */
    esp_rmaker_ota_diag_state_t state;
    /** Flag to indicate whether the OTA which has triggered the Diagnostics checks for rollback
     * was triggered via RainMaker Neo or not. This would be useful only when your application has some
     * other mechanism for OTA too.
     */
    bool rmaker_ota;
} esp_rmaker_ota_diag_priv_t;

/**
 * @brief Function Prototype for Post OTA Diagnostics
 *
 * If the Application rollback feature is enabled, this callback will be invoked
 * as soon as you call esp_rmaker_ota_enable(), if it is the first
 * boot after an OTA. You may perform some application specific diagnostics and
 * report the status which will decide whether to roll back or not.
 *
 * This will be invoked once again after MQTT has connected, in case some additional validations
 * are to be done later.
 *
 * If OTA state == OTA_DIAG_STATE_INIT, then
 *      return OTA_DIAG_STATUS_FAIL to indicate failure and rollback.
 *      return OTA_DIAG_STATUS_SUCCESS or OTA_DIAG_STATUS_PENDING to tell internal OTA logic to continue further.
 *
 * If OTA state == OTA_DIAG_STATE_POST_MQTT, then
 *      return OTA_DIAG_STATUS_FAIL to indicate failure and rollback.
 *      return OTA_DIAG_STATUS_SUCCESS to indicate validation was successful and mark OTA as valid
 *      return OTA_DIAG_STATUS_PENDING to indicate that some additional validations will be done later
 *      and the OTA will eventually be marked valid/invalid using esp_rmaker_ota_mark_valid() or
 *      esp_rmaker_ota_mark_invalid() respectively.
 *
 * @param[in] ota_diag_priv Diagnostics context: the current state and whether the OTA was
 *                          triggered by RainMaker Neo.
 * @param[in] priv The private data passed as esp_rmaker_ota_config_t::priv to
 *                 esp_rmaker_ota_enable().
 *
 * @return esp_rmaker_ota_diag_status_t as applicable
 */
typedef esp_rmaker_ota_diag_status_t (*esp_rmaker_post_ota_diag_t)(esp_rmaker_ota_diag_priv_t *ota_diag_priv, void *priv);

/** ESP RainMaker Neo OTA Configuration */
typedef struct {
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
    /** Custom Job Callback.
     * The callback to be invoked when a custom job is available.
     * If kept NULL, the custom job execution will not be enabled.
     */
    esp_rmaker_ota_custom_job_cb_t custom_job_cb;
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */
    /** Custom OTA Filetype handler lookup function.
     * The function to be used to lookup the OTA Filetype handler context for a custom filetype.
     * If kept NULL, custom filetype handling will not be enabled.
     */
    esp_rmaker_ota_ft_lookup_handler_t custom_filetype_handler_lookup;
    /** OTA Callback.
     * The callback to be invoked when an OTA Job is available.
     * If kept NULL, the internal default callback will be used (Recommended).
     */
    esp_rmaker_ota_cb_t ota_cb;
    /** Optional Image-Reference Validator.
     * Validates the image reference (MQTT stream id) parsed from the OTA job
     * document before any download is started. Return ESP_RMAKER_OK to accept; any
     * non-OK return causes the job to be REJECTED with reason "Image reference failed".
     * If NULL, no validation is performed. When ota_cb is left NULL and the SDK selects
     * the built-in transport callback, it also installs the matching built-in validator.
     */
    esp_rmaker_ota_validate_image_ref_t validate_image_ref;
    /** OTA Diagnostics Callback.
     * A post OTA diagnostic handler to be invoked if app rollback feature is enabled.
     * If kept NULL, the new firmware will be assumed to be fine,
     * and no rollback will be performed.
     */
    esp_rmaker_post_ota_diag_t ota_diag;
    /** Private Data.
     * Optional private data to be passed to the OTA callback.
     */
    void *priv;
} esp_rmaker_ota_config_t;

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_RMNG_OTA_TRANSPORT_MQTT
/**
 * @brief MQTT OTA Callback
 *
 * This callback forces the use of MQTT protocol for OTA updates.
 * Use this in your ota_config.ota_cb to always use MQTT.
 *
 * @param[in] handle An OTA handle assigned by the ESP RainMaker Neo Core
 * @param[in] ota_data The data to be used for the OTA
 * @param[in] ft_handler The filetype handler to be used for the OTA
 *
 * @return ESP_RMAKER_OK if the OTA was successful
 * @return ESP_RMAKER_FAIL if the OTA failed
 */
esp_rmaker_error_t esp_rmaker_ota_mqtt_cb(esp_rmaker_ota_handle_t handle, esp_rmaker_ota_data_t *ota_data, const esp_rmaker_ota_ft_ctx_t *ft_handler);

/**
 * @brief MQTT Image-Reference Validator
 *
 * Built-in validator paired with esp_rmaker_ota_mqtt_cb. Rejects stream identifiers
 * longer than the AWS MQTT file-downloader's STREAM_NAME_MAX_LEN, which would otherwise
 * overflow a fixed-size stack buffer inside the downloader library.
 *
 * @param[in] image_ref The stream name from the job document.
 * @param[in] image_ref_len Length of image_ref in bytes, excluding any null terminator.
 *
 * @return ESP_RMAKER_OK if the stream name is acceptable.
 * @return An error code to reject the job.
 */
esp_rmaker_error_t esp_rmaker_ota_mqtt_validate_image_ref(const char *image_ref, size_t image_ref_len);
#endif

/**
 * @brief Enable OTA
 *
 * Calling this API enables OTA as per the ESP RainMaker Neo specification.
 * Please check the various ESP RainMaker Neo configuration options to
 * use the different variants of OTA. Refer the documentation for
 * additional details.
 *
 * The post-OTA rollback check (esp_rmaker_ota_config_t::ota_diag) runs synchronously inside this
 * call. The rest of the setup is queued on the work queue and completes once esp_rmaker_start()
 * has run it.
 *
 * @param[in] ota_config Pointer to an OTA configuration structure. The structure is copied, so it
 *                       need not outlive the call; the pointed-to `priv` data must.
 *
 * @return ESP_RMAKER_OK on success, or if OTA is already enabled or pending.
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_ota_enable(const esp_rmaker_ota_config_t *ota_config);

/**
 * @brief Disable OTA
 *
 * This API disables OTA and removes all OTA related resources.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_ota_disable(void);

#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
/**
 * @brief Report Custom Job Status
 *
 * This API must be called from the Custom Job Callback to indicate the status of the custom job.
 * Only the following statuses are allowed, with the following translations to AWS IoT Job events:
 * - OTA_STATUS_IN_PROGRESS -> InProgress
 * - OTA_STATUS_SUCCESS -> Succeeded
 * - OTA_STATUS_FAILED -> Failed
 * - OTA_STATUS_REJECTED -> Rejected
 * The status details should be a JSON object.
 *
 * @param[in] status Status to be reported
 * @param[in] status_details_json Status details to be reported
 * @param[in] status_details_json_len The length of the status details JSON string
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_ota_report_custom_job_status(ota_status_t status, const char *status_details_json, size_t status_details_json_len);
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

/**
 * @brief Report OTA Status
 *
 * This API must be called from the OTA Callback to indicate the status of the OTA. The OTA_STATUS_IN_PROGRESS
 * can be reported multiple times with appropriate status details. The final success/failure should
 * be reported only once, at the end.
 *
 * This can be ignored if you are using the default internal OTA callback.
 *
 * @param[in] ota_handle The OTA handle received by the callback
 * @param[in] status Status to be reported
 * @param[in] status_details Status details to be reported
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_ota_report_status(esp_rmaker_ota_handle_t ota_handle, ota_status_t status, const esp_rmaker_ota_status_details_t *status_details);

/**
 * @brief Default OTA callback
 *
 * This is the default OTA callback which will get used if you do not pass your own callback. You can call this
 * even from your callback, in case you want better control on when the OTA can proceed and yet let the actual
 * OTA process be managed by the RainMaker Neo Core.
 *
 * @param[in] handle An OTA handle assigned by the ESP RainMaker Neo Core
 * @param[in] ota_data The data to be used for the OTA
 * @param[in] ft_handler The filetype handler to be used for the OTA
 *
 * @return ESP_RMAKER_OK if the OTA was successful
 * @return ESP_RMAKER_FAIL if the OTA failed.
 */
esp_rmaker_error_t esp_rmaker_ota_default_cb(esp_rmaker_ota_handle_t handle, esp_rmaker_ota_data_t *ota_data, const esp_rmaker_ota_ft_ctx_t *ft_handler);

/**
 * @brief Default Image-Reference Validator
 *
 * Paired with esp_rmaker_ota_default_cb. Dispatches to the MQTT transport validator.
 *
 * @param[in] image_ref The stream name from the job document.
 * @param[in] image_ref_len Length of image_ref in bytes, excluding any null terminator.
 *
 * @return ESP_RMAKER_OK if the reference is valid for the default transport.
 * @return An error code otherwise.
 */
esp_rmaker_error_t esp_rmaker_ota_default_validate_image_ref(const char *image_ref, size_t image_ref_len);

/**
 * @brief Fetch OTA Info
 *
 * This API can be used to explicitly ask the backend if an OTA is available.
 * If it is, then the OTA callback would get invoked.
 *
 * @return ESP_RMAKER_OK if the OTA fetch request was posted to the state machine.
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_ota_fetch(void);

/**
 * @brief Fetch OTA Info with a delay
 *
 * Same as esp_rmaker_ota_fetch(), but the request is issued after the given delay. Calling this
 * again before the timer fires restarts the delay rather than queueing a second fetch.
 *
 * @param[in] time Delay (in seconds). Must be greater than 0.
 *
 * @return ESP_RMAKER_OK if the OTA fetch timer was created.
 * @return ESP_RMAKER_INVALID_ARG if time is not greater than 0.
 * @return ESP_RMAKER_INVALID_STATE if OTA is not enabled.
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_ota_fetch_with_delay(int time);

/**
 * @brief Request Recovery
 *
 * This API can be used to request a recovery from the current error state.
 * Use this after receiving the RMAKER_OTA_EVENT_ERROR_OCCURRED event to request recovery (see esp_rmaker_ota_event_loop.h)
 *
 * @note This does not fix underlying issues, but resumes the OTA process from the indicated recovery state.
 * @note Calling this directly after receiving the RMAKER_OTA_EVENT_ERROR_OCCURRED event might likely cause an infinite error loop.
 * @note You are responsible for ensuring that the underlying issue is fixed before requesting recovery.
 *
 * @return ESP_RMAKER_OK if the recovery request was posted to the state machine.
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_ota_request_recovery(void);

/**
 * @brief Mark OTA as valid
 *
 * This should be called if the OTA validation has been kept pending by returning OTA_DIAG_STATUS_PENDING
 * in the ota_diag callback and then, the validation was eventually successful. This can also be used to mark
 * the OTA valid even before RainMaker Neo core does its own validations (primarily MQTT connection).
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_ota_mark_valid(void);

/**
 * @brief Mark OTA as invalid
 *
 * This should be called if the OTA validation has been kept pending by returning OTA_DIAG_STATUS_PENDING
 * in the ota_diag callback and then, the validation eventually failed. This can even be used to rollback
 * at any point of time before RainMaker Neo core's internal logic and the application's logic mark the OTA
 * as valid.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_ota_mark_invalid(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_OTA_H__ */
