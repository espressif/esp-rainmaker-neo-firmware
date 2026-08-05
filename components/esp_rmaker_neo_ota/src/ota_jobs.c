/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_jobs.c
 * @brief OTA Jobs state machine implementation using AWS IoT Jobs
 */

/* Standard includes */
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>

/* Platform common includes */
#include "osal_time.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_event_loop.h"
#include "osal_ticks.h"
#include "osal_semaphore.h"
#include "osal_queue.h"
#include "osal_sysinfo.h"
#if !CONFIG_RMNG_OTA_DISABLE_AUTO_REBOOT
#include "osal_sysctrl.h"
#endif /* !CONFIG_RMNG_OTA_DISABLE_AUTO_REBOOT */

/* RMNG includes */
#include "esp_rmaker_work_queue.h"
#include "esp_rmaker_credentials.h"
#include "esp_rmaker_mqtt_impl.h"
#include "esp_rmaker_common_events.h"

/* Jobs library includes */
#include "jobs.h"
#include "job_parser.h"
#include "ota_job_processor.h"
#include "core_json.h"

/* Private includes */
#include "ota_jobs.h"
#include "ota_status.h"
#include "ota_nvs.h"
#include "ota_filetype_handler_internal.h"
#include "constants/ota_protocol.h"
#include "util/ota_partition.h"
#include "util/esp_rmaker_convert_base64.h"
#include "retry/esp_rmaker_backoff.h"

/* MQTT channels includes */
#include "network/ota_mqtt_channels.h"

/* Configuration includes */
#include "sdkconfig.h"

#define CONST_STRLEN(s) (sizeof(s) - 1)

/* Logging tag */
static const char *TAG = "rmng_ota_jobs";

/* Job ID queue */
#define OTA_JOB_ID_QUEUE_SIZE 50U
typedef struct {
    char job_ids[OTA_JOB_ID_QUEUE_SIZE][JOBID_MAX_LENGTH + 1];
    struct {
        uint16_t next;
        uint16_t size;
    } index;
} ota_job_id_queue_t;

static ota_job_id_queue_t g_ota_job_id_queue = {
    .index = {
        .next = 0,
        .size = 0,
    },
};

/* Retry context */
#define OTA_ERROR_RETRY_BASE_DELAY_MS 1000
#define OTA_ERROR_RETRY_MAX_DELAY_MS (5 * 60 * 1000)
static esp_rmaker_backoff_retry_context_t g_error_retry_context = {
    .handle = NULL,
    .delay_ctx = {
        .delay_ms = {
            .current = OTA_ERROR_RETRY_BASE_DELAY_MS,
            .max = OTA_ERROR_RETRY_MAX_DELAY_MS,
        },
        .params = {
            .exp_factor = 2,
            .max_jitter_ms = OTA_ERROR_RETRY_BASE_DELAY_MS,
        },
    },
};

/* Subscription retry context: separate from the job-level error retry.
 * Governs re-subscribe on disconnect/reconnect and SUBACK failure. */
static esp_rmaker_backoff_retry_context_t g_sub_retry_ctx = {
    .handle = NULL,
    .delay_ctx = {
        .delay_ms = {
            .current = OTA_ERROR_RETRY_BASE_DELAY_MS,
            .max = OTA_ERROR_RETRY_MAX_DELAY_MS,
        },
        .params = {
            .exp_factor = 2,
            .max_jitter_ms = OTA_ERROR_RETRY_BASE_DELAY_MS,
        },
    },
};

/* Terminal download->FSM handoff retry context: separate from the job-level error
 * retry so a re-delivery in flight cannot be reset out from under itself by an
 * unrelated error backoff. Governs re-delivery of a dropped terminal event
 * (see ota_job_state_post_terminal_event). */
static esp_rmaker_backoff_retry_context_t g_terminal_retry_ctx = {
    .handle = NULL,
    .delay_ctx = {
        .delay_ms = {
            .current = OTA_ERROR_RETRY_BASE_DELAY_MS,
            .max = OTA_ERROR_RETRY_MAX_DELAY_MS,
        },
        .params = {
            .exp_factor = 2,
            .max_jitter_ms = OTA_ERROR_RETRY_BASE_DELAY_MS,
        },
    },
};
/* Payload-free terminal event awaiting backoff re-delivery. Written only on the
 * (rare) direct-post failure path; read only by the backoff scheduler task. At most
 * one terminal event per job is in flight, so a single slot suffices. */
static ota_job_event_data_t g_terminal_event_pending = {0};

/* Pre-init event queue */
typedef struct _pre_init_event_t {
    ota_job_event_data_t *event_data;
    struct _pre_init_event_t *next;
} ota_job_pre_init_event_t;
typedef struct {
    ota_job_pre_init_event_t *head;
    ota_job_pre_init_event_t *tail;
    osal_semaphore_handle_t mutex;
    bool state_initialized;
} ota_job_pre_init_event_queue_t;
static ota_job_pre_init_event_queue_t g_pre_init_event_queue = {0};

/* Static event pool for payload-free events.
 *
 * Guarantees FSM transitions and the error/recovery path do not stall on heap
 * exhaustion. Slots are served via a fixed-size queue of pointers: take =
 * queue_receive, return = queue_send. Pool is sized to CONFIG_RMNG_OTA_EVENT_POOL_SIZE;
 * on exhaustion, copy_event_data falls back to heap so behaviour degrades
 * gracefully instead of failing outright.
 */
#define OTA_JOB_EVENT_POOL_SIZE CONFIG_RMNG_OTA_EVENT_POOL_SIZE
static ota_job_event_data_t g_event_pool_slots[OTA_JOB_EVENT_POOL_SIZE];
static osal_queue_handle_t g_event_pool_free_queue = NULL;

static esp_rmaker_error_t event_pool_init(void);
static void event_pool_deinit(void);
static ota_job_event_data_t *event_pool_take(void);
static bool event_pool_return(ota_job_event_data_t *event_data);

/* Global state machine context */
static ota_job_state_ctx_t g_ota_state_ctx = {
    .state = OTA_JOB_STATE_UNINITIALIZED,
    .subscribed = false,
    .timeout_handler_handle = NULL,
    .last_error = OTA_ERROR_NONE,
};

/* empty transition event */
static const ota_job_event_data_t empty_transition_event = {
    .event = OTA_JOB_EVENT_EMPTY_TRANSITION,
    .payload = NULL,
};

/* Forward declarations - event data helpers */
static ota_job_event_data_t *copy_event_data(const ota_job_event_data_t *event_data);
static void free_event_data(ota_job_event_data_t *event_data);
static esp_rmaker_error_t enqueue_event(const ota_job_event_data_t *event_data, bool should_copy);
#define enqueue_transition_event() enqueue_event(&empty_transition_event, true)

/* Forward declarations - pre-init event queue operations */
static esp_rmaker_error_t pre_init_event_queue_init(void);
static esp_rmaker_error_t pre_init_event_queue_deinit(void);
static bool pre_init_event_queue_check_init_flag(void);
static esp_rmaker_error_t pre_init_event_queue_add_if_not_initialized(const ota_job_event_data_t *event_data);
static esp_rmaker_error_t pre_init_event_queue_enqueue_all_and_set_state_initialized(void);

/* Forward declarations - OTA Data */
static esp_rmaker_error_t parse_custom_fields_from_job_doc(const char *job_doc, size_t job_doc_len, ota_job_info_custom_fields_t *custom_fields);
static esp_rmaker_error_t set_ota_data_from_job_doc(const AfrOtaJobDocumentFields_t *aws_fields, const ota_job_info_custom_fields_t *custom_fields);
static void clear_ota_data(void);

/* Forward declarations - Execution version operations */
static esp_rmaker_error_t execution_version_to_int32(const char *execution_version_str, size_t execution_version_len, int32_t *execution_version_num);

/* Forward declarations - Job ID queue operations */
static esp_rmaker_ota_error_reason_t pending_jobs_to_job_id_queue(const char *payload, size_t payload_len,
        const char *in_progress_jobid_array_path, const char *pending_jobid_array_path);
static const char *get_next_job_id_from_queue(void);
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
static void flush_job_id_queue(void);
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

/* Forward declarations - State handlers */
static void on_event_ignored(const ota_job_event_data_t *event_data);
static ota_job_state_t handle_state_uninitialized(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_network_init(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_reboot_check(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_idle(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_jobs_changed(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_fetching_pending_jobs(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_waiting_for_pending_jobs(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_pending_jobs_received(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_fetching_job_doc(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_waiting_for_job_doc(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_job_doc_received(const ota_job_event_data_t *event_data, bool *should_free_event);
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
static ota_job_state_t handle_state_custom_job_execution(const ota_job_event_data_t *event_data, bool *should_free_event);
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */
static ota_job_state_t handle_state_job_execution(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_post_download(const ota_job_event_data_t *event_data, bool *should_free_event);
static ota_job_state_t handle_state_error(const ota_job_event_data_t *event_data, bool *should_free_event);

/* Forward declarations - Recovery operations */
static void recovery_reset_context(void);

/* Forward declarations - MQTT topic operations */
static esp_rmaker_error_t mqtt_subscribe_jobs_topics(void);
static esp_rmaker_error_t mqtt_unsubscribe_jobs_topics(void);
static esp_rmaker_error_t mqtt_publish_get_pending(void);
static esp_rmaker_error_t mqtt_publish_describe_job(const char *job_id);
static esp_rmaker_error_t mqtt_publish_update_job_status(const char *job_id, uint16_t job_id_len, JobCurrentStatus_t status, const esp_rmaker_ota_status_details_t *status_details, int32_t *p_next_version);
static esp_rmaker_error_t mqtt_publish_update_job_status_with_string(const char *job_id, uint16_t job_id_len, JobCurrentStatus_t status, const char *status_details_json, size_t status_details_json_len, int32_t *p_next_version);
static esp_rmaker_error_t mqtt_publish_final_status(const esp_rmaker_ota_status_details_t *status_details);

/* Forward declarations - MQTT callbacks */
static void mqtt_unified_callback(const char *topic, size_t topic_len,
                                  void *payload, size_t payload_len, void *priv_data);
static void mqtt_on_jobs_changed(const char *topic, size_t topic_len,
                                 void *payload, size_t payload_len, void *priv_data);
static void mqtt_on_get_pending_accepted(const char *topic, size_t topic_len,
        void *payload, size_t payload_len, void *priv_data);
static void mqtt_on_get_pending_rejected(const char *topic, size_t topic_len,
        void *payload, size_t payload_len, void *priv_data);
static void mqtt_on_describe_accepted(const char *topic, size_t topic_len,
                                      void *payload, size_t payload_len, void *priv_data);
static void mqtt_on_describe_rejected(const char *topic, size_t topic_len,
                                      void *payload, size_t payload_len, void *priv_data);
static void mqtt_on_update_accepted(const char *topic, size_t topic_len,
                                    void *payload, size_t payload_len, void *priv_data);
static void mqtt_on_update_rejected(const char *topic, size_t topic_len,
                                    void *payload, size_t payload_len, void *priv_data);

/* Forward declarations - Download window operations */
#if CONFIG_RMNG_OTA_TIME_SUPPORT
static bool has_download_window(const ota_job_download_window_t *download_window);
static int32_t get_download_window_delay_time(const ota_job_download_window_t *download_window);
#endif /* CONFIG_RMNG_OTA_TIME_SUPPORT */

/* Forward declarations - Helper functions */
static void start_response_timer(void);
static void stop_response_timer(void);
static void timeout_callback(void *priv_data);
static const char *ota_job_state_to_string(ota_job_state_t state);
static const char *ota_job_event_to_string(ota_job_event_t event);
static void enter_error_state(esp_rmaker_ota_error_reason_t error);
static void enter_error_state_custom(esp_rmaker_ota_error_reason_t error, ota_job_state_t recovery_state, const ota_job_event_data_t *recovery_event_data);
static void retry_reset_backoff(void);
static void retry_with_backoff_task(void *arg);

/* Forward declarations - subscription retry */
static void sub_retry_sched_cb(void *unused);
static void sub_retry_work(void *unused);
static void on_rmaker_mqtt_event(void *arg, osal_event_base_t base, int32_t id, void *data);
static esp_rmaker_error_t register_mqtt_event_handlers(void);
static void unregister_mqtt_event_handlers(void);

/* Critical initialization functions */

esp_rmaker_error_t ota_job_critical_init(void)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Pool must be ready before any copy_event_data call (pre-init queue relies on it) */
    err = event_pool_init();
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Initialize pre-init event queue */
    err = pre_init_event_queue_init();
    if (err != ESP_RMAKER_OK) {
        event_pool_deinit();
        return err;
    }

    return ESP_RMAKER_OK;
}

/* State machine implementation */

esp_rmaker_error_t ota_job_state_init(const ota_job_config_t *ota_job_config)
{
    if (ota_job_config == NULL) {
        OSAL_LOGE(TAG, "NULL OTA job configuration");
        return ESP_RMAKER_INVALID_ARG;
    }

    if (ota_job_config->image_download.ota_cb == NULL) {
        OSAL_LOGE(TAG, "NULL OTA callback");
        return ESP_RMAKER_INVALID_ARG;
    }

    if (g_pre_init_event_queue.state_initialized) {
        OSAL_LOGW(TAG, "State machine already initialized");
        return ESP_RMAKER_OK;
    }

    esp_rmaker_error_t err;

    /* Store thing name */
    char *thing_name = NULL;
    err = esp_rmaker_credentials_get_thing_name(&thing_name);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get thing name");
        return err;
    }

    /* Store a copy in static context */
    size_t thing_name_len = strlen(thing_name);
    if (thing_name_len > THINGNAME_MAX_LENGTH) {
        OSAL_LOGE(TAG, "Thing name too long: %d > %d", (int)thing_name_len, THINGNAME_MAX_LENGTH);
        free(thing_name);
        return ESP_RMAKER_INVALID_ARG;
    }
    memcpy(g_ota_state_ctx.thing_name, thing_name, thing_name_len);
    g_ota_state_ctx.thing_name[thing_name_len] = '\0';
    g_ota_state_ctx.thing_name_length = thing_name_len;
    free(thing_name);

#if CONFIG_RMNG_OTA_SIGNATURE_VERIFY_ENABLE
    /* Enforce codesign certificate presence when signature verification is enabled */
    esp_rmaker_credential_t codesign_cert;
    err = esp_rmaker_credentials_get_codesign_cert(&codesign_cert);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "OTA signature verification enabled but codesign certificate not found: %d", err);
        return err;
    }
    esp_rmaker_credentials_free_credential(&codesign_cert);
#endif /* CONFIG_RMNG_OTA_SIGNATURE_VERIFY_ENABLE */

    /* Initialize OTA status manager */
    err = ota_status_init(g_ota_state_ctx.thing_name, g_ota_state_ctx.thing_name_length);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize OTA status manager");
        return err;
    }

    /* Configure timeout handler */
    rmaker_ota_timeout_handler_config_t timeout_config = {
        .timeout_ms = OTA_RESPONSE_TIMEOUT_MS,
        .callback = timeout_callback,
        .priv_data = &g_ota_state_ctx,
    };
    err = rmaker_ota_timeout_handler_init(&timeout_config, &g_ota_state_ctx.timeout_handler_handle);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize timeout handler");
        return err;
    }

    /* Initialize error retry context */
    esp_rmaker_backoff_reset(&g_error_retry_context, OTA_ERROR_RETRY_BASE_DELAY_MS);

    /* Initialize subscription retry context */
    esp_rmaker_backoff_reset(&g_sub_retry_ctx, OTA_ERROR_RETRY_BASE_DELAY_MS);

    /* Initialize terminal-event re-delivery retry context */
    esp_rmaker_backoff_reset(&g_terminal_retry_ctx, OTA_ERROR_RETRY_BASE_DELAY_MS);

    /* Register MQTT event handlers so reconnect / SUBACK failure can drive
     * the subscription retry context. Must be registered before the first
     * subscribe attempt (NETWORK_INIT handler). */
    err = register_mqtt_event_handlers();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to register MQTT event handlers");
        return err;
    }

    /* Store OTA job configuration */
    g_ota_state_ctx.image_download.ota_cb = ota_job_config->image_download.ota_cb;
    g_ota_state_ctx.image_download.validate_image_ref = ota_job_config->image_download.validate_image_ref;
    g_ota_state_ctx.image_download.priv = ota_job_config->image_download.priv;
    g_ota_state_ctx.filetype_handler_lookup = ota_job_config->filetype_handler_lookup;
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
    g_ota_state_ctx.custom_job_cb = ota_job_config->custom_job_cb;
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

    g_ota_state_ctx.current_job.has_active_job = false;
    g_ota_state_ctx.current_job.filetype_handler = NULL;
    g_ota_state_ctx.current_job.expected_version = 1;

    g_ota_state_ctx.state = OTA_JOB_STATE_UNINITIALIZED;
    g_ota_state_ctx.last_error = OTA_ERROR_NONE;

    OSAL_LOGI(TAG, "State machine initialized for thing: %s", g_ota_state_ctx.thing_name);

    /* Always transition to network init state, then all pre-init queued events */
    if (err == ESP_RMAKER_OK) {
        OSAL_LOGI(TAG, "Transitioning to NETWORK_INIT state");
        g_ota_state_ctx.state = OTA_JOB_STATE_NETWORK_INIT;
        err = enqueue_transition_event();
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to enqueue initial transition event: %d", err);
            return err;
        }

        OSAL_LOGI(TAG, "Enqueuing all pre-init queued events and setting state initialized");
        err = pre_init_event_queue_enqueue_all_and_set_state_initialized();
    }
    return err;
}

esp_rmaker_error_t ota_job_state_deinit(void)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    if (!pre_init_event_queue_check_init_flag()) {
        return ESP_RMAKER_OK;
    }

    /* Stop timeout handler */
    err = rmaker_ota_timeout_handler_deinit(g_ota_state_ctx.timeout_handler_handle);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to deinitialize timeout handler");
    }

    /* Stop fetch retry */
    esp_rmaker_backoff_reset(&g_error_retry_context, OTA_ERROR_RETRY_BASE_DELAY_MS);

    /* Cancel any pending terminal-event re-delivery */
    esp_rmaker_backoff_reset(&g_terminal_retry_ctx, OTA_ERROR_RETRY_BASE_DELAY_MS);

    /* Unregister MQTT event handlers first so a late event cannot race with
     * the teardown below. */
    unregister_mqtt_event_handlers();

    /* Unsubscribe from topics (always runs: also cancels pending sub retry). */
    mqtt_unsubscribe_jobs_topics();

    /* Deinitialize OTA status manager */
    err = ota_status_deinit();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to deinitialize OTA status manager");
    }

    /* Reset context */
    memset(&g_ota_state_ctx, 0, sizeof(g_ota_state_ctx));
    g_ota_state_ctx.state = OTA_JOB_STATE_UNINITIALIZED;

    /* Deinitialize pre-init event queue */
    pre_init_event_queue_deinit();

    /* Tear down event pool last so any in-flight free_event_data calls
     * via pre_init_event_queue_deinit can return their slots first. */
    event_pool_deinit();

    OSAL_LOGI(TAG, "State machine deinitialized");
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t ota_job_state_fetch(void)
{
    /* Post fetch event to state machine */
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_FETCH_REQUESTED,
        .payload = NULL,
    };
    return ota_job_state_post_event(&event_data);
}

esp_rmaker_error_t ota_job_state_reboot_check(void)
{
    /* Post reboot check event to state machine */
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_REBOOT_CHECK_REQUESTED,
        .payload = NULL,
    };
    return ota_job_state_post_event(&event_data);
}

esp_rmaker_error_t ota_job_state_post_event(const ota_job_event_data_t *event_data)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    /* If the state machine is initialized, enqueue the event to the event loop directly */
    if (g_pre_init_event_queue.state_initialized) {
        return enqueue_event(event_data, true);
    }

    /* Attempt to add the event to the pre-init event queue */
    err = pre_init_event_queue_add_if_not_initialized(event_data);

    /* Handle the case where the state machine is already initialized (race condition) */
    if (err == ESP_RMAKER_ALREADY_INITIALIZED) {
        return enqueue_event(event_data, true);
    }
    return err;
}

/* Backoff-scheduler task that re-delivers a dropped terminal event. Mirrors
 * retry_with_backoff_task: re-arm on failure, reset the backoff on success, so the
 * handoff self-heals and never permanently stalls. */
static void terminal_event_repost_task(void *arg)
{
    (void)arg;
    esp_rmaker_error_t err = ota_job_state_post_event(&g_terminal_event_pending);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Terminal event '%s' re-delivery failed (%d); rescheduling with backoff",
                  ota_job_event_to_string(g_terminal_event_pending.event), err);
        esp_rmaker_backoff_retry(&g_terminal_retry_ctx, terminal_event_repost_task, NULL);
        return;
    }
    OSAL_LOGI(TAG, "Terminal event '%s' delivered after backoff retry",
              ota_job_event_to_string(g_terminal_event_pending.event));
    esp_rmaker_backoff_reset(&g_terminal_retry_ctx, OTA_ERROR_RETRY_BASE_DELAY_MS);
}

esp_rmaker_error_t ota_job_state_post_terminal_event(const ota_job_event_data_t *event_data)
{
    if (event_data == NULL) {
        OSAL_LOGE(TAG, "Cannot post NULL terminal event");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Fast path: try to hand off directly. */
    esp_rmaker_error_t err = ota_job_state_post_event(event_data);
    if (err == ESP_RMAKER_OK) {
        return ESP_RMAKER_OK;
    }

    /* Direct handoff failed (transient OOM: work-queue enqueue). Stash the event and
     * let the backoff scheduler re-deliver it until it lands, so the FSM never stalls
     * in JOB_EXECUTION. Terminal events are payload-free by contract: force payload
     * NULL so the stashed copy (and every re-delivery) stays heap-free. */
    OSAL_LOGW(TAG, "Terminal event '%s' post failed (%d); scheduling backoff re-delivery",
              ota_job_event_to_string(event_data->event), err);
    g_terminal_event_pending = *event_data;
    g_terminal_event_pending.payload = NULL;

    esp_rmaker_error_t sched_err = esp_rmaker_backoff_retry(&g_terminal_retry_ctx, terminal_event_repost_task, NULL);
    if (sched_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to schedule terminal event re-delivery (%d); OTA may stall", sched_err);
    }
    return sched_err;
}

void ota_job_state_run_once(void *priv_data)
{
    ota_job_state_ctx_t *ctx = &g_ota_state_ctx;
    ota_job_event_data_t *event_data = (ota_job_event_data_t *)priv_data;
    bool should_free_event = true;

    if (!pre_init_event_queue_check_init_flag()) {
        OSAL_LOGE(TAG, "State machine not initialized in run_once");
        free_event_data(event_data);
        return;
    }

    ota_job_state_t next_state = ctx->state;
    ota_job_event_t event = event_data ? event_data->event : OTA_JOB_EVENT_ERROR_OCCURRED;

    OSAL_LOGD(TAG, "State machine run_once, state: %d, event: %d", ctx->state, (int)event);

    if (event_data && event == OTA_JOB_EVENT_ERROR_OCCURRED) {
        ctx->state = OTA_JOB_STATE_ERROR;
        next_state = handle_state_error(event_data, &should_free_event);
        if (should_free_event) {
            free_event_data(event_data);
        }
        return;
    }

    switch (ctx->state) {
    case OTA_JOB_STATE_UNINITIALIZED:
        next_state = handle_state_uninitialized(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_NETWORK_INIT:
        next_state = handle_state_network_init(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_REBOOT_CHECK:
        next_state = handle_state_reboot_check(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_IDLE:
        next_state = handle_state_idle(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_JOBS_CHANGED:
        next_state = handle_state_jobs_changed(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_FETCHING_PENDING_JOBS:
        next_state = handle_state_fetching_pending_jobs(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS:
        next_state = handle_state_waiting_for_pending_jobs(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_PENDING_JOBS_RECEIVED:
        next_state = handle_state_pending_jobs_received(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_FETCHING_JOB_DOC:
        next_state = handle_state_fetching_job_doc(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_WAITING_FOR_JOB_DOC:
        next_state = handle_state_waiting_for_job_doc(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_JOB_DOC_RECEIVED:
        next_state = handle_state_job_doc_received(event_data, &should_free_event);
        break;
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
    case OTA_JOB_STATE_CUSTOM_JOB_EXECUTION:
        next_state = handle_state_custom_job_execution(event_data, &should_free_event);
        break;
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */
    case OTA_JOB_STATE_JOB_EXECUTION:
        next_state = handle_state_job_execution(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_POST_DOWNLOAD:
        next_state = handle_state_post_download(event_data, &should_free_event);
        break;
    case OTA_JOB_STATE_ERROR:
        next_state = handle_state_error(event_data, &should_free_event);
        break;
    default:
        OSAL_LOGE(TAG, "Unknown state: %d", ctx->state);
        break;
    }

    if (next_state != ctx->state) {
        OSAL_LOGI(TAG, "State transition: %s -> %s",
                  ota_job_state_to_string(ctx->state),
                  ota_job_state_to_string(next_state));
        ctx->state = next_state;
    }

    if (should_free_event) {
        free_event_data(event_data);
    }
}

ota_job_state_ctx_t *ota_job_state_get_context(void)
{
    return &g_ota_state_ctx;
}

static esp_rmaker_error_t __make_string_copy(const char *src, size_t src_len, char **dst)
{
    char *new_string = NULL;

    if (src == NULL || src_len == 0) {
        OSAL_LOGE(TAG, "Invalid source string: src=%p, src_len=%" PRIu32, src, (uint32_t)src_len);
        return ESP_RMAKER_INVALID_ARG;
    }

    new_string = OSAL_CALLOC_EXTRAM(src_len + 1, sizeof(char));
    if (new_string == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for string: src_len=%" PRIu32, (uint32_t)src_len);
        return ESP_RMAKER_NO_MEM;
    }
    memcpy(new_string, src, src_len);
    new_string[src_len] = '\0';
    *dst = new_string;
    return ESP_RMAKER_OK;
}

/* Pending event data operations */

/* Static event pool operations */

static esp_rmaker_error_t event_pool_init(void)
{
    if (g_event_pool_free_queue != NULL) {
        return ESP_RMAKER_OK;
    }

    g_event_pool_free_queue = osal_queue_create_ext(OTA_JOB_EVENT_POOL_SIZE, sizeof(ota_job_event_data_t *));
    if (g_event_pool_free_queue == NULL) {
        OSAL_LOGE(TAG, "Failed to create event pool free queue");
        return ESP_RMAKER_NO_MEM;
    }

    for (uint32_t i = 0; i < OTA_JOB_EVENT_POOL_SIZE; ++i) {
        ota_job_event_data_t *slot = &g_event_pool_slots[i];
        memset(slot, 0, sizeof(*slot));
        if (osal_queue_send(g_event_pool_free_queue, &slot, 0) != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to seed event pool slot %" PRIu32, i);
            osal_queue_delete(g_event_pool_free_queue);
            g_event_pool_free_queue = NULL;
            return ESP_RMAKER_FAIL;
        }
    }

    return ESP_RMAKER_OK;
}

static void event_pool_deinit(void)
{
    if (g_event_pool_free_queue == NULL) {
        return;
    }
    osal_queue_delete(g_event_pool_free_queue);
    g_event_pool_free_queue = NULL;
}

static ota_job_event_data_t *event_pool_take(void)
{
    if (g_event_pool_free_queue == NULL) {
        return NULL;
    }
    ota_job_event_data_t *slot = NULL;
    if (osal_queue_receive(g_event_pool_free_queue, &slot, 0) != OSAL_ERR_OK) {
        return NULL;
    }
    return slot;
}

static bool event_pool_return(ota_job_event_data_t *event_data)
{
    if (!event_data) {
        return false;
    }
    if (event_data < &g_event_pool_slots[0] ||
            event_data >= &g_event_pool_slots[OTA_JOB_EVENT_POOL_SIZE]) {
        return false;
    }
    memset(event_data, 0, sizeof(*event_data));
    /* Queue is sized to exactly OTA_JOB_EVENT_POOL_SIZE; returning a slot that
     * came from the pool can never overflow it. */
    (void)osal_queue_send(g_event_pool_free_queue, &event_data, 0);
    return true;
}

static ota_job_event_data_t *copy_event_data(const ota_job_event_data_t *event_data)
{
    ota_job_event_data_t *dst = NULL;

    if (event_data == NULL) {
        return NULL;
    }

    /* Payload-free events: prefer the static pool so FSM transitions and the
     * error/recovery path never stall on heap exhaustion. Fall back to heap if
     * the pool is momentarily empty. */
    if (event_data->payload == NULL) {
        dst = event_pool_take();
        if (dst != NULL) {
            dst->event = event_data->event;
            dst->payload = NULL;
            dst->fixed_data = event_data->fixed_data;
            return dst;
        }
        OSAL_LOGW(TAG, "Event pool exhausted, falling back to heap");
    }

    dst = OSAL_CALLOC_EXTRAM(1, sizeof(ota_job_event_data_t));
    if (!dst) {
        OSAL_LOGE(TAG, "Failed to allocate event data copy");
        return NULL;
    }

    dst->event = event_data->event;
    dst->fixed_data = event_data->fixed_data;

    if (event_data->payload != NULL) {
        dst->payload = OSAL_CALLOC_EXTRAM(1, sizeof(ota_job_event_data_payload_t));
        if (!dst->payload) {
            OSAL_LOGE(TAG, "Failed to allocate event payload copy");
            free(dst);
            return NULL;
        }

        dst->payload->len = event_data->payload->len;
        if (event_data->payload->data && event_data->payload->len > 0) {
            void *data_copy = OSAL_CALLOC_EXTRAM(event_data->payload->len, sizeof(uint8_t));
            if (data_copy == NULL) {
                OSAL_LOGE(TAG, "Failed to allocate memory for event payload copy");
                free_event_data(dst);
                return NULL;
            }
            memcpy(data_copy, event_data->payload->data, event_data->payload->len);
            dst->payload->data = data_copy;
        } else {
            /* May fit into the void pointer itself */
            dst->payload->data = event_data->payload->data;
            dst->payload->len = 0;
        }
    }

    return dst;
}

static void free_event_data(ota_job_event_data_t *event_data)
{
    if (!event_data) {
        return;
    }

    /* Return pool-backed slots instead of freeing. */
    if (event_pool_return(event_data)) {
        return;
    }

    if (event_data->payload) {
        if (event_data->payload->data && event_data->payload->len > 0) {
            free(event_data->payload->data);
        }
        free(event_data->payload);
    }
    free(event_data);
}

static esp_rmaker_error_t enqueue_event(const ota_job_event_data_t *event_data, bool should_copy)
{
    ota_job_event_data_t *event_to_queue = NULL;

    if (should_copy) {
        event_to_queue = copy_event_data(event_data);
        if (!event_to_queue) {
            return ESP_RMAKER_NO_MEM;
        }
    } else {
        event_to_queue = (ota_job_event_data_t *)event_data;
    }

    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(ota_job_state_run_once, event_to_queue);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to queue state machine execution");
        /* Preserve a single ownership contract: on any non-OK return, the
         * caller is not responsible for freeing event_data. Either we
         * allocated the copy ourselves, or the caller passed ownership in. */
        free_event_data(event_to_queue);
    }
    return err;
}

/* Pre-init event queue operations */

static esp_rmaker_error_t pre_init_event_queue_lock(void)
{
    if (g_pre_init_event_queue.mutex == NULL) {
        OSAL_LOGE(TAG, "Pre-init event queue not initialized");
        return ESP_RMAKER_INVALID_STATE;
    }
    osal_semaphore_take(g_pre_init_event_queue.mutex, OSAL_MAX_DELAY);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t pre_init_event_queue_unlock(void)
{
    if (g_pre_init_event_queue.mutex == NULL) {
        OSAL_LOGE(TAG, "Pre-init event queue not initialized");
        return ESP_RMAKER_INVALID_STATE;
    }
    osal_semaphore_give(g_pre_init_event_queue.mutex);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t pre_init_event_queue_init(void)
{
    /* Create mutex for pre-init event queue */
    g_pre_init_event_queue.mutex = osal_semaphore_create_mutex();
    if (g_pre_init_event_queue.mutex == NULL) {
        OSAL_LOGE(TAG, "Failed to create pre-init event queue mutex");
        return ESP_RMAKER_NO_MEM;
    }

    /* Initialize head and tail of pre-init event queue */
    g_pre_init_event_queue.head = NULL;
    g_pre_init_event_queue.tail = NULL;

    /* Initialize state of pre-init event queue */
    g_pre_init_event_queue.state_initialized = false;
    return ESP_RMAKER_OK;

}

static esp_rmaker_error_t pre_init_event_queue_deinit(void)
{
    if (g_pre_init_event_queue.mutex == NULL) {
        OSAL_LOGE(TAG, "Pre-init event queue not initialized");
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Delete mutex for pre-init event queue */
    osal_semaphore_delete(g_pre_init_event_queue.mutex);
    g_pre_init_event_queue.mutex = NULL;

    /* Free all events in pre-init event queue */
    ota_job_pre_init_event_t *current = g_pre_init_event_queue.head;
    while (current != NULL) {
        ota_job_pre_init_event_t *next = current->next;
        free_event_data(current->event_data);
        free(current);
        current = next;
    }

    /* Reset head and tail of pre-init event queue */
    g_pre_init_event_queue.head = NULL;
    g_pre_init_event_queue.tail = NULL;

    /* Reset state of pre-init event queue */
    g_pre_init_event_queue.state_initialized = false;

    return ESP_RMAKER_OK;
}

static bool pre_init_event_queue_check_init_flag(void)
{
    if (pre_init_event_queue_lock() != ESP_RMAKER_OK) {
        return false;
    }
    bool initialized = g_pre_init_event_queue.state_initialized;
    pre_init_event_queue_unlock();
    return initialized;
}

static esp_rmaker_error_t pre_init_event_queue_add_if_not_initialized(const ota_job_event_data_t *event_data)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    err = pre_init_event_queue_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    do {
        if (g_pre_init_event_queue.state_initialized) {
            err = ESP_RMAKER_ALREADY_INITIALIZED;
            break;
        }
        ota_job_pre_init_event_t *new_event = OSAL_CALLOC_EXTRAM(1, sizeof(ota_job_pre_init_event_t));
        if (!new_event) {
            err = ESP_RMAKER_NO_MEM;
            break;
        }
        new_event->event_data = copy_event_data(event_data);
        if (!new_event->event_data) {
            err = ESP_RMAKER_NO_MEM;
            break;
        }
        new_event->next = NULL;
        if (g_pre_init_event_queue.tail == NULL) {
            g_pre_init_event_queue.head = new_event;
        } else {
            g_pre_init_event_queue.tail->next = new_event;
        }
        g_pre_init_event_queue.tail = new_event;
    } while (0);

    pre_init_event_queue_unlock();
    return err;
}

static esp_rmaker_error_t pre_init_event_queue_enqueue_all_and_set_state_initialized(void)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    err = pre_init_event_queue_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    bool failed_at_least_once = false;
    do {
        ota_job_pre_init_event_t *current = g_pre_init_event_queue.head;
        while (current != NULL) {
            ota_job_pre_init_event_t *next = current->next;
            // Pass ownership of the event data to the event loop
            err = enqueue_event(current->event_data, false);
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to enqueue event '%s' to event loop: %d", ota_job_event_to_string(current->event_data->event), err);
                free_event_data(current->event_data);
                failed_at_least_once = true;
            }
            free(current);
            current = next;
        }
        g_pre_init_event_queue.head = NULL;
        g_pre_init_event_queue.tail = NULL;
    } while (0);

    g_pre_init_event_queue.state_initialized = !failed_at_least_once;
    pre_init_event_queue_unlock();
    if (failed_at_least_once) {
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

/* OTA data operations */

static esp_rmaker_error_t parse_custom_fields_from_job_doc(const char *job_doc, size_t job_doc_len, ota_job_info_custom_fields_t *custom_fields)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    if (job_doc == NULL || job_doc_len == 0 || custom_fields == NULL) {
        OSAL_LOGE(TAG, "Invalid job document or custom fields: job_doc=%p, job_doc_len=%" PRIu32 ", custom_fields=%p", job_doc, (uint32_t)job_doc_len, custom_fields);
        err = ESP_RMAKER_INVALID_ARG;
        return err;
    }

    /* Parse RMNG OTA fields */
    JSONStatus_t json_ret;
    const char *rmng_ota_payload = NULL;
    size_t rmng_ota_payload_len = 0;
    json_ret = JSON_SearchConst(job_doc, job_doc_len, "rmng_ota", CONST_STRLEN("rmng_ota"), &rmng_ota_payload, &rmng_ota_payload_len, NULL);
    if (json_ret != JSONSuccess || rmng_ota_payload_len == 0) {
        OSAL_LOGE(TAG, "Failed to parse RMNG OTA fields");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Optional: get firmware version */
    const char *fw_version = NULL;
    size_t fw_version_len = 0;
    json_ret = JSON_SearchConst(rmng_ota_payload, rmng_ota_payload_len, "fw_version", CONST_STRLEN("fw_version"), &fw_version, &fw_version_len, NULL);
    if (json_ret == JSONSuccess && fw_version_len > 0) {
        custom_fields->fw_version = fw_version;
        custom_fields->fw_version_len = fw_version_len;
    } else {
        custom_fields->fw_version = NULL;
        custom_fields->fw_version_len = 0;
    }

    /* Optional: Get minimum firmware version */
    const char *min_fw_version = NULL;
    size_t min_fw_version_len = 0;
    json_ret = JSON_SearchConst(rmng_ota_payload, rmng_ota_payload_len, "min_fw_version", CONST_STRLEN("min_fw_version"), &min_fw_version, &min_fw_version_len, NULL);
    if (json_ret == JSONSuccess && min_fw_version_len > 0) {
        custom_fields->min_fw_version = min_fw_version;
        custom_fields->min_fw_version_len = min_fw_version_len;
    } else {
        custom_fields->min_fw_version = NULL;
        custom_fields->min_fw_version_len = 0;
    }

    /* Optional: Get file MD5 (hex). Enables auto-resume + completion integrity check. */
    const char *file_md5 = NULL;
    size_t file_md5_len = 0;
    json_ret = JSON_SearchConst(rmng_ota_payload, rmng_ota_payload_len, "file_md5", CONST_STRLEN("file_md5"), &file_md5, &file_md5_len, NULL);
    if (json_ret == JSONSuccess && file_md5_len > 0) {
        custom_fields->file_md5 = file_md5;
        custom_fields->file_md5_len = file_md5_len;
    } else {
        custom_fields->file_md5 = NULL;
        custom_fields->file_md5_len = 0;
    }

    /* Firmware version is required if minimum firmware version is specified */
    if (custom_fields->min_fw_version != NULL && custom_fields->min_fw_version_len > 0) {
        if (custom_fields->fw_version == NULL || custom_fields->fw_version_len == 0) {
            OSAL_LOGE(TAG, "Custom fields: firmware version is required if minimum firmware version is specified");
            return ESP_RMAKER_INVALID_ARG;
        }
    }

    /* Optional: Get filetype */
    const char *filetype = NULL;
    size_t filetype_len = 0;
    json_ret = JSON_SearchConst(rmng_ota_payload, rmng_ota_payload_len, "filetype", CONST_STRLEN("filetype"), &filetype, &filetype_len, NULL);
    if (json_ret == JSONSuccess && filetype_len > 0) {
        custom_fields->filetype = filetype;
        custom_fields->filetype_len = filetype_len;
    } else {
        custom_fields->filetype = NULL;
        custom_fields->filetype_len = 0;
    }

    /* Optional: Get metadata */
    const char *metadata = NULL;
    size_t metadata_len = 0;
    json_ret = JSON_SearchConst(rmng_ota_payload, rmng_ota_payload_len, "metadata", CONST_STRLEN("metadata"), &metadata, &metadata_len, NULL);
    if (json_ret == JSONSuccess && metadata_len > 0) {
        custom_fields->metadata = metadata;
        custom_fields->metadata_len = metadata_len;
    } else {
        custom_fields->metadata = NULL;
        custom_fields->metadata_len = 0;
    }

    /* Optional: Get download window */
#if CONFIG_RMNG_OTA_TIME_SUPPORT
    custom_fields->download_window = (ota_job_download_window_t) {
        .daily = {
            .start = -1,
            .end = -1,
        },
        .validity = {
            .start = 0,
            .end = 0,
        },
    };
    const char *download_window = NULL;
    size_t download_window_len = 0;
    json_ret = JSON_SearchConst(rmng_ota_payload, rmng_ota_payload_len, "download_window", CONST_STRLEN("download_window"), &download_window, &download_window_len, NULL);
    if (json_ret == JSONSuccess && download_window_len > 0) {
        // Look for validity start
        const char *validity_start = NULL;
        size_t validity_start_len = 0;
        json_ret = JSON_SearchConst(download_window, download_window_len, "validity.start", CONST_STRLEN("validity.start"), &validity_start, &validity_start_len, NULL);
        if (json_ret == JSONSuccess && validity_start_len > 0) {
            custom_fields->download_window.validity.start = strtol(validity_start, NULL, 10);
        }
        // Look for validity end
        const char *validity_end = NULL;
        size_t validity_end_len = 0;
        json_ret = JSON_SearchConst(download_window, download_window_len, "validity.end", CONST_STRLEN("validity.end"), &validity_end, &validity_end_len, NULL);
        if (json_ret == JSONSuccess && validity_end_len > 0) {
            custom_fields->download_window.validity.end = strtol(validity_end, NULL, 10);
        }
        // Look for daily start
        const char *daily_start = NULL;
        size_t daily_start_len = 0;
        json_ret = JSON_SearchConst(download_window, download_window_len, "daily.start", CONST_STRLEN("daily.start"), &daily_start, &daily_start_len, NULL);
        if (json_ret == JSONSuccess && daily_start_len > 0) {
            custom_fields->download_window.daily.start = strtol(daily_start, NULL, 10);
        }
        // Look for daily end
        const char *daily_end = NULL;
        size_t daily_end_len = 0;
        json_ret = JSON_SearchConst(download_window, download_window_len, "daily.end", CONST_STRLEN("daily.end"), &daily_end, &daily_end_len, NULL);
        if (json_ret == JSONSuccess && daily_end_len > 0) {
            custom_fields->download_window.daily.end = strtol(daily_end, NULL, 10);
        }
    }
#endif /* CONFIG_RMNG_OTA_TIME_SUPPORT */
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t set_ota_data_from_job_doc(const AfrOtaJobDocumentFields_t *aws_fields, const ota_job_info_custom_fields_t *custom_fields)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    if (aws_fields == NULL || custom_fields == NULL) {
        OSAL_LOGE(TAG, "Invalid AWS fields or custom fields: aws_fields=%p, custom_fields=%p", aws_fields, custom_fields);
        err = ESP_RMAKER_INVALID_ARG;
        goto set_ota_data_from_job_doc_fail;
    }

    /* Set OTA data */

    // File size
    uint32_t file_size = aws_fields->fileSize;
    if (file_size > INT_MAX) {
        OSAL_LOGE(TAG, "File size too large: %" PRIu32 ", unable to cast to int", file_size);
        err = ESP_RMAKER_INVALID_ARG;
        goto set_ota_data_from_job_doc_fail;
    }
    g_ota_state_ctx.current_job.ota_data.filesize = (int)file_size;

    // File ID within the stream (a stream may carry more than one file)
    g_ota_state_ctx.current_job.ota_data.file_id = aws_fields->fileId;

    // Stream ID
    err = __make_string_copy(aws_fields->imageRef, aws_fields->imageRefLen, &g_ota_state_ctx.current_job.ota_data.stream_id);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to make string copy for stream ID");
        goto set_ota_data_from_job_doc_fail;
    }

    // File signature (optional)
    if (aws_fields->signature != NULL && aws_fields->signatureLen > 0) {
        err = __make_string_copy(aws_fields->signature, aws_fields->signatureLen, &g_ota_state_ctx.current_job.ota_data.file_signature);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to make string copy for file signature");
            goto set_ota_data_from_job_doc_fail;
        }
    }

    // Firmware version (optional)
    if (custom_fields->fw_version != NULL && custom_fields->fw_version_len > 0) {
        err = __make_string_copy(custom_fields->fw_version, custom_fields->fw_version_len, &g_ota_state_ctx.current_job.ota_data.fw_version);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to make string copy for firmware version");
            goto set_ota_data_from_job_doc_fail;
        }
    }

    // File MD5 (optional)
    if (custom_fields->file_md5 != NULL && custom_fields->file_md5_len > 0) {
        err = __make_string_copy(custom_fields->file_md5, custom_fields->file_md5_len, &g_ota_state_ctx.current_job.ota_data.file_md5);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to make string copy for file MD5");
            goto set_ota_data_from_job_doc_fail;
        }
    }

    // Metadata (optional)
    if (custom_fields->metadata != NULL) {
        err = __make_string_copy(custom_fields->metadata, custom_fields->metadata_len, &g_ota_state_ctx.current_job.ota_data.metadata);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to make string copy for metadata");
            goto set_ota_data_from_job_doc_fail;
        }
    }

    return ESP_RMAKER_OK;

set_ota_data_from_job_doc_fail:
    clear_ota_data();
    return err;
}

static void clear_ota_data(void)
{
    if (g_ota_state_ctx.current_job.ota_data.stream_id != NULL) {
        free(g_ota_state_ctx.current_job.ota_data.stream_id);
        g_ota_state_ctx.current_job.ota_data.stream_id = NULL;
    }
    if (g_ota_state_ctx.current_job.ota_data.fw_version != NULL) {
        free(g_ota_state_ctx.current_job.ota_data.fw_version);
        g_ota_state_ctx.current_job.ota_data.fw_version = NULL;
    }
    if (g_ota_state_ctx.current_job.ota_data.file_md5 != NULL) {
        free(g_ota_state_ctx.current_job.ota_data.file_md5);
        g_ota_state_ctx.current_job.ota_data.file_md5 = NULL;
    }
    if (g_ota_state_ctx.current_job.ota_data.file_signature != NULL) {
        free(g_ota_state_ctx.current_job.ota_data.file_signature);
        g_ota_state_ctx.current_job.ota_data.file_signature = NULL;
    }
    if (g_ota_state_ctx.current_job.ota_data.ota_job_id != NULL) {
        free(g_ota_state_ctx.current_job.ota_data.ota_job_id);
        g_ota_state_ctx.current_job.ota_data.ota_job_id = NULL;
    }
    if (g_ota_state_ctx.current_job.ota_data.metadata != NULL) {
        free(g_ota_state_ctx.current_job.ota_data.metadata);
        g_ota_state_ctx.current_job.ota_data.metadata = NULL;
    }

    g_ota_state_ctx.current_job.ota_data.filesize = 0;
    g_ota_state_ctx.current_job.ota_data.file_id = 0;
}

/* Execution version operations */
static esp_rmaker_error_t execution_version_to_int32(const char *execution_version_str, size_t execution_version_len, int32_t *execution_version_num)
{
    *execution_version_num = 0;
    int32_t expected_version = 0;
    for (size_t i = 0; i < execution_version_len; i++) {
        char c = execution_version_str[i];
        if (c < '0' || c > '9') {
            OSAL_LOGE(TAG, "Invalid execution version string: %s", execution_version_str);
            return ESP_RMAKER_INVALID_ARG;
        }
        expected_version = expected_version * 10 + (c - '0');
    }
    *execution_version_num = expected_version;
    return ESP_RMAKER_OK;
}

/* Job ID queue operations */
static esp_rmaker_ota_error_reason_t pending_jobs_to_job_id_queue(const char *payload, size_t payload_len,
        const char *in_progress_jobid_array_path, const char *pending_jobid_array_path)
{
    /* Check arguments */
    if (in_progress_jobid_array_path == NULL || pending_jobid_array_path == NULL) {
        OSAL_LOGE(TAG, "Invalid job ID array paths: in_progress_jobid_array_path=%p, pending_jobid_array_path=%p", in_progress_jobid_array_path, pending_jobid_array_path);
        return OTA_ERROR_FATAL_UNEXPECTED_FORMAT;
    }

    /* Validate JSON */
    if (JSON_Validate(payload, payload_len) != JSONSuccess) {
        OSAL_LOGE(TAG, "Invalid JSON in GetPending response");
        return OTA_ERROR_GET_PENDING_INVALID_FORMAT;
    }

    /* Reset the pending job flag */
    g_ota_state_ctx.current_job.has_pending_job = false;

    /* Add to job ID queue */
    uint16_t idx = 0;
    const char *jobid_array_paths[2] = {in_progress_jobid_array_path, pending_jobid_array_path};
    for (size_t i = 0; i < 2; i++) {
        const char *jobid_array_path = jobid_array_paths[i];
        size_t jobid_array_len = 0;
        const char *jobid_array = NULL;
        JSONStatus_t json_ret;
        // Get the array of job descriptions
        json_ret = JSON_SearchConst(payload, payload_len,
                                    jobid_array_path, strlen(jobid_array_path), &jobid_array, &jobid_array_len, NULL);
        if (json_ret != JSONSuccess || jobid_array_len == 0) {
            continue;
        }

        // Validate the array
        if (JSON_Validate(jobid_array, jobid_array_len) != JSONSuccess) {
            OSAL_LOGE(TAG, "Invalid JSON array in %s: %.*s", jobid_array_path, (int)jobid_array_len, jobid_array);
            continue;
        }

        // Iterate over the array
        size_t start = 0, next = 0;
        JSONPair_t out_pair = {0};
        while (JSON_Iterate(jobid_array, jobid_array_len, &start, &next, &out_pair) == JSONSuccess) {
            // Search for job ID in the iterated value
            const char *jobid = NULL;
            size_t jobid_len = 0;
            json_ret = JSON_SearchConst(out_pair.value, out_pair.valueLength, "jobId", CONST_STRLEN("jobId"), &jobid, &jobid_len, NULL);
            if (json_ret != JSONSuccess || jobid_len == 0) {
                continue;
            }
            OSAL_LOGD(TAG, "Found job ID: %.*s", (int)jobid_len, jobid);

            // Copy to queue
            if (idx >= OTA_JOB_ID_QUEUE_SIZE) {
                OSAL_LOGW(TAG, "Job ID queue is full. Some job IDs will be dropped. Consider increasing the queue size.");
                break;
            }
            if (jobid_len > JOBID_MAX_LENGTH) {
                OSAL_LOGW(TAG, "Job ID too long: %.*s, ignoring", (int)jobid_len, jobid);
                continue;
            }
            memcpy(g_ota_job_id_queue.job_ids[idx], jobid, jobid_len);
            g_ota_job_id_queue.job_ids[idx][jobid_len] = '\0';
            idx++;
        }
    }

    /* Update job ID queue size */
    g_ota_job_id_queue.index.next = 0;
    g_ota_job_id_queue.index.size = idx;

    /* Return error code */
    return idx > 0 ? OTA_ERROR_NONE : OTA_ERROR_NO_PENDING_JOBS;
}

static const char *get_next_job_id_from_queue(void)
{
    if (g_ota_job_id_queue.index.next >= g_ota_job_id_queue.index.size) {
        return NULL;
    }
    return g_ota_job_id_queue.job_ids[g_ota_job_id_queue.index.next++];
}

#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
static void flush_job_id_queue(void)
{
    g_ota_job_id_queue.index.next = 0;
    g_ota_job_id_queue.index.size = 0;
}
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

/* State handlers */

static void on_event_ignored(const ota_job_event_data_t *event_data)
{
    /* Ignore NULL event data */
    if (!event_data) {
        return;
    }

    /* Handle events that need to be reported if ignored here */
    switch (event_data->event) {

    /* Fetch requested event: report to event loop */
    case OTA_JOB_EVENT_FETCH_REQUESTED:
        osal_event_post(RMAKER_OTA_EVENT, RMAKER_OTA_EVENT_FETCH_REQUEST_IGNORED, NULL, 0, OSAL_MAX_DELAY);
        break;

    /* Default: ignore */
    default:
        break;
    }
}

static ota_job_state_t handle_state_uninitialized(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    /* This state is handled during init */
    OSAL_LOGD(TAG, "In UNINITIALIZED state");
    on_event_ignored(event_data);
    return OTA_JOB_STATE_UNINITIALIZED;
}

static ota_job_state_t handle_state_network_init(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    /* Ignore all other events */
    if (!event_data || event_data->event != OTA_JOB_EVENT_EMPTY_TRANSITION) {
        on_event_ignored(event_data);
        return OTA_JOB_STATE_NETWORK_INIT;
    }

    ota_job_state_t next_state = OTA_JOB_STATE_NETWORK_INIT;
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Subscribe to jobs topics. Gate on sub_intended (not .subscribed) so we
     * don't double-call while awaiting a SUBACK. The SUBACK handler flips
     * .subscribed, and the retry context takes over if SUBACK fails or MQTT
     * reconnects. */
    if (!g_ota_state_ctx.sub_intended) {
        err = mqtt_subscribe_jobs_topics();
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGW(TAG, "Failed to subscribe to jobs topics");
            enter_error_state(OTA_ERROR_SUBSCRIPTION_FAILED);
            return OTA_JOB_STATE_ERROR;
        }
    }

    /* Decide the next state based on partition state */
    char *last_job_id = esp_rmaker_ota_get_last_job_id();
    bool transition_to_idle = last_job_id == NULL;
    if (!transition_to_idle) {
        next_state = OTA_JOB_STATE_REBOOT_CHECK;

        do {
            /* Copy the last job ID to the current job context */
            size_t last_job_id_len = strlen(last_job_id);
            if (last_job_id_len <= JOBID_MAX_LENGTH) {
                memcpy(g_ota_state_ctx.current_job.job_id, last_job_id, last_job_id_len);
                g_ota_state_ctx.current_job.job_id[last_job_id_len] = '\0';
            } else {
                OSAL_LOGE(TAG, "Last job ID too long: %d > %d", (int)last_job_id_len, JOBID_MAX_LENGTH);
                err = ESP_RMAKER_INVALID_STATE;
                break;
            }

            /* Copy the last expected version to the current job context */
            int32_t last_expected_version = esp_rmaker_ota_get_last_version();
            if (last_expected_version >= 0) {
                err = ota_status_set_initial_expected_version(g_ota_state_ctx.current_job.job_id, strlen(g_ota_state_ctx.current_job.job_id), last_expected_version);
                if (err != ESP_RMAKER_OK) {
                    OSAL_LOGE(TAG, "Failed to set initial expected version: %d", err);
                    memset(g_ota_state_ctx.current_job.job_id, 0, sizeof(g_ota_state_ctx.current_job.job_id));
                    err = ESP_RMAKER_INVALID_STATE;
                    break;
                }
            } else {
                OSAL_LOGE(TAG, "No last expected version found");
                memset(g_ota_state_ctx.current_job.job_id, 0, sizeof(g_ota_state_ctx.current_job.job_id));
                err = ESP_RMAKER_INVALID_STATE;
                break;
            }

            char *last_filetype = esp_rmaker_ota_get_last_filetype();
            if (last_filetype != NULL) {
                size_t last_filetype_len = strlen(last_filetype);
                if (last_filetype_len > OTA_FILETYPE_MAX_LENGTH) {
                    OSAL_LOGE(TAG, "Last filetype too long: %d > %d", (int)last_filetype_len, OTA_FILETYPE_MAX_LENGTH);
                    memset(g_ota_state_ctx.current_job.job_id, 0, sizeof(g_ota_state_ctx.current_job.job_id));
                    err = ESP_RMAKER_INVALID_STATE;
                    free(last_filetype);
                    break;
                }
                memcpy(g_ota_state_ctx.current_job.filetype, last_filetype, last_filetype_len);
                g_ota_state_ctx.current_job.filetype[last_filetype_len] = '\0';
                free(last_filetype);
            }
        } while (0);

        /* Clean up */
        free(last_job_id);

        /* Reset the invalid NVS state and transition to IDLE anyway if pre-reboot check setup failed */
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGW(TAG, "Pre-reboot check setup failed, resetting NVS state and transitioning to IDLE instead. The job may not have a final status.");
            esp_rmaker_ota_clear_nvs();
            transition_to_idle = true;
        }
    }

    if (transition_to_idle) {
        next_state = OTA_JOB_STATE_IDLE;

        /* Fetch happens on successful subscription; prime the state machine at IDLE */
        ota_job_event_data_t event_data = {
            .event = OTA_JOB_EVENT_EMPTY_TRANSITION,
            .payload = NULL,
        };
        err = ota_job_state_post_event(&event_data);
    }

    return next_state;
}

static ota_job_state_t handle_state_reboot_check(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    ota_job_state_t next_state = OTA_JOB_STATE_REBOOT_CHECK;

    if (!event_data) {
        return next_state;
    }

    esp_rmaker_error_t err;

    switch (event_data->event) {
    case OTA_JOB_EVENT_FINAL_STATUS_REPORT_REQUESTED:
        OSAL_LOGI(TAG, "REBOOT_CHECK: Final status report requested");

        /* Report the final status */
        const esp_rmaker_ota_status_details_t *final_status_details = (const esp_rmaker_ota_status_details_t *)event_data->payload->data;
        if (!final_status_details) {
            OSAL_LOGE(TAG, "Status details is NULL");
            enter_error_state(OTA_ERROR_FATAL_UNEXPECTED_FORMAT);
            return OTA_JOB_STATE_ERROR;
        }
        err = mqtt_publish_final_status(final_status_details);
        if (err == ESP_RMAKER_INVALID_ARG) {
            OSAL_LOGE(TAG, "Critical error: OTA status manager got invalid argument(s), terminating state machine");
            enter_error_state(OTA_ERROR_FATAL_UNEXPECTED_FORMAT);
            return OTA_JOB_STATE_ERROR;
        } else if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to publish final status");
            /* Set recovery context to point to back here if we need to retry */
            *should_free_event = false;
            enter_error_state_custom(OTA_ERROR_RETRY_WITH_BACKOFF, OTA_JOB_STATE_REBOOT_CHECK, event_data);
            return OTA_JOB_STATE_ERROR;
        }

        /* Set the expected status details */
        g_ota_state_ctx.current_job.current_status_details = *final_status_details;
        break;

    case OTA_JOB_EVENT_REBOOT_CHECK_REQUESTED:
        OSAL_LOGI(TAG, "REBOOT_CHECK: Reboot check requested");

        /* Custom filetype: skip NVS checks - users are to report the status themselves */
        if (g_ota_state_ctx.current_job.filetype[0] != '\0') {
            OSAL_LOGI(TAG, "REBOOT_CHECK: Custom filetype, waiting for final status report via esp_rmaker_ota_report_final_status()");
            break;
        }

        /* Get the status flags from NVS */
        esp_rmaker_ota_nvs_flags_t status_flags;
        err = esp_rmaker_ota_nvs_get_status(&status_flags);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to get status flags from NVS. Will report reboot check failed.");
            status_flags.is_checked = true;
            status_flags.is_passed = false;
        }

        /* Send an update, current state will receive accepted/rejected events */
        if (status_flags.is_checked) {
            // report status
            esp_rmaker_ota_status_details_t *status_details = &g_ota_state_ctx.current_job.current_status_details;
            JobCurrentStatus_t status;
            if (status_flags.is_passed) {
                status = Succeeded;
                esp_rmaker_ota_status_details_fill_succeeded(status_details, NULL, osal_sysinfo_get_fw_version());
            } else {
                status = Failed;
                esp_rmaker_ota_status_details_fill_failed(status_details, "Reboot check failed");
            }
            g_ota_state_ctx.current_job.current_status = status;
            err = ota_jobs_mqtt_publish_update_job_status(status, status_details);
            if (err == ESP_RMAKER_INVALID_ARG) {
                OSAL_LOGE(TAG, "Critical error: OTA status manager got invalid argument(s), terminating state machine");
                enter_error_state(OTA_ERROR_FATAL_UNEXPECTED_FORMAT);
                return OTA_JOB_STATE_ERROR;
            } else if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to publish update job status");
                *should_free_event = false;
                enter_error_state_custom(OTA_ERROR_RETRY_WITH_BACKOFF, OTA_JOB_STATE_REBOOT_CHECK, event_data);
                return OTA_JOB_STATE_ERROR;
            }
        }
        break;

    case OTA_JOB_EVENT_UPDATE_ACCEPTED:
        OSAL_LOGI(TAG, "REBOOT_CHECK: Update accepted, transitioning to IDLE");

        /* Custom filetype: skip reset and stay in this state if final status not reported yet */
        if (g_ota_state_ctx.current_job.filetype[0] != '\0') {
            if (g_ota_state_ctx.current_job.final_status_reported) {
                /* Final status reported: reset the filetype */
                memset(g_ota_state_ctx.current_job.filetype, 0, sizeof(g_ota_state_ctx.current_job.filetype));
            } else {
                /* Final status not reported yet: delay reset */
                OSAL_LOGW(TAG, "REBOOT_CHECK: Custom filetype, final status not reported yet, delaying reset");
                break;
            }
        }

        /* Suitable error backoff reset point */
        retry_reset_backoff();

        /* Reset the job ID, filetype, expected version and NVS state */
        ota_status_clear_job_entries(g_ota_state_ctx.current_job.job_id, strlen(g_ota_state_ctx.current_job.job_id));
        memset(g_ota_state_ctx.current_job.job_id, 0, sizeof(g_ota_state_ctx.current_job.job_id));
        memset(g_ota_state_ctx.current_job.filetype, 0, sizeof(g_ota_state_ctx.current_job.filetype));
        g_ota_state_ctx.current_job.final_status_reported = false;
        esp_rmaker_ota_clear_nvs();

        /* Post the success/failure event to the event loop */
        esp_rmaker_ota_event_t event;
        esp_rmaker_ota_status_details_type_t expected_status_details_type;
        switch (g_ota_state_ctx.current_job.current_status) {
        case Succeeded:
            event = RMAKER_OTA_EVENT_SUCCESSFUL;
            expected_status_details_type = ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_SUCCEEDED;
            break;
        case Failed:
            event = RMAKER_OTA_EVENT_FAILED;
            expected_status_details_type = ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_FAILED;
            break;
        default:
            OSAL_LOGE(TAG, "Unexpected current status: %d", (int)g_ota_state_ctx.current_job.current_status);
            enter_error_state(OTA_ERROR_FATAL_UNEXPECTED_FORMAT);
            return OTA_JOB_STATE_ERROR;
        }
        if (g_ota_state_ctx.current_job.current_status_details.type != expected_status_details_type) {
            OSAL_LOGE(TAG, "Expected status details type: %" PRIu16 ", but got: %" PRIu16, (uint16_t)expected_status_details_type, (uint16_t)g_ota_state_ctx.current_job.current_status_details.type);
            enter_error_state(OTA_ERROR_FATAL_UNEXPECTED_FORMAT);
            return OTA_JOB_STATE_ERROR;
        }
        osal_event_post(RMAKER_OTA_EVENT, event, &g_ota_state_ctx.current_job.current_status_details, sizeof(esp_rmaker_ota_status_details_t), OSAL_MAX_DELAY);

        /* Transition to IDLE */
        next_state = OTA_JOB_STATE_IDLE;
        break;

    case OTA_JOB_EVENT_UPDATE_REJECTED:
        /* Recoverable rejects (throttling, version mismatch, transient broker errors)
         * are retried by the status manager, so keep waiting for the acceptance. */
        if (event_data->fixed_data == NULL) {
            OSAL_LOGW(TAG, "REBOOT_CHECK: Update rejected but recoverable, awaiting retry");
            break;
        }

        /* Unrecoverable: the job execution is gone or already terminal, so its final
         * status can never be reported. Without this exit the state machine would park
         * here forever - every fetch request is ignored in this state, so the device
         * would never pick up any later job, and the stale job in NVS would bring it
         * straight back here on every subsequent boot. */
        OSAL_LOGW(TAG, "REBOOT_CHECK: Update rejected unrecoverably for job '%s', abandoning it and transitioning to IDLE",
                  g_ota_state_ctx.current_job.job_id);

        /* Suitable error backoff reset point */
        retry_reset_backoff();

        /* Drop the job and its persisted reboot-check state so the next boot starts clean */
        ota_status_clear_job_entries(g_ota_state_ctx.current_job.job_id, strlen(g_ota_state_ctx.current_job.job_id));
        memset(g_ota_state_ctx.current_job.job_id, 0, sizeof(g_ota_state_ctx.current_job.job_id));
        memset(g_ota_state_ctx.current_job.filetype, 0, sizeof(g_ota_state_ctx.current_job.filetype));
        g_ota_state_ctx.current_job.final_status_reported = false;
        esp_rmaker_ota_clear_nvs();

        /* Report to the application: the update was applied locally, but the cloud will
         * never record its outcome for this job. */
        esp_rmaker_ota_status_details_fill_rejected(&g_ota_state_ctx.current_job.current_status_details,
                ESP_RMAKER_OTA_REJECTED_REASON_JOB_UPDATE_UNRECOVERABLE);
        osal_event_post(RMAKER_OTA_EVENT, RMAKER_OTA_EVENT_REJECTED, &g_ota_state_ctx.current_job.current_status_details,
                        sizeof(esp_rmaker_ota_status_details_t), OSAL_MAX_DELAY);

        /* Transition to IDLE */
        next_state = OTA_JOB_STATE_IDLE;
        break;

    default:
        on_event_ignored(event_data);
        break;
    }

    return next_state;
}

static ota_job_state_t handle_state_idle(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    ota_job_state_t next_state = OTA_JOB_STATE_IDLE;

    if (!event_data) {
        return next_state;
    }

    switch (event_data->event) {
    case OTA_JOB_EVENT_FETCH_REQUESTED:
        OSAL_LOGI(TAG, "IDLE: Fetch requested, transitioning to FETCHING_PENDING_JOBS");
        if (enqueue_transition_event() != ESP_RMAKER_OK) {
            enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            next_state = OTA_JOB_STATE_ERROR;
            break;
        }
        next_state = OTA_JOB_STATE_FETCHING_PENDING_JOBS;
        break;
    case OTA_JOB_EVENT_JOBS_CHANGED:
        OSAL_LOGI(TAG, "IDLE: Jobs changed, transitioning to JOBS_CHANGED");
        /* Ownership passed to enqueue_event; it frees on failure. */
        *should_free_event = false;
        if (enqueue_event(event_data, false) != ESP_RMAKER_OK) {
            enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            next_state = OTA_JOB_STATE_ERROR;
            break;
        }
        next_state = OTA_JOB_STATE_JOBS_CHANGED;
        break;
    default:
        on_event_ignored(event_data);
        break;
    }

    return next_state;
}

static ota_job_state_t handle_state_jobs_changed(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    esp_rmaker_ota_error_reason_t error = OTA_ERROR_NONE;
    ota_job_state_t next_state = OTA_JOB_STATE_JOBS_CHANGED;

    /* Parse job list from payload */
    if (!event_data || event_data->event != OTA_JOB_EVENT_JOBS_CHANGED) {
        // ignore other events
        on_event_ignored(event_data);
        return next_state;
    }
    if (!event_data->payload || !event_data->payload->data) {
        OSAL_LOGW(TAG, "No payload in JOBS_CHANGED, treating as no pending jobs");
        return OTA_JOB_STATE_IDLE;
    }

    const char *payload = event_data->payload->data;
    size_t payload_len = event_data->payload->len;

    OSAL_LOGD(TAG, "Jobs changed: %.*s", (int)payload_len, payload);

    error = pending_jobs_to_job_id_queue(payload, payload_len,
                                         "jobs.IN_PROGRESS", "jobs.QUEUED");

    if (error == OTA_ERROR_NONE) {
        if (enqueue_transition_event() != ESP_RMAKER_OK) {
            enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            next_state = OTA_JOB_STATE_ERROR;
        } else {
            next_state = OTA_JOB_STATE_FETCHING_JOB_DOC;
        }
    } else if (error == OTA_ERROR_NO_PENDING_JOBS) {
        next_state = OTA_JOB_STATE_IDLE;
    } else {
        enter_error_state(error);
        next_state = OTA_JOB_STATE_ERROR;
    }

    return next_state;
}

static ota_job_state_t handle_state_fetching_pending_jobs(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    ota_job_state_t next_state = OTA_JOB_STATE_FETCHING_PENDING_JOBS;

    if (!event_data || event_data->event != OTA_JOB_EVENT_EMPTY_TRANSITION) {
        // ignore other events
        on_event_ignored(event_data);
        return next_state;
    }

    /* Enter action: Publish GetPending request */
    OSAL_LOGI(TAG, "FETCHING_PENDING_JOBS: Publishing GetPending request");

    esp_rmaker_error_t err = mqtt_publish_get_pending();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to publish GetPending request");
        enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
        return next_state;
    }

    /* Start response timer */
    start_response_timer();

    /* Move to waiting state */
    OSAL_LOGI(TAG, "Waiting for GetPending response...");
    next_state = OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS;

    return next_state;
}

static ota_job_state_t handle_state_waiting_for_pending_jobs(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    ota_job_state_t next_state = OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS;

    if (!event_data) {
        return next_state;
    }

    ota_job_event_t event = event_data->event;

    if (event == OTA_JOB_EVENT_PENDING_JOBS_ACCEPTED) {
        stop_response_timer();
        *should_free_event = false;
        if (enqueue_event(event_data, false) != ESP_RMAKER_OK) {
            enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            return OTA_JOB_STATE_ERROR;
        }
        next_state = OTA_JOB_STATE_PENDING_JOBS_RECEIVED;
    } else if (event == OTA_JOB_EVENT_PENDING_JOBS_REJECTED) {
        stop_response_timer();
        OSAL_LOGE(TAG, "GetPending request rejected");
        enter_error_state(OTA_ERROR_GET_PENDING_REJECTED);
    } else if (event == OTA_JOB_EVENT_TIMEOUT) {
        OSAL_LOGE(TAG, "Timeout waiting for GetPending response");
        enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
    } else {
        on_event_ignored(event_data);
    }

    return next_state;
}

static ota_job_state_t handle_state_pending_jobs_received(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    esp_rmaker_ota_error_reason_t error = OTA_ERROR_NONE;
    ota_job_state_t next_state = OTA_JOB_STATE_PENDING_JOBS_RECEIVED;

    /* Parse job list from payload */
    if (!event_data || event_data->event != OTA_JOB_EVENT_PENDING_JOBS_ACCEPTED) {
        // ignore other events
        on_event_ignored(event_data);
        return next_state;
    }
    if (!event_data->payload || !event_data->payload->data) {
        OSAL_LOGE(TAG, "No payload in PENDING_JOBS_RECEIVED");
        enter_error_state(OTA_ERROR_GET_PENDING_INVALID_FORMAT);
        return next_state;
    }

    /* Suitable error backoff reset point */
    retry_reset_backoff();

    const char *payload = event_data->payload->data;
    size_t payload_len = event_data->payload->len;

    OSAL_LOGD(TAG, "Pending jobs received: %.*s", (int)payload_len, payload);

    error = pending_jobs_to_job_id_queue(payload, payload_len,
                                         "inProgressJobs", "queuedJobs");

    if (error == OTA_ERROR_NONE) {
        OSAL_LOGI(TAG, "%" PRIu16 " pending jobs found", g_ota_job_id_queue.index.size);
        if (enqueue_transition_event() != ESP_RMAKER_OK) {
            enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            return OTA_JOB_STATE_ERROR;
        }
        next_state = OTA_JOB_STATE_FETCHING_JOB_DOC;
    } else if (error == OTA_ERROR_NO_PENDING_JOBS) {
        OSAL_LOGI(TAG, "No pending jobs found");
        next_state = OTA_JOB_STATE_IDLE;
    } else {
        enter_error_state(error);
        next_state = OTA_JOB_STATE_ERROR;
    }

    return next_state;
}

static ota_job_state_t handle_state_fetching_job_doc(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    ota_job_state_t next_state = OTA_JOB_STATE_FETCHING_JOB_DOC;

    if (!event_data || event_data->event != OTA_JOB_EVENT_EMPTY_TRANSITION) {
        // ignore other events
        on_event_ignored(event_data);
        return next_state;
    }

    /* Get next job ID from queue */
    const char *job_id = get_next_job_id_from_queue();

    /* No more job IDs to fetch */
    if (!job_id) {
        if (!g_ota_state_ctx.current_job.has_pending_job) {
            // No more job IDs to fetch and no pending job
            OSAL_LOGI(TAG, "FETCHING_JOB_DOC: No more job IDs to fetch and no pending job");
            next_state = OTA_JOB_STATE_IDLE;
            return next_state;
        } else {
            // Execute this job
            OSAL_LOGI(TAG, "FETCHING_JOB_DOC: Executing job: %s", g_ota_state_ctx.current_job.job_id);
            if (enqueue_transition_event() != ESP_RMAKER_OK) {
                enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
                return OTA_JOB_STATE_ERROR;
            }
            next_state = OTA_JOB_STATE_JOB_EXECUTION;
            return next_state;
        }
    }

    /* Enter action: Publish DescribeJobExecution request */
    OSAL_LOGI(TAG, "FETCHING_JOB_DOC: Publishing DescribeJob for job: %s", job_id);

    esp_rmaker_error_t err = mqtt_publish_describe_job(job_id);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to publish DescribeJob request");
        enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
        return next_state;
    }

    /* Start response timer */
    start_response_timer();

    /* Move to waiting state */
    OSAL_LOGI(TAG, "Waiting for DescribeJob response...");
    next_state = OTA_JOB_STATE_WAITING_FOR_JOB_DOC;

    return next_state;
}

static ota_job_state_t handle_state_waiting_for_job_doc(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    ota_job_state_t next_state = OTA_JOB_STATE_WAITING_FOR_JOB_DOC;

    if (!event_data) {
        return next_state;
    }

    ota_job_event_t event = event_data->event;

    if (event == OTA_JOB_EVENT_JOB_DOC_ACCEPTED) {
        stop_response_timer();
        *should_free_event = false;
        if (enqueue_event(event_data, false) != ESP_RMAKER_OK) {
            enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            return OTA_JOB_STATE_ERROR;
        }
        next_state = OTA_JOB_STATE_JOB_DOC_RECEIVED;
    } else if (event == OTA_JOB_EVENT_JOB_DOC_REJECTED) {
        stop_response_timer();
        OSAL_LOGE(TAG, "DescribeJob request rejected");
        enter_error_state(OTA_ERROR_DESCRIBE_JOB_REJECTED);
    } else if (event == OTA_JOB_EVENT_TIMEOUT) {
        OSAL_LOGE(TAG, "Timeout waiting for DescribeJob response");
        enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
    } else {
        on_event_ignored(event_data);
    }

    return next_state;
}

static ota_job_state_t handle_state_job_doc_received(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    esp_rmaker_ota_error_reason_t error = OTA_ERROR_NONE;
    ota_job_state_t next_state = OTA_JOB_STATE_JOB_DOC_RECEIVED;
    esp_rmaker_ota_status_details_t status_details_rejected = {0};

    if (!event_data || event_data->event != OTA_JOB_EVENT_JOB_DOC_ACCEPTED) {
        // ignore other events
        on_event_ignored(event_data);
        return next_state;
    }

    /* Suitable error backoff reset point */
    retry_reset_backoff();

    /* Parse job document from payload */
    if (!event_data || !event_data->payload || !event_data->payload->data) {
        OSAL_LOGE(TAG, "No payload in JOB_DOC_RECEIVED, ignoring");
        goto handle_state_job_doc_received_end;
    }

    const char *payload = event_data->payload->data;
    size_t payload_len = event_data->payload->len;

    OSAL_LOGD(TAG, "Job document received: %.*s", (int)payload_len, payload);

    /* Extract job ID */
    const char *job_id = NULL;
    size_t job_id_len = Jobs_GetJobId(payload, payload_len, &job_id);

    if (job_id_len == 0 || !job_id || job_id_len > JOBID_MAX_LENGTH) {
        OSAL_LOGE(TAG, "Failed to extract job ID or invalid length (%" PRIu32 " > %" PRIu32 "), ignoring", (uint32_t)job_id_len, (uint32_t)JOBID_MAX_LENGTH);
        goto handle_state_job_doc_received_end;
    }

    /* Extract job document */
    const char *job_doc = NULL;
    size_t job_doc_len = Jobs_GetJobDocument(payload, payload_len, &job_doc);

    if (job_doc_len == 0 || !job_doc) {
        OSAL_LOGE(TAG, "Failed to extract job document, ignoring");
        goto handle_state_job_doc_received_end;
    }

    OSAL_LOGI(TAG, "Job document received for job ID: %.*s (%" PRIu32 " bytes)", (int)job_id_len, job_id, (uint32_t)job_doc_len);
    OSAL_LOGD(TAG, "Job doc: %.*s", (int)job_doc_len, job_doc);

    /* Get execution version number from payload */
    const char *execution_version = NULL;
    size_t execution_version_len = 0;
    JSONTypes_t execution_version_type = JSONNull;
    JSONStatus_t json_status = JSON_SearchConst(payload, payload_len,
                               "execution.versionNumber",
                               CONST_STRLEN("execution.versionNumber"),
                               &execution_version, &execution_version_len, &execution_version_type);
    if (json_status != JSONSuccess || execution_version_len == 0 || execution_version_type != JSONNumber) {
        OSAL_LOGW(TAG, "Failed to get execution version number from payload: JSON status: %d, execution version length: %" PRIu32 ", execution version type: %d", json_status, (uint32_t)execution_version_len, execution_version_type);
    }
    int32_t execution_version_num = 0;
    err = execution_version_to_int32(execution_version, execution_version_len, &execution_version_num);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to convert execution version '%s' to uint32, ignoring: %d", execution_version, err);
        goto handle_state_job_doc_received_end;
    }

    /* Parse custom fields from job document */
    ota_job_info_custom_fields_t custom_fields;
    err = parse_custom_fields_from_job_doc(job_doc, job_doc_len, &custom_fields);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to parse custom fields from job document, invalid format");
        esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_JOB_DOC_MISSING_RMNG);
        goto handle_state_job_doc_received_invalid_format;
    }

    /* Get the filetype handler */
    const esp_rmaker_ota_ft_ctx_t *ft_ctx = NULL;
    g_ota_state_ctx.current_job.filetype[0] = '\0'; // Set to empty string
    if (custom_fields.filetype != NULL && custom_fields.filetype_len > 0) {
        /* Copy the filetype to the current job */
        if (custom_fields.filetype_len > OTA_FILETYPE_MAX_LENGTH) {
            OSAL_LOGE(TAG, "Filetype length %" PRIu32 " is too long, maximum length is %" PRIu32, (uint32_t)custom_fields.filetype_len, (uint32_t)OTA_FILETYPE_MAX_LENGTH);
            esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_FILETYPE_TOO_LONG);
            goto handle_state_job_doc_received_reject;
        }
        memcpy(g_ota_state_ctx.current_job.filetype, custom_fields.filetype, custom_fields.filetype_len);
        g_ota_state_ctx.current_job.filetype[custom_fields.filetype_len] = '\0';

        /* Lookup the filetype handler */
        esp_rmaker_ota_ft_lookup_handler_t filetype_handler_lookup = g_ota_state_ctx.filetype_handler_lookup;
        if (filetype_handler_lookup == NULL) {
            OSAL_LOGE(TAG, "Filetype handler lookup function not set");
            esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_NO_CUSTOM_FILETYPE_IMPL);
            goto handle_state_job_doc_received_reject;
        }
        ft_ctx = filetype_handler_lookup(custom_fields.filetype, custom_fields.filetype_len);
        if (ft_ctx == NULL) {
            OSAL_LOGE(TAG, "Failed to get filetype handler for filetype '%.*s', rejecting", (int)custom_fields.filetype_len, custom_fields.filetype);
            esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_FILETYPE_NOT_SUPPORTED);
            goto handle_state_job_doc_received_reject;
        }
        if (!filetype_handler_is_valid_ctx(ft_ctx)) {
            OSAL_LOGE(TAG, "Filetype handler is invalid for filetype '%.*s', rejecting", (int)custom_fields.filetype_len, custom_fields.filetype);
            esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_FILETYPE_HANDLER_INVALID);
            goto handle_state_job_doc_received_reject;
        }
    } else {
        /* Use the default filetype handler */
        ft_ctx = filetype_handler_get_default_ctx();
    }
    g_ota_state_ctx.current_job.filetype_handler = ft_ctx;

    /* Compare the firmware version of the job document with the current firmware version */
    uint32_t fw_ver_job_doc_num = 0; // By default, set to 0
    if (ft_ctx->get_version != NULL) {
        if (custom_fields.fw_version == NULL || custom_fields.fw_version_len == 0) {
            OSAL_LOGE(TAG, "Custom fields: firmware version is required if get_version handler is provided");
            esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_FW_VERSION_REQUIRED);
            goto handle_state_job_doc_received_reject;
        }
        esp_rmaker_ota_ft_version_t fw_ver_current = {0}, fw_ver_job_doc = {
            .str = custom_fields.fw_version,
            .len = custom_fields.fw_version_len,
        };
        err = ft_ctx->get_version(&fw_ver_current);
        OSAL_LOGI(TAG, "Current firmware version: %.*s", (int)fw_ver_current.len, fw_ver_current.str);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to get current firmware version");
            esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_FW_VERSION_UNSUPPORTED);
            goto handle_state_job_doc_received_reject;
        }
        uint32_t fw_ver_current_num = 0;
        err = ft_ctx->version_to_uint32(fw_ver_current, &fw_ver_current_num);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to convert current firmware version to uint32");
            esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_FW_VERSION_UNSUPPORTED);
            goto handle_state_job_doc_received_reject;
        }
        err = ft_ctx->version_to_uint32(fw_ver_job_doc, &fw_ver_job_doc_num);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to convert job document firmware version to uint32, rejecting");
            esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_FW_VERSION_UNSUPPORTED);
            goto handle_state_job_doc_received_reject;
        }
        if (fw_ver_job_doc_num <= fw_ver_current_num) {
            OSAL_LOGW(TAG, "Job document firmware version '%.*s' is lower than or equal to current firmware version, rejecting", (int)fw_ver_job_doc.len, fw_ver_job_doc.str);
            esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_FW_VERSION_TOO_LOW);
            goto handle_state_job_doc_received_reject;
        }

        /* Compare the minimum firmware version of the job document with the current firmware version */
        if (custom_fields.min_fw_version != NULL && custom_fields.min_fw_version_len > 0) {
            esp_rmaker_ota_ft_version_t min_fw_version = {
                .str = custom_fields.min_fw_version,
                .len = custom_fields.min_fw_version_len,
            };
            uint32_t min_fw_version_num = 0;
            err = ft_ctx->version_to_uint32(min_fw_version, &min_fw_version_num);
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to convert minimum firmware version to uint32");
                goto handle_state_job_doc_received_end;
            }
            if (min_fw_version_num > fw_ver_current_num) {
                OSAL_LOGW(TAG, "Minimum firmware version '%.*s' is higher than current firmware version, ignoring", (int)custom_fields.min_fw_version_len, custom_fields.min_fw_version);
                goto handle_state_job_doc_received_end;
            }
        }
    }

#if CONFIG_RMNG_OTA_TIME_SUPPORT
    /* Ignore if download window exists and is not valid */
    if (has_download_window(&custom_fields.download_window)) {
        int32_t delay_time = get_download_window_delay_time(&custom_fields.download_window);
        if (delay_time != 0) {
            OSAL_LOGW(TAG, "Download window is not valid, ignoring");
            goto handle_state_job_doc_received_end;
        }
    }
#endif /* CONFIG_RMNG_OTA_TIME_SUPPORT */

    /* Ignore if this job's firmware version is lower than the best current job found so far */
    if (g_ota_state_ctx.current_job.has_pending_job && fw_ver_job_doc_num < g_ota_state_ctx.current_job.fw_version) {
        goto handle_state_job_doc_received_end;
    }

    /* Parse OTA job document */
    AfrOtaJobDocumentFields_t fields = {0};
    int8_t file_index = otaParser_parseJobDocFile(job_doc, job_doc_len, 0, &fields);

    if (file_index < 0) {
        OSAL_LOGE(TAG, "Failed to parse job document, invalid format");
        esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_JOB_DOC_MISSING_AFR_OTA);
        goto handle_state_job_doc_received_invalid_format;
    }

    /* Validate the image reference (MQTT stream id) before any download
     * setup runs. Catches refs that would otherwise overflow downstream buffers (e.g.
     * AWS MQTT file-downloader's fixed STREAM_NAME_MAX_LEN topic buffer). */
    if (g_ota_state_ctx.image_download.validate_image_ref != NULL) {
        esp_rmaker_error_t v_err = g_ota_state_ctx.image_download.validate_image_ref(
                                       fields.imageRef, fields.imageRefLen);
        if (v_err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Image reference rejected by transport validator (len=%d)", (int)fields.imageRefLen);
            esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_IMAGE_REFERENCE_INVALID);
            goto handle_state_job_doc_received_reject;
        }
    }

#if CONFIG_RMNG_OTA_SIGNATURE_VERIFY_ENABLE
    /* Make sure signature exists */
    if (fields.signature == NULL || fields.signatureLen == 0) {
        OSAL_LOGE(TAG, "OTA signature verification enabled but job document has no signature");
        esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_SIGNATURE_MISSING);
        /* Reject instead of going into custom job execution, because the rest of the document is valid */
        goto handle_state_job_doc_received_reject;
    }
    /* Attempt to parse the signature as base64 */
    size_t signature_len = 0;
    uint8_t *signature = esp_rmaker_convert_base64_to_bytes(fields.signature, fields.signatureLen, &signature_len);
    if (signature == NULL) {
        OSAL_LOGE(TAG, "Failed to convert signature to bytes (invalid base64)");
        esp_rmaker_ota_status_details_fill_rejected(&status_details_rejected, ESP_RMAKER_OTA_REJECTED_REASON_SIGNATURE_INVALID_BASE64);
        /* Reject instead of going into custom job execution, because the rest of the document is valid */
        goto handle_state_job_doc_received_reject;
    }
    free(signature);
#endif /* CONFIG_RMNG_OTA_SIGNATURE_VERIFY_ENABLE */

    /* Set OTA data from job document */
    err = set_ota_data_from_job_doc(&fields, &custom_fields);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to set OTA data from job document");
        error = OTA_ERROR_JOB_DOC_PARSE_FAILED;
        goto handle_state_job_doc_received_end;
    }

    /* Mark this job as the best current job found so far */
    g_ota_state_ctx.current_job.has_pending_job = true;
    g_ota_state_ctx.current_job.fw_version = fw_ver_job_doc_num;
    memcpy(&g_ota_state_ctx.current_job.job_id, job_id, job_id_len);
    g_ota_state_ctx.current_job.job_id[job_id_len] = '\0';
    goto handle_state_job_doc_received_end;

handle_state_job_doc_received_invalid_format:
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
    if (g_ota_state_ctx.custom_job_cb != NULL) {
        /* Execute custom job immediately; flush the job ID queue and copy job ID */
        memcpy(&g_ota_state_ctx.current_job.job_id, job_id, job_id_len);
        g_ota_state_ctx.current_job.job_id[job_id_len] = '\0';
        flush_job_id_queue();

        /* Copy the entire job document as the event data */
        ota_job_event_data_payload_t payload = {
            .data = (void *)job_doc,
            .len = job_doc_len,
        };
        ota_job_event_data_t event_data = {
            .event = OTA_JOB_EVENT_CUSTOM_JOB_EXECUTION_REQUESTED,
            .payload = &payload,
        };

        /* Set the state to CUSTOM_JOB_EXECUTION */
        if (enqueue_event(&event_data, true) != ESP_RMAKER_OK) {
            enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            return OTA_JOB_STATE_ERROR;
        }
        return OTA_JOB_STATE_CUSTOM_JOB_EXECUTION;
    }

    /* With no callback, proceed to report the job document as invalid format */
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */
handle_state_job_doc_received_reject:
    err = ota_status_set_initial_expected_version(job_id, job_id_len, execution_version_num);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to set initial expected version: %d", err);
        error = OTA_ERROR_FATAL_UNEXPECTED_FORMAT;
        goto handle_state_job_doc_received_end;
    }
    err = mqtt_publish_update_job_status(job_id, job_id_len, Rejected, &status_details_rejected, NULL);
    if (err == ESP_RMAKER_INVALID_ARG) {
        OSAL_LOGE(TAG, "Critical error: OTA status manager got invalid argument(s), terminating state machine");
        error = OTA_ERROR_FATAL_UNEXPECTED_FORMAT;
        goto handle_state_job_doc_received_end;
    } else if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to publish update job status: %d", err);
        *should_free_event = false;
        enter_error_state_custom(OTA_ERROR_RETRY_WITH_BACKOFF, OTA_JOB_STATE_JOB_DOC_RECEIVED, event_data);
        return OTA_JOB_STATE_ERROR;
    }
handle_state_job_doc_received_end:
    if (error != OTA_ERROR_NONE) {
        enter_error_state(error);
        next_state = OTA_JOB_STATE_ERROR;
    } else if (enqueue_transition_event() != ESP_RMAKER_OK) {
        enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
        next_state = OTA_JOB_STATE_ERROR;
    } else {
        next_state = OTA_JOB_STATE_FETCHING_JOB_DOC;
    }
    return next_state;
}

#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
static ota_job_state_t handle_state_custom_job_execution(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    if (!event_data) {
        return OTA_JOB_STATE_CUSTOM_JOB_EXECUTION;
    }

    ota_job_state_t next_state = OTA_JOB_STATE_CUSTOM_JOB_EXECUTION;
    JobCurrentStatus_t status;
    esp_rmaker_error_t err;

    switch (event_data->event) {
    case OTA_JOB_EVENT_CUSTOM_JOB_EXECUTION_REQUESTED: {
        /* Execute custom job */
        const char *job_doc = (const char *)event_data->payload->data;
        size_t job_doc_len = event_data->payload->len;
        err = g_ota_state_ctx.custom_job_cb(job_doc, job_doc_len);
        if (err == ESP_RMAKER_OK) {
            /* Custom job executed successfully, wait for next event */
            return OTA_JOB_STATE_CUSTOM_JOB_EXECUTION;
        }

        /* Automatic error handling */
        ota_job_event_data_payload_t payload = {
            .data = NULL,
            .len = 0,
        };
        ota_job_event_data_t status_event_data = {
            .event = OTA_JOB_EVENT_CUSTOM_JOB_FAILED,
            .payload = &payload,
        };
        if (err == ESP_RMAKER_INVALID_ARG) {
            status_event_data.event = OTA_JOB_EVENT_CUSTOM_JOB_REJECTED;
            payload.data = "{\"reason\":\"" ESP_RMAKER_OTA_REJECTED_REASON_INVALID_CUSTOM_JOB_DOCUMENT "\"}";
            payload.len = CONST_STRLEN("{\"reason\":\"" ESP_RMAKER_OTA_REJECTED_REASON_INVALID_CUSTOM_JOB_DOCUMENT "\"}");
        } else {
            status_event_data.event = OTA_JOB_EVENT_CUSTOM_JOB_FAILED;
            payload.data = "{\"reason\":\"" ESP_RMAKER_OTA_FAILED_REASON_FAILED_TO_PROCESS_CUSTOM_JOB_DOCUMENT "\"}";
            payload.len = CONST_STRLEN("{\"reason\":\"" ESP_RMAKER_OTA_FAILED_REASON_FAILED_TO_PROCESS_CUSTOM_JOB_DOCUMENT "\"}");
        }
        if (enqueue_event(&status_event_data, true) != ESP_RMAKER_OK) {
            enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            return OTA_JOB_STATE_ERROR;
        }
        break;
    }
    case OTA_JOB_EVENT_CUSTOM_JOB_PROGRESS:
        /* Report custom job progress */
        status = InProgress;
        goto handle_state_custom_job_execution_report;
    case OTA_JOB_EVENT_CUSTOM_JOB_SUCCEEDED:
        /* Report custom job succeeded */
        status = Succeeded;
        goto handle_state_custom_job_execution_report;
    case OTA_JOB_EVENT_CUSTOM_JOB_FAILED:
        /* Report custom job failed */
        status = Failed;
        goto handle_state_custom_job_execution_report;
    case OTA_JOB_EVENT_CUSTOM_JOB_REJECTED:
        /* Report custom job rejected */
        status = Rejected;
        goto handle_state_custom_job_execution_report;
    case OTA_JOB_EVENT_UPDATE_ACCEPTED:
        if (!ota_status_is_cache_empty()) {
            /* Still waiting for updates to be handled */
            OSAL_LOGW(TAG, "CUSTOM_JOB_EXECUTION: Still waiting for remaining updates to be handled");
            break;
        }

        if (!g_ota_state_ctx.current_job.final_status_reported) {
            /* Final status not reported yet: delay reset */
            OSAL_LOGW(TAG, "CUSTOM_JOB_EXECUTION: Final status not reported yet, delaying reset");
            break;
        }

        /* Suitable error backoff reset point */
        retry_reset_backoff();

        /* Request a fetch */
        err = ota_job_state_fetch();
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to fetch jobs: %d", err);
            enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            return OTA_JOB_STATE_ERROR;
        }
        next_state = OTA_JOB_STATE_IDLE;
        break;
    default:
        on_event_ignored(event_data);
        break;
    }

    return next_state;

handle_state_custom_job_execution_report:;
    const char *status_details_json = (const char *)event_data->payload->data;
    size_t status_details_json_len = event_data->payload->len;
    err = ota_jobs_mqtt_publish_update_job_status_with_string(status, status_details_json, status_details_json_len);
    if (err == ESP_RMAKER_INVALID_ARG) {
        OSAL_LOGE(TAG, "Critical error: OTA status manager got invalid argument(s), terminating state machine");
        enter_error_state(OTA_ERROR_FATAL_UNEXPECTED_FORMAT);
        return OTA_JOB_STATE_ERROR;
    } else if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to publish update job status: %d", err);
        *should_free_event = false;
        enter_error_state_custom(OTA_ERROR_RETRY_WITH_BACKOFF, OTA_JOB_STATE_CUSTOM_JOB_EXECUTION, event_data);
        return OTA_JOB_STATE_ERROR;
    }
    return OTA_JOB_STATE_CUSTOM_JOB_EXECUTION;
}
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

static ota_job_state_t handle_state_job_execution(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    esp_rmaker_ota_error_reason_t error = OTA_ERROR_NONE;
    ota_job_state_t next_state = OTA_JOB_STATE_JOB_EXECUTION;

    /* Enter action: Mark job as active and start download */
    if (!g_ota_state_ctx.current_job.has_active_job) {
        /* First time entering this state */
        OSAL_LOGI(TAG, "JOB_EXECUTION: Starting job execution\n"
                  "  Job ID: %s\n"
                  "  Firmware version: %s\n"
                  "  File size: %" PRId32 " B\n"
                  "  Stream/URL: %s\n"
                  "  File signature: %s\n",
                  g_ota_state_ctx.current_job.job_id,
                  g_ota_state_ctx.current_job.ota_data.fw_version != NULL ? g_ota_state_ctx.current_job.ota_data.fw_version : "N/A",
                  (int32_t)g_ota_state_ctx.current_job.ota_data.filesize,
                  g_ota_state_ctx.current_job.ota_data.stream_id != NULL ? g_ota_state_ctx.current_job.ota_data.stream_id : "N/A",
                  g_ota_state_ctx.current_job.ota_data.file_signature != NULL ? g_ota_state_ctx.current_job.ota_data.file_signature : "N/A");

        /* Mark job as active (we've started executing) */
        g_ota_state_ctx.current_job.has_active_job = true;

        /* Reset job info flags */
        g_ota_state_ctx.current_job.should_reboot = false;
        g_ota_state_ctx.current_job.final_status_reported = false;
        g_ota_state_ctx.current_job.final_status_queued = false;

        /* Report update job status to AWS IoT Jobs */
        esp_rmaker_ota_status_details_t status_details;
        esp_rmaker_ota_status_details_fill_in_progress(&status_details, 0, g_ota_state_ctx.current_job.ota_data.filesize);
        ota_jobs_mqtt_publish_update_job_status(InProgress, &status_details);

        /* Post event to user application */
        const char *filetype = g_ota_state_ctx.current_job.filetype[0] != '\0' ? g_ota_state_ctx.current_job.filetype : NULL;
        esp_rmaker_ota_status_details_fill_starting(&status_details, g_ota_state_ctx.current_job.job_id, filetype, g_ota_state_ctx.current_job.ota_data.fw_version);
        osal_event_post(RMAKER_OTA_EVENT, RMAKER_OTA_EVENT_STARTING,
                        &status_details, sizeof(esp_rmaker_ota_status_details_t),
                        OSAL_MAX_DELAY);

        /* Start image download */
        esp_rmaker_error_t err = g_ota_state_ctx.image_download.ota_cb(
                                     (esp_rmaker_ota_handle_t)&g_ota_state_ctx,
                                     &g_ota_state_ctx.current_job.ota_data,
                                     g_ota_state_ctx.current_job.filetype_handler
                                 );

        if (err != ESP_RMAKER_OK) {
            esp_rmaker_ota_status_details_t status_details;
            esp_rmaker_ota_status_details_fill_failed(&status_details, ESP_RMAKER_OTA_FAILED_REASON_IMAGE_DOWNLOADER_SETUP_FAILED);
            ota_job_event_data_payload_t payload = {
                .data = (void *) &status_details,
                .len = sizeof(esp_rmaker_ota_status_details_t),
            };
            ota_job_event_data_t event_data = {
                .event = OTA_JOB_EVENT_FINAL_STATUS_REPORT_REQUESTED,
                .payload = &payload,
            };
            if (enqueue_event(&event_data, true) != ESP_RMAKER_OK) {
                enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
                return OTA_JOB_STATE_ERROR;
            }
            return OTA_JOB_STATE_POST_DOWNLOAD;
        }

        OSAL_LOGI(TAG, "Image download started");
        goto handle_state_job_execution_end;
    }

    /* Handle download events */
    if (!event_data) {
        return next_state;
    }

    ota_job_event_t event = event_data->event;

    if (event == OTA_JOB_EVENT_IMAGE_DOWNLOAD_PROGRESS) {
        char *downloaded_bytes_str = (char *)event_data->payload->data;
        uint32_t downloaded_bytes = strtoul(downloaded_bytes_str, NULL, 10);
        esp_rmaker_ota_status_details_t status_details;
        esp_rmaker_ota_status_details_fill_in_progress(&status_details, downloaded_bytes, g_ota_state_ctx.current_job.ota_data.filesize);
        esp_rmaker_ota_report_status((esp_rmaker_ota_handle_t)&g_ota_state_ctx, OTA_STATUS_IN_PROGRESS, &status_details);
    } else if (event == OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED) {
        OSAL_LOGI(TAG, "Image download succeeded!");

        /* Suitable error backoff reset point */
        retry_reset_backoff();

        /* Report update job status to AWS IoT Jobs */
        esp_rmaker_ota_status_details_t status_details;
        esp_rmaker_ota_status_details_fill_in_progress(&status_details, g_ota_state_ctx.current_job.ota_data.filesize, g_ota_state_ctx.current_job.ota_data.filesize);
        ota_jobs_mqtt_publish_update_job_status(InProgress, &status_details);

        /* Move to post-download state */
        *should_free_event = false;
        if (enqueue_event(event_data, false) != ESP_RMAKER_OK) {
            enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            return OTA_JOB_STATE_ERROR;
        }
        next_state = OTA_JOB_STATE_POST_DOWNLOAD;
    } else if (event == OTA_JOB_EVENT_TIMEOUT) {
        OSAL_LOGE(TAG, "Image download timed out, retrying...");
        error = OTA_ERROR_RETRY_WITH_BACKOFF;
        goto handle_state_job_execution_end;
    } else if ((event == OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_SETUP) ||
               (event == OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_STREAM_SUBSCRIPTION) ||
               (event == OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_POST_DOWNLOAD_CHECKS) ||
               (event == OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_IMAGE_HEADER_INVALID) ||
               (event == OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_SIGNATURE_INVALID) ||
               (event == OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_MD5_INVALID) ||
               (event == OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_UNKNOWN_ERROR)) {
        /* Report update job status to AWS IoT Jobs */
        const char *reason = NULL;
        switch (event) {
        case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_SETUP:
            reason = ESP_RMAKER_OTA_FAILED_REASON_IMAGE_DOWNLOADER_SETUP_FAILED;
            break;
        case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_STREAM_SUBSCRIPTION:
            reason = ESP_RMAKER_OTA_FAILED_REASON_MQTT_STREAM_SUBSCRIPTION_FAILED;
            break;
        case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_POST_DOWNLOAD_CHECKS:
            reason = ESP_RMAKER_OTA_FAILED_REASON_POST_DOWNLOAD_CHECKS_FAILED;
            break;
        case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_IMAGE_HEADER_INVALID:
            reason = ESP_RMAKER_OTA_FAILED_REASON_IMAGE_HEADER_INVALID;
            break;
        case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_SIGNATURE_INVALID:
            reason = ESP_RMAKER_OTA_FAILED_REASON_SIGNATURE_INVALID;
            break;
        case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_MD5_INVALID:
            reason = ESP_RMAKER_OTA_FAILED_REASON_MD5_INVALID;
            break;
        case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_UNKNOWN_ERROR:
        default:
            reason = ESP_RMAKER_OTA_FAILED_REASON_UNKNOWN_ERROR;
            break;
        }
        esp_rmaker_ota_status_details_t status_details;
        esp_rmaker_ota_status_details_fill_failed(&status_details, reason);

        ota_job_event_data_payload_t payload = {
            .data = (void *) &status_details,
            .len = sizeof(esp_rmaker_ota_status_details_t),
        };
        ota_job_event_data_t next_event_data = {
            .event = OTA_JOB_EVENT_FINAL_STATUS_REPORT_REQUESTED,
            .payload = &payload,
        };
        if (enqueue_event(&next_event_data, true) != ESP_RMAKER_OK) {
            enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            return OTA_JOB_STATE_ERROR;
        }
        next_state = OTA_JOB_STATE_POST_DOWNLOAD;
    } else if (event == OTA_JOB_EVENT_FINAL_STATUS_REPORT_REQUESTED) {
        OSAL_LOGI(TAG, "Final status report requested during job execution; queuing for post-download state");
        g_ota_state_ctx.current_job.current_status_details = *((const esp_rmaker_ota_status_details_t *)event_data->payload->data);
        g_ota_state_ctx.current_job.final_status_queued = true;
    } else {
        // ignore other events
        on_event_ignored(event_data);
    }

handle_state_job_execution_end:
    if (error != OTA_ERROR_NONE) {
        enter_error_state(error);
    }
    return next_state;
}

static ota_job_state_t handle_state_post_download(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    if (!event_data) {
        return OTA_JOB_STATE_POST_DOWNLOAD;
    }

    esp_rmaker_error_t err;

    /* Handle a queued final status report */
    if (g_ota_state_ctx.current_job.final_status_queued) {
        err = mqtt_publish_final_status(&g_ota_state_ctx.current_job.current_status_details);
        if (err == ESP_RMAKER_INVALID_ARG) {
            OSAL_LOGE(TAG, "Critical error: OTA status manager got invalid argument(s), terminating state machine");
            enter_error_state(OTA_ERROR_FATAL_UNEXPECTED_FORMAT);
            return OTA_JOB_STATE_ERROR;
        } else if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to publish final status");
            *should_free_event = false;
            enter_error_state_custom(OTA_ERROR_RETRY_WITH_BACKOFF, OTA_JOB_STATE_POST_DOWNLOAD, event_data);
            return OTA_JOB_STATE_ERROR;
        }
        g_ota_state_ctx.current_job.final_status_queued = false;
    }

    ota_job_state_t next_state = OTA_JOB_STATE_POST_DOWNLOAD;
    switch (event_data->event) {
    case OTA_JOB_EVENT_FINAL_STATUS_REPORT_REQUESTED:
        OSAL_LOGI(TAG, "POST_DOWNLOAD: Final status report requested");

        /* Report the final status */
        const esp_rmaker_ota_status_details_t *final_status_details = (const esp_rmaker_ota_status_details_t *)event_data->payload->data;
        if (!final_status_details) {
            OSAL_LOGE(TAG, "Status details is NULL");
            enter_error_state(OTA_ERROR_FATAL_UNEXPECTED_FORMAT);
            return OTA_JOB_STATE_ERROR;
        }
        err = mqtt_publish_final_status(final_status_details);
        if (err == ESP_RMAKER_INVALID_ARG) {
            OSAL_LOGE(TAG, "Critical error: OTA status manager got invalid argument(s), terminating state machine");
            enter_error_state(OTA_ERROR_FATAL_UNEXPECTED_FORMAT);
            return OTA_JOB_STATE_ERROR;
        } else if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to publish final status");
            *should_free_event = false;
            enter_error_state_custom(OTA_ERROR_RETRY_WITH_BACKOFF, OTA_JOB_STATE_IDLE, event_data);
            return OTA_JOB_STATE_ERROR;
        }
        g_ota_state_ctx.current_job.final_status_reported = true;
        break;
    case OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED:
        /* Set version if available */
        if (g_ota_state_ctx.current_job.filetype_handler->set_version != NULL &&
                g_ota_state_ctx.current_job.ota_data.fw_version != NULL) {
            esp_rmaker_ota_ft_version_t version = {
                .str = g_ota_state_ctx.current_job.ota_data.fw_version,
                .len = strlen(g_ota_state_ctx.current_job.ota_data.fw_version),
            };
            esp_rmaker_error_t err = g_ota_state_ctx.current_job.filetype_handler->set_version(version);
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGW(TAG, "Failed to set version for filetype '%s': %d", g_ota_state_ctx.current_job.filetype, err);
            }
        }

        /* Set should reboot flag (carried as a scalar so this terminal event stays
         * payload-free and heap-independent; see ota_job_event_data_t.fixed_data). */
        g_ota_state_ctx.current_job.should_reboot = event_data->fixed_data != NULL;

        if (g_ota_state_ctx.current_job.should_reboot) {
            /* Set the NVS state to indicate that a job is pending verification */
            const char *filetype = g_ota_state_ctx.current_job.filetype[0] != '\0' ? g_ota_state_ctx.current_job.filetype : NULL;
            esp_rmaker_ota_nvs_set_job_pending_verification(g_ota_state_ctx.current_job.job_id, filetype, g_ota_state_ctx.current_job.expected_version);
        } else {
            OSAL_LOGI(TAG, "Reboot not requested for filetype, waiting for final status report via esp_rmaker_ota_report_final_status()");
        }
        break;
    case OTA_JOB_EVENT_UPDATE_ACCEPTED:
        /* Check if there are still updates pending */
        if (!ota_status_is_cache_empty()) {
            // still waiting for updates to be handled
            OSAL_LOGI(TAG, "POST_DOWNLOAD: Still waiting for updates to be handled");
            break;
        }

        if (g_ota_state_ctx.current_job.should_reboot) {
#if CONFIG_RMNG_OTA_DISABLE_AUTO_REBOOT
            osal_event_post(RMAKER_OTA_EVENT, RMAKER_OTA_EVENT_REQ_FOR_REBOOT,
                            NULL, 0, OSAL_MAX_DELAY);
#else /* !CONFIG_RMNG_OTA_DISABLE_AUTO_REBOOT */
            osal_sysctrl_reboot();
#endif /* !CONFIG_RMNG_OTA_DISABLE_AUTO_REBOOT */
        } else if (g_ota_state_ctx.current_job.final_status_reported) {
            /* Job is complete, transition to IDLE */
            OSAL_LOGI(TAG, "POST_DOWNLOAD: Job is complete, no reboot requested, transitioning to IDLE");

            /* Reset current job info */
            memset(&g_ota_state_ctx.current_job, 0, sizeof(ota_job_info_t));

            /* Suitable error backoff reset point */
            retry_reset_backoff();

            /* Request a fetch */
            err = ota_job_state_fetch();
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to fetch jobs: %d", err);
                enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
                return OTA_JOB_STATE_ERROR;
            }

            next_state = OTA_JOB_STATE_IDLE;
        }
        break;

    default:
        on_event_ignored(event_data);
        break;
    }

    return next_state;
}

static ota_job_state_t handle_state_error(const ota_job_event_data_t *event_data, bool *should_free_event)
{
    if (!event_data) {
        return OTA_JOB_STATE_ERROR;
    }

    ota_job_state_t next_state = OTA_JOB_STATE_ERROR;
    ota_job_event_t event = event_data->event;
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    const char *error_message = NULL;
    switch (event) {
    case OTA_JOB_EVENT_ERROR_OCCURRED:
        error_message = esp_rmaker_ota_error_reason_to_string(g_ota_state_ctx.last_error);
        OSAL_LOGI(TAG, "OTA failed, state machine terminated. Reason: %s", error_message);

        /* If we have an active job, report FAILED to AWS */
        if (g_ota_state_ctx.current_job.has_active_job) {
            esp_rmaker_ota_status_details_t status_details;
            esp_rmaker_ota_status_details_fill_failed(&status_details, error_message);
            esp_rmaker_ota_report_status((esp_rmaker_ota_handle_t)&g_ota_state_ctx, OTA_STATUS_FAILED, &status_details);
            g_ota_state_ctx.current_job.has_active_job = false;
        }

        /* State machine stays in ERROR state until user decides to retry */
        break;
    case OTA_JOB_EVENT_RECOVERY_REQUESTED:
        OSAL_LOGI(TAG, "ERROR: Recovery requested");

        /* Post recovery event data if available */
        if (g_ota_state_ctx.recovery.event_data != NULL) {
            err = enqueue_event(g_ota_state_ctx.recovery.event_data, false); // pass ownership to next state
            /* enqueue_event frees on failure; either way we no longer own it */
            g_ota_state_ctx.recovery.event_data = NULL;
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to post recovery event: %d - scheduling retry with backoff", err);
                esp_rmaker_backoff_retry(&g_error_retry_context, retry_with_backoff_task, NULL);
                return OTA_JOB_STATE_ERROR;
            }
        } else {
            /* Post recovery event */
            ota_job_event_data_t recovery_event = {
                .event = g_ota_state_ctx.recovery.event,
                .payload = NULL,
            };
            err = enqueue_event(&recovery_event, true);
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to post recovery event: %d", err);
                esp_rmaker_backoff_retry(&g_error_retry_context, retry_with_backoff_task, NULL);
                return OTA_JOB_STATE_ERROR;
            }
        }

        /* Next state is the recovery state */
        next_state = g_ota_state_ctx.recovery.state;
        /* Reset context */
        recovery_reset_context();
        break;
    default:
        on_event_ignored(event_data);
        break;
    }
    return next_state;
}

/* Recovery operations */
static void recovery_reset_context(void)
{
    g_ota_state_ctx.last_error = OTA_ERROR_NONE;
    g_ota_state_ctx.recovery.state = OTA_JOB_STATE_UNINITIALIZED;
    g_ota_state_ctx.recovery.event = OTA_JOB_EVENT_ERROR_OCCURRED;
    if (g_ota_state_ctx.recovery.event_data != NULL) {
        free_event_data(g_ota_state_ctx.recovery.event_data);
        g_ota_state_ctx.recovery.event_data = NULL;
    }
}

esp_rmaker_error_t ota_job_state_recover(void)
{
    if (g_ota_state_ctx.recovery.state == OTA_JOB_STATE_UNINITIALIZED || g_ota_state_ctx.recovery.event == OTA_JOB_EVENT_ERROR_OCCURRED) {
        OSAL_LOGE(TAG, "Invalid recovery state: state=%s, event=%s", ota_job_state_to_string(g_ota_state_ctx.recovery.state), ota_job_event_to_string(g_ota_state_ctx.recovery.event));
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Post recovery event */
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_RECOVERY_REQUESTED,
        .payload = NULL,
    };
    esp_rmaker_error_t err = ota_job_state_post_event(&event_data);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to post recovery event: %d", err);
    }
    return err;
}

/* MQTT topic operations */

static esp_rmaker_error_t mqtt_subscribe_jobs_topics(void)
{
    char topic_buffer[TOPIC_BUFFER_SIZE];
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_OTA,
        .sub = MQTT_CHANNEL_SUB_OTA_JOBS_SUBSCRIBE,
    };

    /* Build wildcard topic: $aws/things/{thingName}/jobs/# */
    int topic_len = snprintf(topic_buffer, sizeof(topic_buffer),
                             "$aws/things/%s/jobs/#",
                             g_ota_state_ctx.thing_name);

    if (topic_len < 0 || topic_len >= (int)sizeof(topic_buffer)) {
        OSAL_LOGE(TAG, "Failed to build wildcard topic");
        return ESP_RMAKER_FAIL;
    }

    OSAL_LOGI(TAG, "Subscribing to: %s", topic_buffer);

    /* Record subscribe intent before the call so a SUBACK failure arriving
     * before this function returns is handled as a retry, not a no-op. */
    g_ota_state_ctx.sub_intended = true;

    osal_err_t mqtt_status = esp_rmaker_mqtt_impl.subscribe(
                                 &channel, topic_buffer, topic_len, mqtt_unified_callback, QoS1, NULL);

    if (mqtt_status != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to subscribe to jobs topic: %d", mqtt_status);
        return ESP_RMAKER_FAIL;
    }

    /* .subscribed is set only when the SUBACK arrives successfully
     * (see on_rmaker_mqtt_event); not set here. */
    OSAL_LOGI(TAG, "Jobs subscribe requested; awaiting SUBACK");
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t mqtt_unsubscribe_jobs_topics(void)
{
    /* Always clear intent + cancel any pending retry first, even if we never
     * got a successful SUBACK: a retry could still be armed. */
    g_ota_state_ctx.sub_intended = false;
    esp_rmaker_backoff_reset(&g_sub_retry_ctx, OTA_ERROR_RETRY_BASE_DELAY_MS);

    if (!g_ota_state_ctx.subscribed) {
        return ESP_RMAKER_OK;
    }

    char topic_buffer[TOPIC_BUFFER_SIZE];
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_OTA,
        .sub = MQTT_CHANNEL_SUB_OTA_JOBS_UNSUBSCRIBE,
    };

    int topic_len = snprintf(topic_buffer, sizeof(topic_buffer),
                             "$aws/things/%s/jobs/#",
                             g_ota_state_ctx.thing_name);

    if (topic_len > 0 && topic_len < (int)sizeof(topic_buffer)) {
        esp_rmaker_mqtt_impl.unsubscribe(&channel, topic_buffer, topic_len, QoS1);
    }

    g_ota_state_ctx.subscribed = false;
    OSAL_LOGI(TAG, "Unsubscribed from jobs topics");
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t mqtt_publish_get_pending(void)
{
    char topic_buffer[TOPIC_BUFFER_SIZE];
    size_t topic_len = 0;

    JobsStatus_t jobs_status = Jobs_GetPending(
                                   topic_buffer, sizeof(topic_buffer),
                                   g_ota_state_ctx.thing_name,
                                   g_ota_state_ctx.thing_name_length,
                                   &topic_len);

    if (jobs_status != JobsSuccess) {
        OSAL_LOGE(TAG, "Failed to build GetPending topic: %d", jobs_status);
        return ESP_RMAKER_FAIL;
    }

    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_OTA,
        .sub = MQTT_CHANNEL_SUB_OTA_GET_PENDING,
    };

    OSAL_LOGI(TAG, "Publishing to: %s", topic_buffer);
    osal_err_t mqtt_status = esp_rmaker_mqtt_impl.publish(
                                 &channel, topic_buffer, topic_len, "{}", 2, QoS1, false);

    if (mqtt_status != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to publish GetPending: %d", mqtt_status);
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t mqtt_publish_describe_job(const char *job_id)
{
    char topic_buffer[TOPIC_BUFFER_SIZE];
    size_t topic_len = 0;
    uint16_t job_id_len = strlen(job_id);

    JobsStatus_t jobs_status = Jobs_Describe(
                                   topic_buffer, sizeof(topic_buffer),
                                   g_ota_state_ctx.thing_name,
                                   g_ota_state_ctx.thing_name_length,
                                   job_id, job_id_len,
                                   &topic_len);

    if (jobs_status != JobsSuccess) {
        OSAL_LOGE(TAG, "Failed to build Describe topic: %d", jobs_status);
        return ESP_RMAKER_FAIL;
    }

    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_OTA,
        .sub = MQTT_CHANNEL_SUB_OTA_DESCRIBE_JOB,
    };

    OSAL_LOGI(TAG, "Publishing to: %s", topic_buffer);
    osal_err_t mqtt_status = esp_rmaker_mqtt_impl.publish(
                                 &channel, topic_buffer, topic_len, "{}", 2, QoS1, false);

    if (mqtt_status != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to publish Describe: %d", mqtt_status);
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t mqtt_publish_update_job_status(const char *job_id, uint16_t job_id_len, JobCurrentStatus_t status, const esp_rmaker_ota_status_details_t *status_details, int32_t *p_next_version)
{
    char *status_details_str = esp_rmaker_ota_status_details_to_json(status_details);
    if (status_details_str == NULL) {
        OSAL_LOGE(TAG, "Failed to convert status details to JSON");
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = mqtt_publish_update_job_status_with_string(job_id, job_id_len, status, status_details_str, strlen(status_details_str), p_next_version);
    free(status_details_str);
    return err;
}

static esp_rmaker_error_t mqtt_publish_update_job_status_with_string(const char *job_id, uint16_t job_id_len, JobCurrentStatus_t status, const char *status_details_json, size_t status_details_json_len, int32_t *p_next_version)
{
    /* Create OTA status object */
    ota_status_update_t ota_status = {
        .status = status,
        .status_details_str = (char *)status_details_json,
        .status_details_str_len = status_details_json_len,
    };
    memcpy(ota_status.job_id, job_id, job_id_len);
    ota_status.job_id[job_id_len] = '\0';
    ota_status.job_id_len = job_id_len;

    /* Send OTA status */
    return ota_status_send(&ota_status, p_next_version);
}
esp_rmaker_error_t ota_jobs_mqtt_publish_update_job_status(JobCurrentStatus_t status, const esp_rmaker_ota_status_details_t *status_details)
{
    uint16_t job_id_len = strlen(g_ota_state_ctx.current_job.job_id);
    esp_rmaker_error_t err = mqtt_publish_update_job_status(g_ota_state_ctx.current_job.job_id, job_id_len, status, status_details, &g_ota_state_ctx.current_job.expected_version);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to publish update job status: %d", err);
        return err;
    }

    /* Update current status on update publish success */
    g_ota_state_ctx.current_job.current_status = status;

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t ota_jobs_mqtt_publish_update_job_status_with_string(JobCurrentStatus_t status, const char *status_details_json, size_t status_details_json_len)
{
    uint16_t job_id_len = strlen(g_ota_state_ctx.current_job.job_id);
    esp_rmaker_error_t err = mqtt_publish_update_job_status_with_string(g_ota_state_ctx.current_job.job_id, job_id_len, status, status_details_json, status_details_json_len, &g_ota_state_ctx.current_job.expected_version);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to publish update job status: %d", err);
        return err;
    }

    /* Update current status on update publish success */
    g_ota_state_ctx.current_job.current_status = status;

    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t mqtt_publish_final_status(const esp_rmaker_ota_status_details_t *status_details)
{
    if (!status_details) {
        OSAL_LOGE(TAG, "Status details is NULL");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Cancel the timer */
    esp_rmaker_error_t err = filetype_handler_status_timer_stop();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to stop status timer: %d", (int)err);
        return err;
    }

    /* Get the final job status */
    JobCurrentStatus_t job_status;
    switch (status_details->type) {
    case ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_SUCCEEDED:
        job_status = Succeeded;
        break;
    case ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_FAILED:
        job_status = Failed;
        break;
    default:
        OSAL_LOGE(TAG, "Not a supported final status: %d", (int)status_details->type);
        return ESP_RMAKER_INVALID_ARG;
    }

    return ota_jobs_mqtt_publish_update_job_status(job_status, status_details);
}

/* Implemented here because it is tightly coupled to the state machine */
esp_rmaker_error_t esp_rmaker_ota_report_final_status(const esp_rmaker_ota_status_details_t *status_details)
{
    if (!status_details) {
        OSAL_LOGE(TAG, "Status details is NULL");
        return ESP_RMAKER_INVALID_ARG;
    }

    /**  Post an event that will be handled in either scenario:
     * Called without reboot: POST_DOWNLOAD state
     * Called with reboot: REBOOT_CHECK state
     */
    ota_job_event_data_payload_t payload = {
        .data = (void *)status_details,
        .len = sizeof(esp_rmaker_ota_status_details_t),
    };
    const ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_FINAL_STATUS_REPORT_REQUESTED,
        .payload = &payload,
    };
    return ota_job_state_post_event(&event_data);
}

/* MQTT callbacks */

static void mqtt_unified_callback(const char *topic, size_t topic_len,
                                  void *payload, size_t payload_len, void *priv_data)
{
    /* Make a mutable copy of the topic */
    char topic_copy[TOPIC_BUFFER_SIZE];
    if (topic_len >= sizeof(topic_copy)) {
        OSAL_LOGE(TAG, "Topic too long: %" PRIu32, (uint32_t)topic_len);
        return;
    }

    memcpy(topic_copy, topic, topic_len);
    topic_copy[topic_len] = '\0';

    /* Parse topic using Jobs library */
    JobsTopic_t api = JobsInvalidTopic;
    char *job_id = NULL;
    uint16_t job_id_len = 0;

    JobsStatus_t status = Jobs_MatchTopic(
                              topic_copy, topic_len,
                              g_ota_state_ctx.thing_name,
                              g_ota_state_ctx.thing_name_length,
                              &api, &job_id, &job_id_len);

    if (status != JobsSuccess) {
        OSAL_LOGW(TAG, "Non-Jobs topic: %.*s", (int)topic_len, topic);
        return;
    }

    OSAL_LOGD(TAG, "Received Jobs API: %d", api);

    /* Route to appropriate handler */
    switch (api) {
    case JobsJobsChanged:
        mqtt_on_jobs_changed(topic, topic_len, payload, payload_len, priv_data);
        break;
    case JobsGetPendingSuccess:
        mqtt_on_get_pending_accepted(topic, topic_len, payload, payload_len, priv_data);
        break;
    case JobsGetPendingFailed:
        mqtt_on_get_pending_rejected(topic, topic_len, payload, payload_len, priv_data);
        break;
    case JobsDescribeSuccess:
        mqtt_on_describe_accepted(topic, topic_len, payload, payload_len, priv_data);
        break;
    case JobsDescribeFailed:
        mqtt_on_describe_rejected(topic, topic_len, payload, payload_len, priv_data);
        break;
    case JobsUpdateSuccess:
        mqtt_on_update_accepted(topic, topic_len, payload, payload_len, priv_data);
        break;
    case JobsUpdateFailed:
        mqtt_on_update_rejected(topic, topic_len, payload, payload_len, priv_data);
        break;
    default:
        OSAL_LOGD(TAG, "Unhandled Jobs API: %d", api);
        break;
    }
}

static void mqtt_on_jobs_changed(const char *topic, size_t topic_len,
                                 void *payload, size_t payload_len, void *priv_data)
{
    OSAL_LOGI(TAG, "Jobs changed");
    OSAL_LOGD(TAG, "Payload: %.*s", (int)payload_len, (char *)payload);

    /* Post event to state machine */
    ota_job_event_data_payload_t payload_data = {
        .data = (char *)payload,
        .len = payload_len,
    };
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_JOBS_CHANGED,
        .payload = &payload_data,
    };
    ota_job_state_post_event(&event_data);
}

static void mqtt_on_get_pending_accepted(const char *topic, size_t topic_len,
        void *payload, size_t payload_len, void *priv_data)
{
    OSAL_LOGI(TAG, "GetPending accepted");
    OSAL_LOGD(TAG, "Payload: %.*s", (int)payload_len, (char *)payload);

    /* Post event to state machine */
    ota_job_event_data_payload_t payload_data = {
        .data = (char *)payload,
        .len = payload_len,
    };
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_PENDING_JOBS_ACCEPTED,
        .payload = &payload_data,
    };
    ota_job_state_post_event(&event_data);
}

static void mqtt_on_get_pending_rejected(const char *topic, size_t topic_len,
        void *payload, size_t payload_len, void *priv_data)
{
    OSAL_LOGE(TAG, "GetPending rejected");
    OSAL_LOGD(TAG, "Payload: %.*s", (int)payload_len, (char *)payload);

    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_PENDING_JOBS_REJECTED,
        .payload = NULL,
    };
    ota_job_state_post_event(&event_data);
}

static void mqtt_on_describe_accepted(const char *topic, size_t topic_len,
                                      void *payload, size_t payload_len, void *priv_data)
{
    OSAL_LOGI(TAG, "DescribeJob accepted");
    OSAL_LOGD(TAG, "Payload: %.*s", (int)payload_len, (char *)payload);

    /* Post event to state machine */
    ota_job_event_data_payload_t payload_data = {
        .data = (char *)payload,
        .len = payload_len,
    };
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_JOB_DOC_ACCEPTED,
        .payload = &payload_data,
    };
    ota_job_state_post_event(&event_data);
}

static void mqtt_on_describe_rejected(const char *topic, size_t topic_len,
                                      void *payload, size_t payload_len, void *priv_data)
{
    OSAL_LOGE(TAG, "DescribeJob rejected");
    OSAL_LOGD(TAG, "Payload: %.*s", (int)payload_len, (char *)payload);

    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_JOB_DOC_REJECTED,
        .payload = NULL,
    };
    ota_job_state_post_event(&event_data);
}

static void mqtt_on_update_accepted(const char *topic, size_t topic_len,
                                    void *payload, size_t payload_len, void *priv_data)
{
    OSAL_LOGI(TAG, "UpdateJob accepted");
    OSAL_LOGD(TAG, "Payload: %.*s", (int)payload_len, (char *)payload);

    /* Get the return values from the OTA status manager */
    ota_status_update_response_return_t return_values;
    esp_rmaker_error_t err = ota_status_on_update_response(payload, payload_len, true, &return_values);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to handle update accepted: %d", err);
        return;
    }

    /* Ignore if the update is not for the current job */
    if (strncmp(return_values.job_id, g_ota_state_ctx.current_job.job_id, return_values.job_id_len) != 0) {
        OSAL_LOGW(TAG, "Update is not for the current job, ignoring");
        return;
    }

    /* Mark the terminal status as reported if encountered */
    if (return_values.is_terminal) {
        g_ota_state_ctx.current_job.final_status_reported = true;
    }

    /* Post event to state machine */
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_UPDATE_ACCEPTED,
        .payload = NULL,
    };
    ota_job_state_post_event(&event_data);
}

static void mqtt_on_update_rejected(const char *topic, size_t topic_len,
                                    void *payload, size_t payload_len, void *priv_data)
{
    OSAL_LOGE(TAG, "UpdateJob rejected");
    OSAL_LOGD(TAG, "Payload: %.*s", (int)payload_len, (char *)payload);

    /* Pass payload to OTA status manager */
    ota_status_update_response_return_t return_values;
    esp_rmaker_error_t err = ota_status_on_update_response(payload, payload_len, false, &return_values);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to handle update rejected: %d", err);
        return;
    }

    /* Ignore if the update is not for the current job */
    if (strncmp(return_values.job_id, g_ota_state_ctx.current_job.job_id, return_values.job_id_len) != 0) {
        OSAL_LOGW(TAG, "Reject is not for the current job, ignoring");
        return;
    }

    /* Post event to state machine so states that block on an acceptance can react.
     * Recoverable rejects are still handled by the status manager's retry path; the
     * event only carries whether giving up is required (as fixed_data, so the event
     * stays payload-free; see ota_job_event_data_t.fixed_data). */
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_UPDATE_REJECTED,
        .payload = NULL,
        .fixed_data = return_values.is_unrecoverable_reject ? (void *) true : NULL,
    };
    ota_job_state_post_event(&event_data);
}

/* Download window operations */
#if CONFIG_RMNG_OTA_TIME_SUPPORT
static bool has_download_window(const ota_job_download_window_t *download_window)
{
    return download_window->validity.start != 0 && download_window->validity.end != 0;
}

#define MINUTES_PER_DAY 1440
/**
 * @brief Returns the time in minutes to delay this job:
 * - <0: the job has expired
 * - 0: the job is valid, do it now
 * - >0: should delay the job by this number of minutes
 * @param[in] download_window The download window to check
 * @return The time in seconds to delay this job
 */
static int32_t get_download_window_delay_time(const ota_job_download_window_t *download_window)
{
    /* Check validity period */
    time_t start_time = download_window->validity.start;
    time_t end_time = download_window->validity.end;

    // No validity period, always valid
    if (start_time == 0 || end_time == 0) {
        return 0;
    }

    // Invalid validity period, always invalid
    if (start_time > end_time) {
        return -1;
    }

    // Check if current time is within validity period
    time_t now = osal_get_time(NULL);
    if (now < start_time) {
        return (start_time - now) / 60;
    }
    if (now > end_time) {
        return -1;
    }

    /* Check daily window */
    int16_t start_minutes_since_midnight = download_window->daily.start;
    int16_t end_minutes_since_midnight = download_window->daily.end;

    // No daily window, always valid
    if (start_minutes_since_midnight == -1 || end_minutes_since_midnight == -1) {
        return 0;
    }

    // Check if current time is within daily window
    struct tm now_tm;
    localtime_r(&now, &now_tm);
    int16_t now_minutes_since_midnight = now_tm.tm_hour * 60 + now_tm.tm_min;

    // Download window spans across midnight, adjust so that start is at 0
    if (start_minutes_since_midnight > end_minutes_since_midnight) {
        int16_t diff_min = MINUTES_PER_DAY - start_minutes_since_midnight;
        start_minutes_since_midnight = 0;
        end_minutes_since_midnight += diff_min;
        now_minutes_since_midnight += diff_min;
        if (now_minutes_since_midnight > MINUTES_PER_DAY) {
            now_minutes_since_midnight -= MINUTES_PER_DAY;
        }
    }

    if (now_minutes_since_midnight >= start_minutes_since_midnight && now_minutes_since_midnight <= end_minutes_since_midnight) {
        return 0;
    }

    // Calculate the minimum delay to get into the daily window
    int32_t delay_min = start_minutes_since_midnight - now_minutes_since_midnight;
    if (delay_min < 0) {
        delay_min += MINUTES_PER_DAY;
    }
    return delay_min;
}
#endif /* CONFIG_RMNG_OTA_TIME_SUPPORT */

/* Helper functions */

static void start_response_timer(void)
{
    /* Post timeout event after OTA_RESPONSE_TIMEOUT_MS */
    OSAL_LOGD(TAG, "Starting response timer (%" PRIu32 " ms)", (uint32_t)OTA_RESPONSE_TIMEOUT_MS);
    esp_rmaker_error_t err = rmaker_ota_timeout_handler_restart(g_ota_state_ctx.timeout_handler_handle);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to restart timeout handler");
        enter_error_state(OTA_ERROR_TIMEOUT_HANDLER_RESTART_FAILED);
    }
}

static void stop_response_timer(void)
{
    if (g_ota_state_ctx.timeout_handler_handle) {
        OSAL_LOGD(TAG, "Stopping response timer");
        /* Stop timer if running */
        esp_rmaker_error_t err = rmaker_ota_timeout_handler_stop(g_ota_state_ctx.timeout_handler_handle);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGW(TAG, "Failed to stop timeout handler; might timeout anyway");
        }
    }
}

static void timeout_callback(void *priv_data)
{
    OSAL_LOGW(TAG, "Response timeout!");
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_TIMEOUT,
        .payload = NULL,
    };
    ota_job_state_post_event(&event_data);
}

static const char *ota_job_state_to_string(ota_job_state_t state)
{
    switch (state) {
    case OTA_JOB_STATE_UNINITIALIZED:
        return "UNINITIALIZED";
    case OTA_JOB_STATE_NETWORK_INIT:
        return "NETWORK_INIT";
    case OTA_JOB_STATE_REBOOT_CHECK:
        return "REBOOT_CHECK";
    case OTA_JOB_STATE_IDLE:
        return "IDLE";
    case OTA_JOB_STATE_JOBS_CHANGED:
        return "JOBS_CHANGED";
    case OTA_JOB_STATE_FETCHING_PENDING_JOBS:
        return "FETCHING_PENDING_JOBS";
    case OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS:
        return "WAITING_FOR_PENDING_JOBS";
    case OTA_JOB_STATE_PENDING_JOBS_RECEIVED:
        return "PENDING_JOBS_RECEIVED";
    case OTA_JOB_STATE_FETCHING_JOB_DOC:
        return "FETCHING_JOB_DOC";
    case OTA_JOB_STATE_WAITING_FOR_JOB_DOC:
        return "WAITING_FOR_JOB_DOC";
    case OTA_JOB_STATE_JOB_DOC_RECEIVED:
        return "JOB_DOC_RECEIVED";
    case OTA_JOB_STATE_JOB_EXECUTION:
        return "JOB_EXECUTION";
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
    case OTA_JOB_STATE_CUSTOM_JOB_EXECUTION:
        return "CUSTOM_JOB_EXECUTION";
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */
    case OTA_JOB_STATE_POST_DOWNLOAD:
        return "POST_DOWNLOAD";
    case OTA_JOB_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static const char *ota_job_event_to_string(ota_job_event_t event)
{
    switch (event) {
    case OTA_JOB_EVENT_EMPTY_TRANSITION:
        return "EMPTY_TRANSITION";
    case OTA_JOB_EVENT_ERROR_OCCURRED:
        return "ERROR_OCCURRED";
    case OTA_JOB_EVENT_REBOOT_CHECK_REQUESTED:
        return "REBOOT_CHECK_REQUESTED";
    case OTA_JOB_EVENT_FINAL_STATUS_REPORT_REQUESTED:
        return "FINAL_STATUS_REPORT_REQUESTED";
    case OTA_JOB_EVENT_FETCH_REQUESTED:
        return "FETCH_REQUESTED";
    case OTA_JOB_EVENT_JOBS_CHANGED:
        return "JOBS_CHANGED";
    case OTA_JOB_EVENT_PENDING_JOBS_ACCEPTED:
        return "PENDING_JOBS_ACCEPTED";
    case OTA_JOB_EVENT_PENDING_JOBS_REJECTED:
        return "PENDING_JOBS_REJECTED";
    case OTA_JOB_EVENT_JOB_DOC_ACCEPTED:
        return "JOB_DOC_ACCEPTED";
    case OTA_JOB_EVENT_JOB_DOC_REJECTED:
        return "JOB_DOC_REJECTED";
    case OTA_JOB_EVENT_UPDATE_ACCEPTED:
        return "UPDATE_ACCEPTED";
    case OTA_JOB_EVENT_UPDATE_REJECTED:
        return "UPDATE_REJECTED";
    case OTA_JOB_EVENT_TIMEOUT:
        return "TIMEOUT";
    case OTA_JOB_EVENT_IMAGE_DOWNLOAD_PROGRESS:
        return "IMAGE_DOWNLOAD_PROGRESS";
    case OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED:
        return "IMAGE_DOWNLOAD_SUCCEEDED";
    case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_SETUP:
        return "IMAGE_DOWNLOAD_FAILED_SETUP";
    case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_STREAM_SUBSCRIPTION:
        return "IMAGE_DOWNLOAD_FAILED_STREAM_SUBSCRIPTION";
    case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_POST_DOWNLOAD_CHECKS:
        return "IMAGE_DOWNLOAD_FAILED_POST_DOWNLOAD_CHECKS";
    case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_IMAGE_HEADER_INVALID:
        return "IMAGE_DOWNLOAD_FAILED_IMAGE_HEADER_INVALID";
    case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_SIGNATURE_INVALID:
        return "IMAGE_DOWNLOAD_FAILED_SIGNATURE_INVALID";
    case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_MD5_INVALID:
        return "IMAGE_DOWNLOAD_FAILED_MD5_INVALID";
    case OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_UNKNOWN_ERROR:
        return "IMAGE_DOWNLOAD_FAILED_UNKNOWN_ERROR";
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
    case OTA_JOB_EVENT_CUSTOM_JOB_PROGRESS:
        return "CUSTOM_JOB_PROGRESS";
    case OTA_JOB_EVENT_CUSTOM_JOB_SUCCEEDED:
        return "CUSTOM_JOB_SUCCEEDED";
    case OTA_JOB_EVENT_CUSTOM_JOB_FAILED:
        return "CUSTOM_JOB_FAILED";
    case OTA_JOB_EVENT_CUSTOM_JOB_REJECTED:
        return "CUSTOM_JOB_REJECTED";
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */
    default:
        return "UNKNOWN_EVENT";
    }
}

static void post_error_event(esp_rmaker_ota_error_reason_t error)
{
    OSAL_LOGE(TAG, "Entering ERROR state, reason: %s", esp_rmaker_ota_error_reason_to_string(error));

    /* Set last error */
    g_ota_state_ctx.last_error = error;

    /* Post error event to drive FSM into ERROR state */
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_ERROR_OCCURRED,
        .payload = NULL,
    };
    ota_job_state_post_event(&event_data);

    if (error == OTA_ERROR_RETRY_WITH_BACKOFF) {
        /* Backoff retry */
        esp_rmaker_backoff_retry(&g_error_retry_context, retry_with_backoff_task, NULL);
    } else {
        /* Post error to event loop */
        osal_event_post(RMAKER_OTA_EVENT, RMAKER_OTA_EVENT_ERROR_OCCURRED, &error, sizeof(error), OSAL_MAX_DELAY);
    }
}

static void enter_error_state(esp_rmaker_ota_error_reason_t error)
{
    ota_job_state_t recovery_state = OTA_JOB_STATE_UNINITIALIZED;
    ota_job_event_t recovery_event = OTA_JOB_EVENT_ERROR_OCCURRED;

    /* Set recovery context */
    switch (error) {
    case OTA_ERROR_NONE:
        break;
    case OTA_ERROR_SUBSCRIPTION_FAILED:
        /* Retry by re-initializing network resources */
        recovery_state = OTA_JOB_STATE_NETWORK_INIT;
        recovery_event = OTA_JOB_EVENT_EMPTY_TRANSITION;
        break;

    case OTA_ERROR_RETRY_WITH_BACKOFF:
        /* Retry by fetching jobs again */
        recovery_state = OTA_JOB_STATE_IDLE;
        recovery_event = OTA_JOB_EVENT_FETCH_REQUESTED;
        break;

    /* Job retrieval and execution errors */
    case OTA_ERROR_TIMEOUT_HANDLER_RESTART_FAILED:
    case OTA_ERROR_GET_PENDING_INVALID_FORMAT:
    case OTA_ERROR_GET_PENDING_REJECTED:
    case OTA_ERROR_DESCRIBE_JOB_REJECTED:
    case OTA_ERROR_JOB_DOC_PARSE_FAILED:
        /* Retry by fetching jobs again */
        recovery_state = OTA_JOB_STATE_IDLE;
        recovery_event = OTA_JOB_EVENT_FETCH_REQUESTED;
        break;

    /* All other errors as irrecoverable */
    default:
        OSAL_LOGE(TAG, "No recovery route available for error: %s - set as irrecoverable", esp_rmaker_ota_error_reason_to_string(error));
        break;
    }

    g_ota_state_ctx.recovery.state = recovery_state;
    g_ota_state_ctx.recovery.event = recovery_event;
    g_ota_state_ctx.recovery.event_data = NULL;
    post_error_event(error);
}

static void enter_error_state_custom(esp_rmaker_ota_error_reason_t error, ota_job_state_t recovery_state, const ota_job_event_data_t *recovery_event_data)
{
    g_ota_state_ctx.recovery.state = recovery_state;
    g_ota_state_ctx.recovery.event_data = (ota_job_event_data_t *)recovery_event_data;
    g_ota_state_ctx.recovery.event = OTA_JOB_EVENT_ERROR_OCCURRED;
    post_error_event(error);
}

static void retry_reset_backoff(void)
{
    esp_rmaker_backoff_reset(&g_error_retry_context, OTA_ERROR_RETRY_BASE_DELAY_MS);
}

static void retry_with_backoff_task(void *arg)
{
    esp_rmaker_error_t err = ota_job_state_recover();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to recover from error: %d", err);
        esp_rmaker_backoff_retry(&g_error_retry_context, retry_with_backoff_task, NULL);
    }
}

/* Subscription retry implementation ------------------------------------------ */

static void sub_retry_sched_cb(void *unused)
{
    (void)unused;
    /* Scheduler context; dispatch to work queue so the subscribe call
     * runs on a safe task context. */
    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(sub_retry_work, NULL);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to enqueue sub retry work: %d; rescheduling", err);
        esp_rmaker_backoff_retry(&g_sub_retry_ctx, sub_retry_sched_cb, NULL);
    }
}

static void sub_retry_work(void *unused)
{
    (void)unused;
    if (!g_ota_state_ctx.sub_intended) {
        /* Raced with unsubscribe/deinit; nothing to do. */
        return;
    }
    /* Re-arm intent + retry the subscribe. mqtt_subscribe_jobs_topics keeps
     * sub_intended true on re-entry. On local failure, retry via backoff. */
    if (mqtt_subscribe_jobs_topics() != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Sub retry: local subscribe call failed; scheduling next attempt");
        esp_rmaker_backoff_retry(&g_sub_retry_ctx, sub_retry_sched_cb, NULL);
    }
    /* On success: SUBACK handler resets backoff + flips .subscribed. */
}

static void on_rmaker_mqtt_event(void *arg, osal_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    switch ((esp_rmaker_common_event_t)id) {
    case RMAKER_MQTT_EVENT_DISCONNECTED:
        /* Broker drop. The subscription is gone server-side even though we
         * still intend it. Cancel any pending retry; CONNECTED will re-arm. */
        g_ota_state_ctx.subscribed = false;
        esp_rmaker_backoff_reset(&g_sub_retry_ctx, OTA_ERROR_RETRY_BASE_DELAY_MS);
        break;

    case RMAKER_MQTT_EVENT_CONNECTED:
        if (g_ota_state_ctx.sub_intended && !g_ota_state_ctx.subscribed) {
            /* Immediate attempt (current delay, no jitter/increment). */
            esp_rmaker_backoff_fire(&g_sub_retry_ctx, sub_retry_sched_cb, NULL);
        }
        break;

    case RMAKER_MQTT_EVENT_SUBSCRIBED: {
        const osal_mqtt_event_loop_data_on_complete_t *d = data;
        if (!d) {
            return;
        }
        if (d->channel.main != MQTT_CHANNEL_MAIN_OTA ||
                d->channel.sub != MQTT_CHANNEL_SUB_OTA_JOBS_SUBSCRIBE) {
            return;
        }
        if (d->status == OSAL_ERR_OK) {
            g_ota_state_ctx.subscribed = true;
            esp_rmaker_backoff_reset(&g_sub_retry_ctx, OTA_ERROR_RETRY_BASE_DELAY_MS);
            OSAL_LOGI(TAG, "Jobs SUBACK received");

            /* The subscription is live again. Republish any terminal job update
             * whose accepted/rejected response may have been dropped while the
             * subscription was down (e.g. across a reconnect), so the state
             * machine is not left waiting on a lost response. */
            ota_status_resend_pending_terminals();

            /* Do a fetch on successful subscription */
            err = ota_job_state_fetch();
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to fetch jobs: %d", err);
                enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
            }
        } else if (g_ota_state_ctx.sub_intended) {
            OSAL_LOGW(TAG, "Jobs SUBACK failed (status=%d); scheduling retry", d->status);
            esp_rmaker_backoff_retry(&g_sub_retry_ctx, sub_retry_sched_cb, NULL);
        }
        break;
    }

    default:
        break;
    }
}

static esp_rmaker_error_t register_mqtt_event_handlers(void)
{
    osal_err_t err;
    err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, on_rmaker_mqtt_event, NULL);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register CONNECTED handler: %d", err);
        return ESP_RMAKER_FAIL;
    }
    err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_DISCONNECTED, on_rmaker_mqtt_event, NULL);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register DISCONNECTED handler: %d", err);
        osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, on_rmaker_mqtt_event);
        return ESP_RMAKER_FAIL;
    }
    err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_SUBSCRIBED, on_rmaker_mqtt_event, NULL);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register SUBSCRIBED handler: %d", err);
        osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, on_rmaker_mqtt_event);
        osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_DISCONNECTED, on_rmaker_mqtt_event);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

static void unregister_mqtt_event_handlers(void)
{
    osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, on_rmaker_mqtt_event);
    osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_DISCONNECTED, on_rmaker_mqtt_event);
    osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_SUBSCRIBED, on_rmaker_mqtt_event);
}

#if defined(RMAKER_OTA_JOBS_TEST_WRAP_LINKER) || defined(RMAKER_OTA_JOBS_TEST_WRAP_DL_LIB)
void rmaker_ota_jobs_test_reset_backoff(void)
{
    esp_rmaker_backoff_reset(&g_error_retry_context, OTA_ERROR_RETRY_BASE_DELAY_MS);
}

uint64_t rmaker_ota_jobs_test_get_backoff_delay_ms(void)
{
    return g_error_retry_context.delay_ctx.delay_ms.current;
}
#endif
