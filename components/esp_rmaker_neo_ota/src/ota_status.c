/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_status.c
 * @brief OTA status management
 */

/* Includes **********************************************************************/

/* Declarations */
#include "ota_status.h"

/* Standard includes */
#include <inttypes.h>
#include <string.h>

/* Platform common includes */
#include "osal_semaphore.h"
#include "osal_scheduler.h"
#include "osal_mem_alloc.h"
#include "osal_log.h"

/* RMNG includes */
#include "esp_rmaker_work_queue.h"
#include "esp_rmaker_mqtt_impl.h"

/* Core JSON includes */
#include "core_json.h"

/* MQTT channels includes */
#include "network/ota_mqtt_channels.h"

/* Types ************************************************************************/

/**
 * @brief OTA status collection of entries for a job
 */
typedef struct _ota_status_job_entries_t {
    char job_id[JOBID_MAX_LENGTH + 1];
    size_t job_id_len;
    uint32_t job_id_int;
    int32_t version;
    bool has_cached_terminal;
    JobCurrentStatus_t cached_status;
    char *cached_status_details_str;
    size_t cached_status_details_str_len;
    int32_t cached_expected_version;
    uint64_t retry_delay_ms;
    osal_scheduler_task_handle_t retry_handle;
    struct _ota_status_job_entries_t *next;
} ota_status_job_entries_t;

/**
 * @brief OTA status cache
 */
typedef struct {
    struct {
        ota_status_job_entries_t *head;
        ota_status_job_entries_t *tail;
        uint32_t next_job_id_int;
        size_t count;
    } entries;
    osal_semaphore_handle_t mutex;
} ota_status_cache_t;

/* Constants **********************************************************************/

/**
 * @brief Tag for logging
 */
static const char *TAG = "rmng_ota_status";

/**
 * @brief Initial retry delay in milliseconds
 */
#define OTA_STATUS_RETRY_INITIAL_DELAY_MS (1000)

/**
 * @brief Maximum retry delay in milliseconds
 */
#define OTA_STATUS_RETRY_MAX_DELAY_MS     (60 * 1000)

/**
 * @brief Client token preamble
 */
#define CLIENTTOKEN_PREAMBLE "{\"clientToken\":"

/* Variables **********************************************************************/

/**
 * @brief Pointer to the OTA status cache
 */
static ota_status_cache_t *p_ota_status_cache = NULL;

/**
 * @brief Thing name
 */
static const char *g_thing_name = NULL;
/**
 * @brief Length of the thing name
 */
static size_t g_thing_name_length = 0;

#if defined(RMAKER_OTA_JOBS_TEST_WRAP_LINKER) || defined(RMAKER_OTA_JOBS_TEST_WRAP_DL_LIB)
/* Test instrumentation. No effect unless a test opts in via the accessors below.
 * When intercept is enabled, retry (re)scheduling is counted and the handle is
 * marked armed, but no real timer is started - so unit tests can drive the retry
 * task synchronously and assert the response-watchdog re-arm deterministically,
 * regardless of whether the scheduler is wrapped (Linux) or real (macOS). */
static bool g_test_intercept_scheduling = false;
static int g_test_retry_schedule_count = 0;
#endif

/* Private function declarations **************************************************/

/**
 * @brief Helper macro to get the length of a constant string
 *
 * @param[in] s The constant string
 * @return The length of the string
 */
#define CONST_STRLEN(s) (sizeof(s) - 1)

/**
 * @brief Initialize the OTA status cache
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t ota_status_cache_init(void);

/**
 * @brief Deinitialize the OTA status cache
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t ota_status_cache_deinit(void);

/**
 * @brief Lock the OTA status cache
 */
static void ota_status_cache_lock(void);

/**
 * @brief Unlock the OTA status cache
 */
static void ota_status_cache_unlock(void);

/**
 * @brief Get or create job entries in the OTA status cache (cache must be locked)
 *
 * @param[in] job_id The job ID to lookup
 * @param[in] job_id_len The length of the job ID
 * @param[out] p_job_entries Pointer to store the job entries
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t ota_status_cache_get_or_create_job_entries_locked(const char *job_id, size_t job_id_len,
        ota_status_job_entries_t **p_job_entries);

/**
 * @brief Find job entries using the job ID integer (cache must be locked)
 *
 * @param[in] job_id_int The job ID integer to lookup
 * @return Pointer to job entries, or NULL if not found
 */
static ota_status_job_entries_t *ota_status_cache_find_job_entries_by_int_locked(uint32_t job_id_int);

/**
 * @brief Update the cached terminal status for a job (cache must be locked)
 *
 * @param[in,out] job_entries The job entries to update
 * @param[in] status The OTA status to cache
 * @param[in] expected_version The expected version used for this status
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t ota_status_cache_set_cached_terminal_locked(ota_status_job_entries_t *job_entries,
        const ota_status_update_t *status,
        int32_t expected_version);

/**
 * @brief Schedule or reset the retry timer for a job
 *
 * @param[in] job_entries The job entries to schedule for
 * @param[in] delay_ms Delay in milliseconds
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t ota_status_schedule_or_reset_retry(ota_status_job_entries_t *job_entries, uint64_t delay_ms);

/**
 * @brief Remove a job entry from the cache (cache must be locked)
 *
 * @param[in] job_entries The job entries to remove
 */
static void ota_status_cache_remove_job_entries_locked(ota_status_job_entries_t *job_entries);

/**
 * @brief Check whether an UpdateJobExecution reject code can ever be recovered from
 *
 * @param code The "code" field of the rejected payload (not NUL-terminated)
 * @param code_len The length of @a code
 * @return true if retrying the update can never succeed, false if a retry may help.
 */
static bool ota_status_reject_is_unrecoverable(const char *code, size_t code_len);

/**
 * @brief Overwrite the cached version for a job ID
 *
 * @param[in] job_id The job ID to update
 * @param[in] job_id_len The length of the job ID
 * @param[in] version The version to set
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t ota_status_cache_overwrite_version(const char *job_id, size_t job_id_len, int32_t version);

/**
 * @brief Convert a string to a uint32_t
 *
 * @param[in] str The string to convert
 * @param[in] str_len The length of the string
 * @param[out] p_value The value to store the result in
 * @return true if the conversion was successful, false otherwise
 */
static bool __str_to_uint32(const char *str, size_t str_len, uint32_t *p_value);

/**
 * @brief Get the client token details from a payload
 *
 * @param[in] payload The payload to use for the client token
 * @param[in] payload_len The length of the payload
 * @param[out] p_is_terminal Whether the token indicates a terminal status
 * @param[out] p_job_id_int The job ID integer from the token
 * @return true if the token is valid, false otherwise
 */
static bool ota_status_cache_token_from_payload(const char *payload, size_t payload_len, bool *p_is_terminal,
        uint32_t *p_job_id_int);

/**
 * @brief Determine whether a status is terminal
 *
 * @param[in] status The status to check
 * @return true if terminal, false otherwise
 */
static bool ota_status_is_terminal(JobCurrentStatus_t status);

/**
 * @brief Publish a status update with an explicit expected version
 *
 * @param[in] status The OTA status to publish
 * @param[in] expected_version Expected version to use
 * @param[in] is_terminal Whether the update is terminal
 * @param[in] job_id_int Job ID integer for the client token
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t ota_status_publish_update(const ota_status_update_t *status, int32_t expected_version,
        bool is_terminal, uint32_t job_id_int);

/**
 * @brief Retry task for cached terminal updates
 *
 * @param[in] arg Pointer to the job entries
 */
static void ota_status_retry_task(void *arg);

/* Private function definitions ****************************************************/

static esp_rmaker_error_t ota_status_cache_init(void)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Allocate memory for the OTA status cache */
    p_ota_status_cache = (ota_status_cache_t *)OSAL_CALLOC_EXTRAM(1, sizeof(ota_status_cache_t));
    if (p_ota_status_cache == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for OTA status cache");
        return ESP_RMAKER_NO_MEM;
    }

    /* Create a mutex for the OTA status cache */
    p_ota_status_cache->mutex = osal_semaphore_create_mutex();
    if (p_ota_status_cache->mutex == NULL) {
        OSAL_LOGE(TAG, "Failed to create mutex for OTA status cache");
        err = ESP_RMAKER_NO_MEM;
        goto ota_status_cache_init_fail;
    }

    /* Initialize the entries */
    p_ota_status_cache->entries.head = NULL;
    p_ota_status_cache->entries.tail = NULL;
    p_ota_status_cache->entries.next_job_id_int = 1;
    p_ota_status_cache->entries.count = 0;
    return ESP_RMAKER_OK;

ota_status_cache_init_fail:
    ota_status_cache_deinit();
    return err;
}

static esp_rmaker_error_t ota_status_cache_deinit(void)
{
    if (p_ota_status_cache == NULL) {
        return ESP_RMAKER_OK;
    }

    /* Delete the mutex */
    if (p_ota_status_cache->mutex != NULL) {
        osal_semaphore_delete(p_ota_status_cache->mutex);
    }

    /* Free all job entries and their cached status */
    ota_status_job_entries_t *job_entries = p_ota_status_cache->entries.head;
    while (job_entries != NULL) {
        ota_status_job_entries_t *next_job = job_entries->next;
        if (job_entries->retry_handle != NULL) {
            osal_scheduler_stop_timer(job_entries->retry_handle);
            osal_scheduler_cancel_task(&job_entries->retry_handle);
        }
        if (job_entries->cached_status_details_str != NULL) {
            free(job_entries->cached_status_details_str);
        }
        free(job_entries);
        job_entries = next_job;
    }

    /* Free the OTA status cache */
    free(p_ota_status_cache);
    p_ota_status_cache = NULL;

    /* Return success */
    return ESP_RMAKER_OK;
}

static void ota_status_cache_lock(void)
{
    if (p_ota_status_cache == NULL || p_ota_status_cache->mutex == NULL) {
        return;
    }
    osal_semaphore_take(p_ota_status_cache->mutex, OSAL_MAX_DELAY);
}

static void ota_status_cache_unlock(void)
{
    if (p_ota_status_cache == NULL || p_ota_status_cache->mutex == NULL) {
        return;
    }
    osal_semaphore_give(p_ota_status_cache->mutex);
}

static esp_rmaker_error_t ota_status_cache_get_or_create_job_entries_locked(const char *job_id, size_t job_id_len,
        ota_status_job_entries_t **p_job_entries)
{
    if (p_job_entries == NULL || job_id == NULL || job_id_len == 0 || job_id_len > JOBID_MAX_LENGTH) {
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Find the collection of entries for the job */
    ota_status_job_entries_t *job_entries = p_ota_status_cache->entries.head;

    while (job_entries != NULL) {
        if (job_entries->job_id_len == job_id_len && strncmp(job_entries->job_id, job_id, job_id_len) == 0) {
            break;
        }
        job_entries = job_entries->next;
    }

    /* Create the collection of entries for the job if it doesn't exist */
    if (job_entries == NULL) {
        job_entries = (ota_status_job_entries_t *)OSAL_CALLOC_EXTRAM(1, sizeof(ota_status_job_entries_t));
        if (job_entries == NULL) {
            return ESP_RMAKER_NO_MEM;
        }
        memcpy(job_entries->job_id, job_id, job_id_len);
        job_entries->job_id[job_id_len] = '\0';
        job_entries->job_id_len = job_id_len;
        job_entries->job_id_int = p_ota_status_cache->entries.next_job_id_int++;
        job_entries->version = 1;
        job_entries->has_cached_terminal = false;
        job_entries->cached_status_details_str = NULL;
        job_entries->cached_status_details_str_len = 0;
        job_entries->cached_expected_version = 0;
        job_entries->retry_delay_ms = OTA_STATUS_RETRY_INITIAL_DELAY_MS;
        job_entries->retry_handle = NULL;

        if (p_ota_status_cache->entries.head == NULL) {
            p_ota_status_cache->entries.head = job_entries;
        } else {
            p_ota_status_cache->entries.tail->next = job_entries;
        }
        p_ota_status_cache->entries.tail = job_entries;
    }

    *p_job_entries = job_entries;
    return ESP_RMAKER_OK;
}

static ota_status_job_entries_t *ota_status_cache_find_job_entries_by_int_locked(uint32_t job_id_int)
{
    ota_status_job_entries_t *job_entries = p_ota_status_cache->entries.head;
    while (job_entries != NULL) {
        if (job_entries->job_id_int == job_id_int) {
            return job_entries;
        }
        job_entries = job_entries->next;
    }
    return NULL;
}

static esp_rmaker_error_t ota_status_cache_set_cached_terminal_locked(ota_status_job_entries_t *job_entries,
        const ota_status_update_t *status,
        int32_t expected_version)
{
    if (job_entries == NULL || status == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    if (job_entries->cached_status_details_str != NULL) {
        free(job_entries->cached_status_details_str);
        job_entries->cached_status_details_str = NULL;
        job_entries->cached_status_details_str_len = 0;
    }

    if (status->status_details_str != NULL && status->status_details_str_len > 0) {
        job_entries->cached_status_details_str = (char *)OSAL_MALLOC_EXTRAM(status->status_details_str_len * sizeof(char));
        if (job_entries->cached_status_details_str == NULL) {
            OSAL_LOGE(TAG, "Failed to allocate cached status details string");
            return ESP_RMAKER_NO_MEM;
        }
        memcpy(job_entries->cached_status_details_str, status->status_details_str, status->status_details_str_len);
        job_entries->cached_status_details_str_len = status->status_details_str_len;
    }

    job_entries->cached_status = status->status;
    job_entries->cached_expected_version = expected_version;

    /* Increment the count if this is the first cached terminal update for the job */
    if (!job_entries->has_cached_terminal) {
        p_ota_status_cache->entries.count++;
    }
    job_entries->has_cached_terminal = true;
    job_entries->retry_delay_ms = OTA_STATUS_RETRY_INITIAL_DELAY_MS;

    return ota_status_schedule_or_reset_retry(job_entries, job_entries->retry_delay_ms);
}

static esp_rmaker_error_t ota_status_schedule_or_reset_retry(ota_status_job_entries_t *job_entries, uint64_t delay_ms)
{
    if (job_entries == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

#if defined(RMAKER_OTA_JOBS_TEST_WRAP_LINKER) || defined(RMAKER_OTA_JOBS_TEST_WRAP_DL_LIB)
    if (g_test_intercept_scheduling) {
        /* Count the (re)schedule but start no real timer and leave retry_handle
         * NULL, so teardown's stop_timer/cancel_task are not called on a fake
         * handle. Keeps tests deterministic and crash-free. */
        g_test_retry_schedule_count++;
        return ESP_RMAKER_OK;
    }
#endif

    if (job_entries->retry_handle == NULL) {
        osal_err_t sched_err = osal_scheduler_schedule_task(&job_entries->retry_handle,
                               delay_ms,
                               ota_status_retry_task,
                               job_entries);
        if (sched_err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to schedule retry task: %d", (int)sched_err);
            return ESP_RMAKER_FAIL;
        }
    } else {
        osal_err_t sched_err = osal_scheduler_reset_timer(job_entries->retry_handle,
                               delay_ms);
        if (sched_err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to reset retry timer: %d", (int)sched_err);
            return ESP_RMAKER_FAIL;
        }
    }

    return ESP_RMAKER_OK;
}

static void ota_status_cache_remove_job_entries_locked(ota_status_job_entries_t *job_entries)
{
    if (job_entries == NULL || p_ota_status_cache == NULL) {
        return;
    }

    ota_status_job_entries_t *current = p_ota_status_cache->entries.head;
    ota_status_job_entries_t *prev = NULL;
    while (current != NULL) {
        if (current == job_entries) {
            break;
        }
        prev = current;
        current = current->next;
    }

    if (current == NULL) {
        return;
    }

    if (current->retry_handle != NULL) {
        osal_scheduler_stop_timer(current->retry_handle);
        osal_scheduler_cancel_task(&current->retry_handle);
    }

    if (current->cached_status_details_str != NULL) {
        free(current->cached_status_details_str);
        current->cached_status_details_str = NULL;
        current->cached_status_details_str_len = 0;
    }

    if (prev == NULL) {
        p_ota_status_cache->entries.head = current->next;
    } else {
        prev->next = current->next;
    }
    if (p_ota_status_cache->entries.tail == current) {
        p_ota_status_cache->entries.tail = prev;
    }
    if (current->has_cached_terminal && p_ota_status_cache->entries.count > 0) {
        p_ota_status_cache->entries.count--;
    }
    free(current);
}

static bool ota_status_is_terminal(JobCurrentStatus_t status)
{
    return (status == Failed) || (status == Succeeded) || (status == Rejected);
}

static esp_rmaker_error_t ota_status_publish_update(const ota_status_update_t *status, int32_t expected_version,
        bool is_terminal, uint32_t job_id_int)
{
    char topic_buffer[TOPIC_BUFFER_SIZE];
    size_t topic_len = 0;

    JobsStatus_t jobs_status = Jobs_Update(
                                   topic_buffer, sizeof(topic_buffer),
                                   g_thing_name,
                                   g_thing_name_length,
                                   status->job_id, status->job_id_len,
                                   &topic_len);

    if (jobs_status != JobsSuccess) {
        OSAL_LOGE(TAG, "Failed to build Update topic for job %s: %d", status->job_id, jobs_status);
        return ESP_RMAKER_FAIL;
    }

    /* Build update message */
    char expected_version_str[10];
    int expected_version_str_len = snprintf(expected_version_str, sizeof(expected_version_str), "%" PRId32, expected_version);
    if (expected_version_str_len < 0 || expected_version_str_len >= sizeof(expected_version_str)) {
        OSAL_LOGE(TAG, "Failed to format expected version string");
        return ESP_RMAKER_FAIL;
    }

    bool has_status_details = (status->status_details_str != NULL && status->status_details_str_len > 0);
    size_t base_message_len = UPDATE_JOB_MSG_LENGTH;
    if (has_status_details) {
        base_message_len += status->status_details_str_len + CONST_STRLEN( ",\"statusDetails\":" );
    }

    size_t clientToken_len = CONST_STRLEN(CLIENTTOKEN_PREAMBLE) + 1 + 10 + 2;
    char message_buffer[base_message_len + clientToken_len + 1];
    int clientToken_pos = snprintf(message_buffer, clientToken_len + 1, "%s\"%c%" PRIu32 "\"", CLIENTTOKEN_PREAMBLE, is_terminal ? '1' : '0', job_id_int);
    if (clientToken_pos < 0 || clientToken_pos >= (int)(clientToken_len + 1)) {
        OSAL_LOGE(TAG, "Failed to insert clientToken: pos=%d", clientToken_pos);
        return ESP_RMAKER_FAIL;
    }

    /* This Jobs LTS builds only {"status":"..","expectedVersion":".."} and has no statusDetails
     * support, so statusDetails is spliced in manually below (before the closing brace). */
    size_t message_len = Jobs_UpdateMsg(status->status, expected_version_str, expected_version_str_len,
                                        message_buffer + clientToken_pos, base_message_len + 1);

    if (message_len == 0) {
        OSAL_LOGE(TAG, "Failed to build Update message");
        return ESP_RMAKER_FAIL;
    }

    if (has_status_details) {
        /* Overwrite the trailing '}' with ,"statusDetails":<details>} */
        char *close_brace = message_buffer + clientToken_pos + message_len - 1;
        size_t remaining = sizeof(message_buffer) - (size_t)(close_brace - message_buffer);
        int appended = snprintf(close_brace, remaining, ",\"statusDetails\":%.*s}",
                                (int)status->status_details_str_len, status->status_details_str);
        if (appended < 0 || appended >= (int)remaining) {
            OSAL_LOGE(TAG, "Failed to insert statusDetails: appended=%d", appended);
            return ESP_RMAKER_FAIL;
        }
        message_len += (size_t)appended - 1; /* -1 for the '}' that was overwritten */
    }
    message_len += clientToken_pos;
    message_buffer[clientToken_pos] = ',';

    OSAL_LOGD(TAG, "Update message (length: %" PRIu32 "): %.*s", (uint32_t)message_len, (int)message_len, message_buffer);

    const char *status_str = (status->status == Succeeded) ? "SUCCEEDED" :
                             (status->status == Failed) ? "FAILED" :
                             (status->status == InProgress) ? "IN_PROGRESS" :
                             (status->status == Rejected) ? "REJECTED" : "UNKNOWN";

    OSAL_LOGI(TAG, "Updating job '%.*s' -> %s (version: %" PRId32 ")", (int)status->job_id_len, status->job_id, status_str, expected_version);

    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_OTA,
        .sub = MQTT_CHANNEL_SUB_OTA_UPDATE_JOB,
    };

    osal_err_t mqtt_status = esp_rmaker_mqtt_impl.publish(
                                 &channel, topic_buffer, topic_len, message_buffer, message_len,
                                 is_terminal ? QoS1 : QoS0, false);

    if (mqtt_status != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to publish Update: %d", mqtt_status);
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

static void ota_status_retry_task(void *arg)
{
    ota_status_job_entries_t *job_entries = (ota_status_job_entries_t *)arg;
    if (job_entries == NULL) {
        return;
    }

    ota_status_update_t status;
    memset(&status, 0, sizeof(status));
    char *status_details_copy = NULL;
    size_t status_details_len = 0;
    int32_t expected_version = 0;
    uint64_t next_delay = 0;
    bool should_retry = false;

    ota_status_cache_lock();
    if (job_entries->has_cached_terminal) {
        status.status = job_entries->cached_status;
        memcpy(status.job_id, job_entries->job_id, job_entries->job_id_len);
        status.job_id[job_entries->job_id_len] = '\0';
        status.job_id_len = job_entries->job_id_len;
        if (job_entries->cached_status_details_str != NULL && job_entries->cached_status_details_str_len > 0) {
            status_details_copy = (char *)OSAL_MALLOC_EXTRAM(job_entries->cached_status_details_str_len * sizeof(char));
            if (status_details_copy != NULL) {
                memcpy(status_details_copy, job_entries->cached_status_details_str,
                       job_entries->cached_status_details_str_len);
                status_details_len = job_entries->cached_status_details_str_len;
            }
        }
        expected_version = job_entries->version;
        job_entries->version++;
        next_delay = job_entries->retry_delay_ms * 2;
        if (next_delay > OTA_STATUS_RETRY_MAX_DELAY_MS) {
            next_delay = OTA_STATUS_RETRY_MAX_DELAY_MS;
        }
        job_entries->retry_delay_ms = next_delay;
        should_retry = true;
    }
    ota_status_cache_unlock();

    if (!should_retry) {
        if (status_details_copy != NULL) {
            free(status_details_copy);
        }
        return;
    }

    status.status_details_str = status_details_copy;
    status.status_details_str_len = status_details_len;

    esp_rmaker_error_t err = ota_status_publish_update(&status, expected_version, true, job_entries->job_id_int);
    if (status_details_copy != NULL) {
        free(status_details_copy);
    }

    if (err == ESP_RMAKER_OK) {
        /* Publish succeeded at the MQTT layer, but the broker's accepted/rejected
         * response can still be lost before it reaches us, e.g. it arrives during
         * the jobs re-subscribe gap after a reconnect and is dropped as an
         * unsolicited publish. Keep the retry timer armed as a response watchdog:
         * if no response removes this entry within next_delay, we republish.
         * The timer is cancelled when the terminal update is accepted (entry
         * removed in ota_status_on_update_response) or otherwise definitively
         * handled, so this does not loop once the update is acknowledged. */
        if (ota_status_schedule_or_reset_retry(job_entries, next_delay) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to re-arm response watchdog");
        }
        return;
    }

    /* Best-effort reversal of the version increment */
    ota_status_cache_overwrite_version(status.job_id, status.job_id_len, expected_version);

    OSAL_LOGW(TAG, "Retry publish failed: %d, rescheduling retry task in %" PRIu64 "ms", err, next_delay);
    if (ota_status_schedule_or_reset_retry(job_entries, next_delay) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to reschedule retry task");
    }
}

static esp_rmaker_error_t ota_status_cache_overwrite_version(const char *job_id, size_t job_id_len, int32_t version)
{
    if (job_id == NULL || job_id_len == 0) {
        return ESP_RMAKER_INVALID_ARG;
    }

    ota_status_cache_lock();
    ota_status_job_entries_t *job_entries = NULL;
    esp_rmaker_error_t err = ota_status_cache_get_or_create_job_entries_locked(job_id, job_id_len, &job_entries);
    if (err != ESP_RMAKER_OK) {
        ota_status_cache_unlock();
        return err;
    }

    job_entries->version = version;
    ota_status_cache_unlock();
    return ESP_RMAKER_OK;
}

static bool ota_status_reject_is_unrecoverable(const char *code, size_t code_len)
{
    /* AWS IoT Jobs UpdateJobExecution reject codes for which a retry can never be
     * accepted: the execution is gone, it is already in a terminal state, or the
     * request itself was malformed. Everything else (InternalError, RequestThrottled,
     * VersionMismatch, or any code added later) stays retryable. */
    static const char *const k_unrecoverable_codes[] = {
        "ResourceNotFound",
        "TerminalStateReached",
        "InvalidStateTransition",
        "InvalidRequest",
        "InvalidTopic",
        "InvalidJson",
    };

    if (code == NULL || code_len == 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof(k_unrecoverable_codes) / sizeof(k_unrecoverable_codes[0]); i++) {
        size_t len = strlen(k_unrecoverable_codes[i]);
        if (code_len == len && strncmp(code, k_unrecoverable_codes[i], len) == 0) {
            return true;
        }
    }
    return false;
}

static bool __str_to_uint32(const char *str, size_t str_len, uint32_t *p_value)
{
    if (p_value == NULL || str == NULL || str_len == 0) {
        return false;
    }

    uint32_t value = 0;
    for (size_t i = 0; i < str_len; i++) {
        char c = str[i];
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    *p_value = value;
    return true;
}

static bool ota_status_cache_token_from_payload(const char *payload, size_t payload_len, bool *p_is_terminal,
        uint32_t *p_job_id_int)
{
    if (p_is_terminal == NULL || p_job_id_int == NULL || payload == NULL || payload_len == 0) {
        return false;
    }

    const char *client_token = NULL;
    size_t client_token_len = 0;
    JSONTypes_t client_token_type = JSONNull;
    JSONStatus_t json_status = JSON_SearchConst(payload, payload_len,
                               "clientToken",
                               CONST_STRLEN("clientToken"),
                               &client_token, &client_token_len, &client_token_type);
    if (json_status != JSONSuccess || client_token_len < 2 || client_token_type != JSONString) {
        OSAL_LOGE(TAG, "Failed to get client token from payload");
        return false;
    }

    if (client_token[0] == '1') {
        *p_is_terminal = true;
    } else if (client_token[0] == '0') {
        *p_is_terminal = false;
    } else {
        OSAL_LOGE(TAG, "Invalid client token terminal byte");
        return false;
    }

    return __str_to_uint32(client_token + 1, client_token_len - 1, p_job_id_int);
}

/* Public function definitions ****************************************************/

esp_rmaker_error_t ota_status_init(const char *thing_name, size_t thing_name_length)
{
    if (thing_name == NULL || thing_name_length == 0 || thing_name_length > THINGNAME_MAX_LENGTH) {
        OSAL_LOGE(TAG, "Invalid thing name: %s, length: %d", thing_name, (int)thing_name_length);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Store thing name */
    g_thing_name = thing_name;
    g_thing_name_length = thing_name_length;

    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Initialize the OTA status cache */
    err = ota_status_cache_init();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize OTA status cache: %d", err);
        goto ota_status_init_fail;
    }
    return ESP_RMAKER_OK;

ota_status_init_fail:
    return ota_status_deinit();
}

esp_rmaker_error_t ota_status_deinit(void)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Clear the thing name */
    g_thing_name = NULL;
    g_thing_name_length = 0;

    /* Deinitialize the OTA status cache */
    err = ota_status_cache_deinit();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to deinitialize OTA status cache: %d", err);
    }
    return err;
}

esp_rmaker_error_t ota_status_set_initial_expected_version(const char *job_id, size_t job_id_len, int32_t initial_version)
{
    if (job_id == NULL || job_id_len == 0) {
        OSAL_LOGE(TAG, "Invalid job ID: job_id=%p, job_id_len=%d", (void *)job_id, (int)job_id_len);
        return ESP_RMAKER_INVALID_ARG;
    }
    if (job_id_len > JOBID_MAX_LENGTH) {
        OSAL_LOGE(TAG, "Invalid job ID length: %d (max %d)", (int)job_id_len, (int)JOBID_MAX_LENGTH);
        return ESP_RMAKER_INVALID_ARG;
    }

    return ota_status_cache_overwrite_version(job_id, job_id_len, initial_version);
}

void ota_status_clear_job_entries(const char *job_id, size_t job_id_len)
{
    if (job_id == NULL || job_id_len == 0 || p_ota_status_cache == NULL) {
        return;
    }

    ota_status_cache_lock();
    ota_status_job_entries_t *current = p_ota_status_cache->entries.head;
    while (current != NULL) {
        if (current->job_id_len == job_id_len && strncmp(current->job_id, job_id, job_id_len) == 0) {
            break;
        }
        current = current->next;
    }

    /* Delegate to the canonical removal so the retry timer is stopped and
     * cancelled before the entry is freed. Removing inline would leave an armed
     * watchdog timer pointing at freed memory (use-after-free when it fires). */
    if (current != NULL) {
        ota_status_cache_remove_job_entries_locked(current);
    }
    ota_status_cache_unlock();
}

void ota_status_resend_pending_terminals(void)
{
    if (p_ota_status_cache == NULL) {
        return;
    }

    ota_status_cache_lock();
    size_t pending = p_ota_status_cache->entries.count;
    if (pending == 0) {
        ota_status_cache_unlock();
        return;
    }

    /* Snapshot the cached terminal updates under the lock so we can republish
     * them after releasing it: ota_status_send() takes the lock itself. */
    ota_status_update_t *snapshot = (ota_status_update_t *)OSAL_CALLOC_EXTRAM(pending, sizeof(ota_status_update_t));
    if (snapshot == NULL) {
        ota_status_cache_unlock();
        OSAL_LOGE(TAG, "Failed to allocate resend snapshot");
        return;
    }

    size_t count = 0;
    for (ota_status_job_entries_t *job_entries = p_ota_status_cache->entries.head;
            job_entries != NULL && count < pending; job_entries = job_entries->next) {
        if (!job_entries->has_cached_terminal) {
            continue;
        }
        ota_status_update_t *s = &snapshot[count];
        s->status = job_entries->cached_status;
        memcpy(s->job_id, job_entries->job_id, job_entries->job_id_len);
        s->job_id[job_entries->job_id_len] = '\0';
        s->job_id_len = job_entries->job_id_len;
        if (job_entries->cached_status_details_str != NULL && job_entries->cached_status_details_str_len > 0) {
            s->status_details_str = (char *)OSAL_MALLOC_EXTRAM(job_entries->cached_status_details_str_len * sizeof(char));
            if (s->status_details_str != NULL) {
                memcpy(s->status_details_str, job_entries->cached_status_details_str, job_entries->cached_status_details_str_len);
                s->status_details_str_len = job_entries->cached_status_details_str_len;
            }
        }
        count++;
    }
    ota_status_cache_unlock();

    /* Republish each cached terminal update now that the subscription is live.
     * ota_status_send() publishes immediately and re-arms the response watchdog
     * for the terminal status, so a response dropped during the re-subscribe gap
     * no longer leaves the state machine waiting. */
    for (size_t i = 0; i < count; i++) {
        if (ota_status_send(&snapshot[i], NULL) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to resend terminal status for job %.*s",
                      (int)snapshot[i].job_id_len, snapshot[i].job_id);
        }
        if (snapshot[i].status_details_str != NULL) {
            free(snapshot[i].status_details_str);
        }
    }
    free(snapshot);
}

bool ota_status_is_cache_empty(void)
{
    if (p_ota_status_cache == NULL) {
        return true;
    }

    ota_status_cache_lock();
    bool is_empty = p_ota_status_cache->entries.count == 0;
    ota_status_cache_unlock();
    return is_empty;
}


#if defined(RMAKER_OTA_JOBS_TEST_WRAP_LINKER) || defined(RMAKER_OTA_JOBS_TEST_WRAP_DL_LIB)
static bool g_test_ota_status_publish_fail = false;

void rmaker_ota_status_test_set_publish_fail(int fail)
{
    g_test_ota_status_publish_fail = (fail != 0);
}

void rmaker_ota_status_test_set_intercept_scheduling(int enable)
{
    g_test_intercept_scheduling = (enable != 0);
}

int rmaker_ota_status_test_get_retry_schedule_count(void)
{
    return g_test_retry_schedule_count;
}

void rmaker_ota_status_test_reset_retry_schedule_count(void)
{
    g_test_retry_schedule_count = 0;
}

/* Synchronously run the retry task once for the first cached entry. */
void rmaker_ota_status_test_run_first_retry(void)
{
    ota_status_cache_lock();
    ota_status_job_entries_t *entry =
        (p_ota_status_cache != NULL) ? p_ota_status_cache->entries.head : NULL;
    ota_status_cache_unlock();
    if (entry != NULL) {
        ota_status_retry_task(entry);
    }
}
#endif

esp_rmaker_error_t ota_status_send(const ota_status_update_t *status, int32_t *p_next_version)
{
#if defined(RMAKER_OTA_JOBS_TEST_WRAP_LINKER) || defined(RMAKER_OTA_JOBS_TEST_WRAP_DL_LIB)
    if (g_test_ota_status_publish_fail) {
        return ESP_RMAKER_FAIL;
    }
#endif
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Validate the status */
    if (status == NULL || status->job_id[0] == '\0' || status->job_id_len == 0) {
        OSAL_LOGE(TAG, "Invalid status: status=%p, job_id[0]='%c', job_id_len=%d", (void *)status, status->job_id[0], (int)status->job_id_len);
        return ESP_RMAKER_INVALID_ARG;
    }
    if (status->job_id_len > JOBID_MAX_LENGTH) {
        OSAL_LOGE(TAG, "Invalid job ID length: %d (max %d)", (int)status->job_id_len, (int)JOBID_MAX_LENGTH);
        return ESP_RMAKER_INVALID_ARG;
    }
    if (status->status == Queued) {
        OSAL_LOGE(TAG, "Queued is an invalid update status");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Validate status details JSON */
    bool has_status_details = (status->status_details_str != NULL && status->status_details_str_len > 0);
    if (has_status_details) {
        JSONStatus_t json_status = JSON_Validate(status->status_details_str, status->status_details_str_len);
        if (json_status != JSONSuccess) {
            OSAL_LOGE(TAG, "Status details JSON is not a valid JSON string: %.*s", (int)status->status_details_str_len, status->status_details_str);
            return ESP_RMAKER_INVALID_ARG;
        }
    }

    bool is_terminal = ota_status_is_terminal(status->status);

    ota_status_cache_lock();
    ota_status_job_entries_t *job_entries = NULL;
    err = ota_status_cache_get_or_create_job_entries_locked(status->job_id, status->job_id_len, &job_entries);
    if (err != ESP_RMAKER_OK) {
        ota_status_cache_unlock();
        OSAL_LOGE(TAG, "Failed to get job entries: %d", err);
        return err;
    }

    int32_t expected_version = job_entries->version;
    if (is_terminal) {
        err = ota_status_cache_set_cached_terminal_locked(job_entries, status, expected_version);
        if (err != ESP_RMAKER_OK) {
            ota_status_cache_unlock();
            OSAL_LOGE(TAG, "Failed to cache terminal status: %d", err);
            return err;
        }
    }
    job_entries->version++;
    uint32_t job_id_int = job_entries->job_id_int;
    ota_status_cache_unlock();

    err = ota_status_publish_update(status, expected_version, is_terminal, job_id_int);
    if (err != ESP_RMAKER_OK) {
        /* Best-effort reversal of the version increment */
        ota_status_cache_overwrite_version(status->job_id, status->job_id_len, expected_version);
        if (p_next_version != NULL) {
            OSAL_LOGW(TAG, "Failed to publish update, reverting version to: %" PRId32, expected_version);
            *p_next_version = expected_version;
        }

        /* Retries are handled by the retry task only for terminal statuses */
        return is_terminal ? ESP_RMAKER_OK : err;
    }

    if (p_next_version != NULL) {
        *p_next_version = expected_version + 1;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t ota_status_on_update_response(const char *payload, size_t payload_len, bool accepted, ota_status_update_response_return_t *p_return)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Validate the payload */
    if (payload == NULL || payload_len == 0) {
        OSAL_LOGE(TAG, "Invalid payload: payload=%p, payload_len=%d", (void *)payload, (int)payload_len);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Get the client token details from the payload */
    bool is_terminal = false;
    uint32_t job_id_int = 0;
    if (!ota_status_cache_token_from_payload(payload, payload_len, &is_terminal, &job_id_int)) {
        OSAL_LOGE(TAG, "Failed to get client token from payload");
        return ESP_RMAKER_INVALID_ARG;
    }

    ota_status_cache_lock();
    ota_status_job_entries_t *job_entries = ota_status_cache_find_job_entries_by_int_locked(job_id_int);
    if (job_entries == NULL) {
        ota_status_cache_unlock();
        OSAL_LOGE(TAG, "Failed to find job entries for token: %" PRIu32, job_id_int);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Copy return values */
    if (p_return != NULL) {
        memcpy(p_return->job_id, job_entries->job_id, job_entries->job_id_len);
        p_return->job_id[job_entries->job_id_len] = '\0';
        p_return->job_id_len = job_entries->job_id_len;
        p_return->is_terminal = is_terminal;
        p_return->is_unrecoverable_reject = false;
    }

    /* If accepted, we can exit early for progress updates */
    if (accepted) {
        if (is_terminal) {
            ota_status_cache_remove_job_entries_locked(job_entries);
        }
        ota_status_cache_unlock();
        return ESP_RMAKER_OK;
    }

    /* Check the error code */
    const char *code = NULL;
    size_t code_len = 0;
    JSONTypes_t code_type = JSONNull;
    JSONStatus_t json_status = JSON_SearchConst(payload, payload_len,
                               "code",
                               CONST_STRLEN("code"),
                               &code, &code_len, &code_type);
    if (json_status != JSONSuccess || code_len == 0 || code_type != JSONString) {
        OSAL_LOGE(TAG, "Failed to get error code from payload");
        ota_status_cache_unlock();
        return ESP_RMAKER_INVALID_ARG;
    }

    size_t version_mismatch_len = CONST_STRLEN("VersionMismatch");

    if (code_len == version_mismatch_len && strncmp(code, "VersionMismatch", code_len) == 0) {
        /* Attempt to fix version mismatch */
        /* Get expected version from payload */
        const char *expected_version = NULL;
        size_t expected_version_len = 0;
        JSONTypes_t expected_version_type = JSONNull;
        JSONStatus_t json_status = JSON_SearchConst(payload, payload_len,
                                   "executionState.versionNumber",
                                   CONST_STRLEN("executionState.versionNumber"),
                                   &expected_version, &expected_version_len, &expected_version_type);
        if (json_status != JSONSuccess || expected_version_len == 0 || expected_version_type != JSONNumber) {
            OSAL_LOGE(TAG, "Failed to get expected version from payload");
            ota_status_cache_unlock();
            return ESP_RMAKER_INVALID_ARG;
        }

        /* Get expected version */
        uint32_t expected_version_num = 0;
        if (!__str_to_uint32(expected_version, expected_version_len, &expected_version_num)) {
            OSAL_LOGE(TAG, "Failed to convert expected version to uint32");
            ota_status_cache_unlock();
            return ESP_RMAKER_INVALID_ARG;
        }

        /* Overwrite the version with the expected version */
        job_entries->version = (int32_t)expected_version_num;
        ota_status_update_t cached_status;
        memset(&cached_status, 0, sizeof(cached_status));
        char *status_details_copy = NULL;
        size_t status_details_len = 0;
        bool has_cached_terminal = job_entries->has_cached_terminal;
        if (has_cached_terminal) {
            cached_status.status = job_entries->cached_status;
            memcpy(cached_status.job_id, job_entries->job_id, job_entries->job_id_len);
            cached_status.job_id[job_entries->job_id_len] = '\0';
            cached_status.job_id_len = job_entries->job_id_len;
            if (job_entries->cached_status_details_str != NULL && job_entries->cached_status_details_str_len > 0) {
                status_details_copy = (char *)OSAL_MALLOC_EXTRAM(job_entries->cached_status_details_str_len * sizeof(char));
                if (status_details_copy != NULL) {
                    memcpy(status_details_copy, job_entries->cached_status_details_str,
                           job_entries->cached_status_details_str_len);
                    status_details_len = job_entries->cached_status_details_str_len;
                }
            }
        }
        ota_status_cache_unlock();

        if (has_cached_terminal) {
            cached_status.status_details_str = status_details_copy;
            cached_status.status_details_str_len = status_details_len;
            OSAL_LOGD(TAG, "Resending cached terminal update for job %.*s with expected version: %" PRId32,
                      (int)cached_status.job_id_len, cached_status.job_id, (int32_t)expected_version_num);
            err = ota_status_send(&cached_status, NULL);
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to resend cached update with expected version: %d", err);
            }
        }

        if (status_details_copy != NULL) {
            free(status_details_copy);
        }

        return ESP_RMAKER_OK;
    }

    /* Unrecoverable reject: no future update for this execution can be accepted, so
     * drop the entry (which also disarms the retry timer) and tell the caller, instead
     * of leaving a retry loop that can never make progress. */
    if (ota_status_reject_is_unrecoverable(code, code_len)) {
        OSAL_LOGW(TAG, "Unrecoverable reject '%.*s' for job: %.*s, giving up on this update",
                  (int)code_len, code, (int)job_entries->job_id_len, job_entries->job_id);
        if (p_return != NULL) {
            p_return->is_unrecoverable_reject = true;
        }
        ota_status_cache_remove_job_entries_locked(job_entries);
        ota_status_cache_unlock();
        return ESP_RMAKER_OK;
    }

    if (job_entries->has_cached_terminal) {
        if (ota_status_schedule_or_reset_retry(job_entries, job_entries->retry_delay_ms) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to reschedule retry task");
        }
    }
    ota_status_cache_unlock();

    return ESP_RMAKER_OK;
}
