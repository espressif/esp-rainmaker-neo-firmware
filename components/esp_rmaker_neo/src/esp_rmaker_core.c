/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_core.c
 * @brief Core functions for the ESP RainMaker Neo SDK.
 */

/* Declarations */
#include "esp_rmaker_core.h"

/* Standard includes */
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

/* Versioning includes */
#include "esp_rmaker_version.h"

/* Node model includes */
#include "node_internal.h"
#include "timeseries.h"
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "bridge/bridge_internal.h"
#endif
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "node_config_pending.h"
#endif

/* Core includes */
#include "core_internal.h"

/* Platform common headers */
#include "osal_log.h"

/* NVS includes */
#include "osal_storage.h"
#include "constants/nvs.h"

/* Challenge response includes */
#include "chal_resp/impl.h"

/* Time synchronization includes */
#include "osal_timesync.h"

/* Network connectivity includes */
#include "osal_netstatus.h"

/* Checksum includes */
#include "checksum_impl.h"

/* Configuration includes */
#include "local_config.h"
#include "constants/cloud.h"
#include "esp_rmaker_factory_part.h"
#include "sdkconfig.h"

#if CONFIG_ESP_RMAKER_ASSISTED_CLAIM
/* Assisted claiming includes */
#include "claim/impl.h"
#endif /* CONFIG_ESP_RMAKER_ASSISTED_CLAIM */

/* Cloud includes */
#include "network/cloud/manager.h"
#include "network/cloud/events.h"

/* Network includes */
#include "network/common.h"
#include "network/mqtt_channels.h"
#include "network/state_changes.h"
#include "network/notify.h"
#include "network/shadows.h"
#include "constants/network.h"
#if CONFIG_RMNG_CUSTOM_MQTT_CLIENT_PROVIDER
#include "osal_mqtt_prototypes.h"
#include "osal_mqtt_events.h"
#else
#include "osal_mqtt_impl.h"
#endif /* !CONFIG_RMNG_CUSTOM_MQTT_CLIENT_PROVIDER */
#ifdef CONFIG_ESP_RMAKER_MQTT_ENABLE_BUDGETING
#include "network/mqtt_budget.h"
#endif

/* Work Queue includes */
#include "esp_rmaker_work_queue.h"
#include "esp_rmaker_runtime_gate.h"

/* Event loop includes */
#include "event_loop.h"

/* Platform common includes */
#include "osal_scheduler.h"

/* Event flags includes */
#include "event_flags.h"

/* Crypto includes */
#include "util/esp_rmaker_crypto.h"

/* Services includes */
#include "services/schedules.h"
#include "services/automation.h"

/* Time-sync flow selector */
#include "time_sync_flow.h"

/* Retry includes */
#include "retry/manager.h"

/* Constants **************************************************************/

/**
 * @brief Tag for the core functions.
 */
static const char *TAG = "rmng_core";

/**
 * @brief Base delay for the cloud setup task.
 */
#define RMAKER_CORE_CLOUD_SETUP_BASE_DELAY_MS 1000

/**
 * @brief Base delay for the node configuration reporting task.
 */
#define RMAKER_CORE_NODE_CONFIG_REPORT_BASE_DELAY_MS 1000

#if CONFIG_RMNG_HOST_CTRL
/**
 * @brief Base delay for the indexed shadow subscribe task.
 */
#define RMAKER_CORE_INDEXED_SHADOW_SUBSCRIBE_BASE_DELAY_MS 1000
#endif /* CONFIG_RMNG_HOST_CTRL */

/* Structure definitions *******************************************************/

/**
 * @brief Private data for the ESP RainMaker Neo SDK.
 */
typedef struct {
    struct {
        esp_rmaker_state_t state: 3; /**< State of the ESP RainMaker Neo SDK, see esp_rmaker_state_t */
        bool is_init_common_components: 1; /**< Whether the common components are initialized */
        bool is_init_pre_prov: 1; /**< Whether the program is initialized for pre-provisioning */
        bool is_init_full: 1; /**< Whether the program is initialized for a node and all components */
        bool start_task_in_queue: 1; /**< Whether the start task is in the queue */
        bool enable_time_sync: 1; /**< Whether time synchronization is enabled */
        bool core_mqtt_setup_done: 1; /**< MQTT common setup (incl. budgeting wrapper) completed */
        bool core_mqtt_impl_inited: 1; /**< esp_rmaker_mqtt_impl.init completed */
    } trackers;
    uint8_t sub_flags; /**< Subscribed flags, see esp_rmaker_sub_flags_t */
#if CONFIG_ESP_RMAKER_ASSISTED_CLAIM
    /** Assisted-claiming context, non-NULL between esp_rmaker_pre_prov_init() and
     * esp_rmaker_pre_prov_deinit() when the node still needs to be claimed. */
    esp_rmaker_claim_data_t *claim_data;
#endif /* CONFIG_ESP_RMAKER_ASSISTED_CLAIM */
    struct {
        retry_manager_context_t cloud_setup; /**< The retry manager context for the cloud setup task */
        retry_manager_context_t node_config; /**< The retry manager context for the node configuration reporting task */
#if TIME_SYNC_DECOUPLED_FLOW
        retry_manager_context_t time_sync; /**< Flat-cadence poll for time sync; arms schedules on success (decoupled flow only) */
#endif
#if CONFIG_RMNG_HOST_CTRL
        retry_manager_context_t indexed_shadow_subscribe; /**< The retry manager context for the indexed shadow subscribe task */
#endif /* CONFIG_RMNG_HOST_CTRL */
    } retry_contexts; /**< Retry manager contexts for the various retry algorithms */
    const esp_rmaker_node_t *node; /**< Node handle */
    osal_mqtt_conn_params_t *mqtt_conn_params; /**< MQTT connection parameters */
} esp_rmaker_priv_data_t;

/* Global variables **************************************************************/

/**
 * @brief Private data for the ESP RainMaker Neo SDK.
 */
static esp_rmaker_priv_data_t priv_data = {0};

/**
 * @brief Whether the device has disconnected before.
 */
static bool has_disconnected_before = false;

/* Static function declarations ***************************************************/

/**
 * @brief Callback when a connection is established.
 * @param[in] event_handler_arg The argument to pass to the event handler.
 * @param[in] event_base The event base to register the event handler to.
 * @param[in] event_id The event id to register the event handler to.
 * @param[in] event_data The data to send with the event.
 */
static void __esp_rmaker_mqtt_connection_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief MQTT cloud events on complete event handler.
 * @param[in] event_handler_arg The argument to pass to the event handler.
 * @param[in] event_base The event base to register the event handler to.
 * @param[in] event_id The event id to register the event handler to.
 * @param[in] event_data The data to send with the event.
 */
static void __esp_rmaker_mqtt_cloud_on_complete_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief Initialize common components shared between SDK and pre-provisioning.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_FAIL on failure.
 */
static esp_rmaker_error_t __init_common_components(void);

/**
 * @brief Initialize the ESP RainMaker Neo SDK.
 * @param[in] config The configuration for the ESP RainMaker Neo SDK.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_FAIL on failure.
 * @return ESP_RMAKER_INVALID_ARG if the configuration is NULL.
 */
static esp_rmaker_error_t esp_rmaker_init(const esp_rmaker_config_t *config);

/**
 * @brief Deinitialize the ESP RainMaker Neo SDK.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_FAIL on failure.
 */
static esp_rmaker_error_t esp_rmaker_deinit(void);

/**
 * @brief Actions done only on the first MQTT connection. Starts retry managers for:
 * - Cloud setup: Subscribes to the from_cloud topic and gets cloud information (e.g., group info, Alexa is enabled, etc.)
 * - Node configuration report: Reports node configuration to the cloud.
 * - (remote only) Indexed shadow subscribe: Subscribes to the indexed shadow.
 */
static void esp_rmaker_on_first_mqtt_connection(void);

/**
 * @brief Task to start the ESP RainMaker Neo SDK.
 * @param[in] unused Unused parameter.
 */
static void esp_rmaker_start_task(void *unused);

/**
 * @brief Task to stop the ESP RainMaker Neo SDK.
 * @param[in] unused Unused parameter.
 */
static void esp_rmaker_stop_task(void *unused);

/**
 * @brief Task to perform required actions after a reconnect.
 * - Topics are already automatically resubscribed by the MQTT client.
 * - Need to call esp_rmaker_get_cloud_info() to get the cloud information again.
 * @param[in] unused Unused parameter.
 */
static void esp_rmaker_on_reconnect_task(void *unused);

/**
 * @brief Get cloud information.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_FAIL on failure.
 */
static esp_rmaker_error_t esp_rmaker_get_cloud_info(void);

/**
 * @brief Called when the cloud setup task fails.
 * - Sets the cloud information to defaults.
 * - Reschedules the cloud setup task with the backoff context.
 */
static void esp_rmaker_cloud_setup_on_failure(void);

/**
 * @brief Work function to setup cloud.
 * - Subscribes to the from_cloud topic.
 * - gets cloud information (e.g., group info, Alexa is enabled, etc.)
 * - Calls esp_rmaker_cloud_manager_start_listening() and esp_rmaker_get_cloud_info()
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t esp_rmaker_cloud_setup_work_fn(void *unused);

/**
 * @brief Work function to report node configuration.
 * - Reports node configuration to the cloud.
 * - Calls esp_rmaker_report_node_config()
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t esp_rmaker_node_config_report_work_fn(void *unused);

#if TIME_SYNC_DECOUPLED_FLOW
/**
 * @brief Time-sync poll work function (decoupled flow only).
 * - Returns ESP_RMAKER_FAIL while the clock is unsynced so the retry
 *   manager reschedules on the flat poll cadence (retries forever).
 * - On sync, arms all deferred schedules and returns ESP_RMAKER_OK to stop.
 * @return ESP_RMAKER_OK once time is synced, ESP_RMAKER_FAIL otherwise.
 */
static esp_rmaker_error_t esp_rmaker_time_sync_work_fn(void *unused);
#endif

#if CONFIG_RMNG_HOST_CTRL
/**
 * @brief Work function to subscribe to the indexed shadow.
 * - Subscribes to the indexed shadow.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t esp_rmaker_indexed_shadow_subscribe_work_fn(void *unused);
#endif /* CONFIG_RMNG_HOST_CTRL */

/**
 * @brief Register a node.
 * @param[in] node The node to register.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_FAIL on failure.
 * @return ESP_RMAKER_INVALID_ARG if the node is NULL.
 */
static esp_rmaker_error_t esp_rmaker_register_node(const esp_rmaker_node_t *node);

/**
 * @brief Check if the node should be reported as online, and report if so.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_FAIL on failure.
 */
static esp_rmaker_error_t esp_rmaker_check_and_report_online_status(void);

/* Static function definitions ****************************************************/

static void __esp_rmaker_mqtt_connection_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch ((esp_rmaker_common_event_t) event_id) {
    case RMAKER_MQTT_EVENT_CONNECTED:
        OSAL_LOGI(TAG, "MQTT Connection established");

        if (has_disconnected_before) {
            // This is a reconnect; perform required actions after a reconnect.
            esp_rmaker_work_queue_add_task(esp_rmaker_on_reconnect_task, NULL);
        }
        break;
    case RMAKER_MQTT_EVENT_DISCONNECTED:
        OSAL_LOGI(TAG, "MQTT Connection lost");

        /* A disconnect implies mass unsubscription from all topics. */
        esp_rmaker_core_unsubscribed_from_cloud();
        esp_rmaker_core_unsubscribed_from_params();

        esp_rmaker_event_flags_clear_online();

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        /* Drop inflight markers in the node-config pending list: acks
         * for the prior MQTT session are abandoned, so on reconnect
         * the retry context refires every still-pending ctx. */
        node_config_pending_clear_all_inflight();
#endif

        /* Only set the reconnect flag if not stopping/stopped, to avoid race with stop task */
        if (priv_data.trackers.state != ESP_RMAKER_STATE_STOP_REQUESTED &&
                priv_data.trackers.state != ESP_RMAKER_STATE_STOPPED) {
            has_disconnected_before = true;
        }
        break;
    default:
        OSAL_LOGE(TAG, "Unknown event id on connection handler: %" PRId32, event_id);
        break;
    }
}

static void __esp_rmaker_mqtt_cloud_on_complete_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    osal_mqtt_event_loop_data_on_complete_t *mqtt_data = (osal_mqtt_event_loop_data_on_complete_t *)event_data;
    osal_mqtt_event_loop_channel_t channel = mqtt_data->channel;
    osal_err_t status = mqtt_data->status;
    switch (channel.main) {
    case MQTT_CHANNEL_MAIN_CLOUD_MANAGER:
        switch ((mqtt_channel_sub_cloud_manager_t) channel.sub) {
        case MQTT_CHANNEL_SUB_CLOUD_MANAGER_CLOUD_SETUP:
            if (status != OSAL_ERR_OK) {
                esp_rmaker_cloud_setup_on_failure();
                retry_manager_resume_context(&priv_data.retry_contexts.cloud_setup);
            } else {
                retry_manager_stop_context(&priv_data.retry_contexts.cloud_setup);
            }
            break;
        case MQTT_CHANNEL_SUB_CLOUD_MANAGER_REPORT_NODE_CONFIG:
            if (status != OSAL_ERR_OK) {
                retry_manager_resume_context(&priv_data.retry_contexts.node_config);
            } else {
                retry_manager_stop_context(&priv_data.retry_contexts.node_config);
            }
            break;
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        case MQTT_CHANNEL_SUB_CLOUD_MANAGER_BRIDGE_CHILD_CLOUD_INFO:
            /* Fire-and-forget: a per-child cloud-info publish failure
             * does not trigger the self-only cloud-setup retry. The
             * bridge's RMAKER_MQTT_EVENT_CONNECTED handler re-fires the
             * event bundle for every READY child on the next connect,
             * which is the recovery path. */
            if (status != OSAL_ERR_OK) {
                OSAL_LOGW(TAG, "Bridge child cloud-info publish failed: %d (will retry on next reconnect)", status);
            }
            break;
#endif
        default:
            break;
        }
        break;
#if CONFIG_RMNG_HOST_CTRL
    case MQTT_CHANNEL_MAIN_SHADOWS:
        switch ((mqtt_channel_sub_shadows_t) channel.sub) {
        case MQTT_CHANNEL_SUB_INDEXED_SHADOW_SUBSCRIBE:
            if (status != OSAL_ERR_OK) {
                retry_manager_resume_context(&priv_data.retry_contexts.indexed_shadow_subscribe);
            } else {
                retry_manager_stop_context(&priv_data.retry_contexts.indexed_shadow_subscribe);
            }
            break;
        case MQTT_CHANNEL_SUB_INDEXED_SHADOW_UNSUBSCRIBE:
            break;
        default:
            break;
        }
        break;
#endif /* CONFIG_RMNG_HOST_CTRL */
    default:
        break;
    }
}

static esp_rmaker_error_t __init_common_components(void)
{
    if (priv_data.trackers.is_init_common_components) {
        return ESP_RMAKER_OK;
    }

    /* Arm the connectivity trapdoor as early as possible - before the network can come
     * up - so the start task can gate MQTT on connectivity (see esp_rmaker_start_task). */
    osal_netstatus_arm();

    /* Initialise event loop */
    OSAL_LOGD(TAG, "Initializing event loop...");
    esp_rmaker_error_t event_loop_err = event_loop_init(__esp_rmaker_mqtt_connection_handler);
    if (event_loop_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize event loop");
        return event_loop_err;
    }

    /* Initialise NVS - needed by local configuration */
    OSAL_LOGD(TAG, "Initializing NVS...");
    osal_err_t nvs_err = osal_storage_init(RMAKER_NVS_PART_NAME);
    if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to initialize NVS");
        return ESP_RMAKER_FAIL;
    }

    /* Initialise local configuration */
    OSAL_LOGD(TAG, "Initializing local configuration...");
    esp_rmaker_error_t local_config_err = esp_rmaker_local_config_init();
    if (local_config_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize local configuration: %d", local_config_err);
        return local_config_err;
    }

    priv_data.trackers.is_init_common_components = true;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __deinit_common_components(void)
{
    esp_rmaker_error_t err_out = ESP_RMAKER_OK;

    if (!priv_data.trackers.is_init_common_components) {
        return ESP_RMAKER_OK;
    }

    /* De-initialize local configuration */
    esp_rmaker_error_t local_config_err = esp_rmaker_local_config_deinit();
    if (local_config_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize local configuration");
        err_out = ESP_RMAKER_FAIL;
    }

    /* NVS is deliberately left initialized so that external components can still use it
     * regardless of the SDK's state.
     * osal_storage_init() is idempotent, so a later re-init still works. */

    /* De-initialize event loop */
    esp_rmaker_error_t event_loop_err = event_loop_deinit();
    if (event_loop_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize event loop");
        err_out = ESP_RMAKER_FAIL;
    }

    if (err_out == ESP_RMAKER_OK) {
        priv_data.trackers.is_init_common_components = false;
    }
    return err_out;
}

static esp_rmaker_error_t esp_rmaker_init(const esp_rmaker_config_t *config)
{
    esp_rmaker_error_t ret = ESP_RMAKER_OK;

    if (!config) {
        OSAL_LOGE(TAG, "Configuration cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Initialise common components if not already initialized */
    if (__init_common_components() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize common components");
        ret = ESP_RMAKER_FAIL;
        goto fail;
    }

    /* Initialise checksum */
    OSAL_LOGD(TAG, "Initializing checksum...");
    ret = esp_rmaker_checksum_init();
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize checksum");
        goto fail;
    }

    /* Initialise time synchronization (only reflect enable in trackers after success) */
    priv_data.trackers.enable_time_sync = false;
    if (config->enable_time_sync) {
        OSAL_LOGD(TAG, "Initializing time synchronization...");
        // use default configuration
        osal_timesync_config_t osal_timesync_config = {
            .server_name = NULL,
            .sync_time_cb = NULL,
            .event_loop_registration_info = {
                .event_base = RMAKER_COMMON_EVENT,
                .event_ids = {
                    .tz_posix_changed = RMAKER_EVENT_TZ_POSIX_CHANGED,
                    .tz_changed = RMAKER_EVENT_TZ_CHANGED,
                },
            },
        };
        int osal_timesync_err = osal_timesync_init(&osal_timesync_config);
        if (osal_timesync_err != 0) {
            OSAL_LOGE(TAG, "Failed to initialize time synchronization");
            ret = ESP_RMAKER_FAIL;
            goto fail;
        }
        priv_data.trackers.enable_time_sync = true;
    }

    /* Initialise scheduler */
    OSAL_LOGD(TAG, "Initializing scheduler...");
    osal_err_t scheduler_err = osal_scheduler_init();
    if (scheduler_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to initialize scheduler");
        ret = ESP_RMAKER_FAIL;
        goto fail;
    }

    /* Initialise network common */
    OSAL_LOGD(TAG, "Initializing network common...");
    ret = esp_rmaker_network_init();
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize network common");
        goto fail;
    }

    /* Initialise state change management */
    OSAL_LOGD(TAG, "Initializing state change management...");
    ret = esp_rmaker_state_init();
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize state change management");
        goto fail;
    }

    /* Initialise cloud manager */
    OSAL_LOGD(TAG, "Initializing cloud manager...");
    ret = esp_rmaker_cloud_manager_init();
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize cloud manager");
        goto fail;
    }

    /* Initialise notification manager */
    OSAL_LOGD(TAG, "Initializing notification manager...");
    ret = esp_rmaker_notify_init();
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize notification manager");
        goto fail;
    }

    /* Initialise timeseries manager */
    OSAL_LOGD(TAG, "Initializing timeseries manager...");
    ret = timeseries_init();
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize timeseries manager");
        goto fail;
    }

    /* Initialise Work Queue */
    OSAL_LOGD(TAG, "Initializing Work Queue...");
    ret = esp_rmaker_work_queue_init();
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize Work Queue");
        goto fail;
    }

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    /* Initialise the bridge subsystem. */
    OSAL_LOGD(TAG, "Initializing bridge subsystem...");
    ret = bridge_internal_init();
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize bridge subsystem");
        goto fail;
    }
#endif

    /* Initialise MQTT */
    OSAL_LOGI(TAG, "Initializing MQTT...");
#if CONFIG_RMNG_CUSTOM_MQTT_CLIENT_PROVIDER
    osal_mqtt_impl_setup_t mqtt_setup_fn = config->mqtt_setup_fn;
    if (!mqtt_setup_fn) {
        OSAL_LOGE(TAG, "MQTT setup function is not provided, even though custom MQTT client provider is enabled");
        ret = ESP_RMAKER_FAIL;
        goto fail;
    }
#else
    osal_mqtt_impl_setup_t mqtt_setup_fn = osal_mqtt_impl_setup;
#endif /* CONFIG_RMNG_CUSTOM_MQTT_CLIENT_PROVIDER */
#ifdef CONFIG_ESP_RMAKER_MQTT_ENABLE_BUDGETING
    // Use the budgetted implementation
    osal_err_t mqtt_err = mqtt_budget_impl_setup(mqtt_setup_fn, &esp_rmaker_mqtt_impl);
#else
    // Use the default implementation
    osal_err_t mqtt_err = mqtt_setup_fn(&esp_rmaker_mqtt_impl);
#endif /* CONFIG_ESP_RMAKER_MQTT_ENABLE_BUDGETING */
    if (mqtt_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to set up MQTT implementation: %d", mqtt_err);
        ret = ESP_RMAKER_FAIL;
        goto fail;
    }
    priv_data.trackers.core_mqtt_setup_done = true;

    ret = esp_rmaker_credentials_get_mqtt_conn_params(&priv_data.mqtt_conn_params);
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get MQTT connection parameters");
        goto fail;
    }

    osal_mqtt_event_loop_registration_info_t event_loop_registration_info = {
        .event_base = RMAKER_COMMON_EVENT,
        .event_ids = {
            .connected = RMAKER_MQTT_EVENT_CONNECTED,
            .disconnected = RMAKER_MQTT_EVENT_DISCONNECTED,
            .published = RMAKER_MQTT_EVENT_PUBLISHED,
            .subscribed = RMAKER_MQTT_EVENT_SUBSCRIBED,
            .unsubscribed = RMAKER_MQTT_EVENT_UNSUBSCRIBED,
        },
    };

    mqtt_err = esp_rmaker_mqtt_impl.init(priv_data.mqtt_conn_params, &event_loop_registration_info);
    if (mqtt_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to initialize MQTT implementation");
        ret = ESP_RMAKER_FAIL;
        goto fail;
    }
    priv_data.trackers.core_mqtt_impl_inited = true;

    /* Initialize services */
    OSAL_LOGD(TAG, "Initializing services...");
    /* Schedule service is initialized regardless of enable_time_sync:
     * Arming is deferred until time syncs (see __arm_or_defer / arm_all). */
    ret = esp_rmaker_schedule_service_init();
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize schedule service");
        goto fail;
    }

    // Automation service
    ret = esp_rmaker_automation_service_init();
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize automation service");
        goto fail;
    }

    /* Add work function to start the ESP RainMaker SDK as first task in queue */
    esp_rmaker_error_t add_task_err = esp_rmaker_work_queue_add_task(esp_rmaker_start_task, NULL);
    if (add_task_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add work function to start the ESP RainMaker SDK");
    } else {
        priv_data.trackers.start_task_in_queue = true;
    }

    return ESP_RMAKER_OK;

fail: {
        esp_rmaker_error_t deinit_err = esp_rmaker_deinit();
        if (deinit_err != ESP_RMAKER_OK) {
            OSAL_LOGW(TAG, "Teardown after failed init reported errors: %d", deinit_err);
        }
    }
    return ret;
}

static esp_rmaker_error_t esp_rmaker_deinit(void)
{
    esp_rmaker_error_t err_out = ESP_RMAKER_OK;
    esp_rmaker_error_t step_err;

    /* Defensive: close the runtime gate in case deinit was reached without a
     * preceding stop (e.g. a reset that skips stop). */
    esp_rmaker_runtime_gate_set_active(false);

    /* Join the work-queue worker FIRST, before any subsystem/child teardown. */
    step_err = esp_rmaker_work_queue_deinit();
    if (step_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize Work Queue");
        err_out = ESP_RMAKER_FAIL;
    }
    priv_data.trackers.start_task_in_queue = false;

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    /* Tear the bridge subsystem down first. Each child slot's free path
     * resets its embedded node, which in turn drops per-node schedule
     * handles via ::esp_rmaker_schedule_service_unload_node - so this must happen
     * BEFORE schedule_service_deinit, otherwise the visitor that unloads
     * per-node lists would race the child free path. */
    step_err = bridge_internal_deinit();
    if (step_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize bridge subsystem");
        err_out = ESP_RMAKER_FAIL;
    }
#endif

    /* De-initialize services (best-effort; continue on failure) */
    step_err = esp_rmaker_schedule_service_deinit();
    if (step_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize schedule service");
        err_out = ESP_RMAKER_FAIL;
    }
    step_err = esp_rmaker_automation_service_deinit();
    if (step_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize automation service");
        err_out = ESP_RMAKER_FAIL;
    }

    /* De-initialize MQTT */
    if (priv_data.trackers.core_mqtt_impl_inited) {
        osal_err_t mqtt_err = esp_rmaker_mqtt_impl.deinit();
        if (mqtt_err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to de-initialize MQTT implementation");
            err_out = ESP_RMAKER_FAIL;
        }
        priv_data.trackers.core_mqtt_impl_inited = false;
    }
#ifdef CONFIG_ESP_RMAKER_MQTT_ENABLE_BUDGETING
    if (priv_data.trackers.core_mqtt_setup_done) {
        mqtt_budget_impl_deinit(&esp_rmaker_mqtt_impl);
    }
#endif /* CONFIG_ESP_RMAKER_MQTT_ENABLE_BUDGETING */
    if (priv_data.mqtt_conn_params) {
        esp_rmaker_credentials_free_mqtt_conn_params(priv_data.mqtt_conn_params);
        priv_data.mqtt_conn_params = NULL;
    }
    priv_data.trackers.core_mqtt_setup_done = false;

    /* De-initialize cloud manager */
    step_err = esp_rmaker_cloud_manager_deinit();
    if (step_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize cloud manager");
        err_out = ESP_RMAKER_FAIL;
    }

    /* De-initialize timeseries manager */
    step_err = timeseries_deinit();
    if (step_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize timeseries manager");
        err_out = ESP_RMAKER_FAIL;
    }

    /* De-initialize notification manager */
    step_err = esp_rmaker_notify_deinit();
    if (step_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize notification manager");
        err_out = ESP_RMAKER_FAIL;
    }

    /* De-initialize state change management */
    step_err = esp_rmaker_state_deinit();
    if (step_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize state change management");
        err_out = ESP_RMAKER_FAIL;
    }

    /* De-initialize network common */
    step_err = esp_rmaker_network_deinit();
    if (step_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize network common");
        err_out = ESP_RMAKER_FAIL;
    }

    /* De-initialize scheduler */
    osal_err_t scheduler_err = osal_scheduler_deinit();
    if (scheduler_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize scheduler");
        err_out = ESP_RMAKER_FAIL;
    }

    /* De-initialize time synchronization */
    if (priv_data.trackers.enable_time_sync) {
        OSAL_LOGD(TAG, "De-initializing time synchronization...");
        int osal_timesync_err = osal_timesync_deinit();
        if (osal_timesync_err != 0) {
            OSAL_LOGE(TAG, "Failed to de-initialize time synchronization");
            err_out = ESP_RMAKER_FAIL;
        } else {
            priv_data.trackers.enable_time_sync = false;
        }
    }

    /* De-initialize checksum */
    step_err = esp_rmaker_checksum_deinit();
    if (step_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize checksum");
        err_out = ESP_RMAKER_FAIL;
    }

    /* De-initialize common components */
    if (priv_data.trackers.is_init_common_components) {
        step_err = __deinit_common_components();
        if (step_err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to de-initialize common components");
            err_out = ESP_RMAKER_FAIL;
        }
    }

    return err_out;
}

static void esp_rmaker_on_first_mqtt_connection(void)
{
    /**
     * Setup the cloud using a retry manager.
     * - Subscribe to the from_cloud topic.
     * - Get cloud information (e.g., group info, Alexa is enabled, etc.)
     * - MQTT parameters subscription is handled in the group info callback
     */
    OSAL_LOGD(TAG, "Setting up cloud...");
    retry_manager_execute_context(&priv_data.retry_contexts.cloud_setup);

    /* Node-config drain is kicked from inside cloud_setup_work_fn's
     * success path (see esp_rmaker_cloud_setup_work_fn) - only after
     * the cloud subscribe is genuinely complete. */

#if CONFIG_RMNG_HOST_CTRL
    /* Start subscribing to the indexed shadow (for use with remote control) */
    OSAL_LOGD(TAG, "Subscribing to the indexed shadow...");
    retry_manager_execute_context(&priv_data.retry_contexts.indexed_shadow_subscribe);
#endif /* CONFIG_RMNG_HOST_CTRL */
}

static void esp_rmaker_start_task(void *unused)
{
    priv_data.trackers.start_task_in_queue = false;

    osal_err_t mqtt_err;
    esp_rmaker_error_t rmaker_err;

    /* Time synchronization: two flows.
     *
     * CONFIG_MBEDTLS_HAVE_TIME_DATE set -> mbedTLS validates the server
     * cert's notBefore/notAfter, so a valid clock is a hard prerequisite
     * for the MQTT TLS handshake. Preserve the synchronous flow: block the
     * work queue until synced (as before). Schedules then arm inline
     * because ``osal_timesync_is_synced()`` is already true; the poll is unused.
     *
     * Otherwise -> decoupled flow: never block. Kick the time-sync poll and
     * let MQTT + all non-wall-clock work proceed immediately. The poll
     * arms schedules once it observes sync; wall-clock consumers guard
     * on ``osal_timesync_is_synced()`` until then. The poll is kicked
     * regardless of ``enable_time_sync`` - that flag only decides whether
     * *this* SDK inits SNTP; the clock syncs either way (possibly via an
     * external SNTP owner). */
#if TIME_SYNC_DECOUPLED_FLOW
    OSAL_LOGD(TAG, "Decoupled time-sync flow: kicking time-sync poll (non-blocking).");
    retry_manager_execute_context(&priv_data.retry_contexts.time_sync);
#else
    /* Poll is_synced() directly rather than osal_timesync_wait_for_sync(): the
     * latter returns immediately (no block) when this SDK did not init SNTP
     * (enable_time_sync=false, external SNTP owner), which would let the TLS
     * handshake run against a ~1970 clock and leave schedules unarmed (the
     * arm-on-sync poll is decoupled-flow only). Polling here blocks until the
     * clock is valid regardless of who owns SNTP; on_start then arms inline.
     *
     * The wait is unbounded: in this flow a valid clock is a hard TLS
     * prerequisite and the cloud getTimeSync coarse-set cannot help (it
     * arrives over MQTT, which cannot connect without a valid clock). */
    OSAL_LOGD(TAG, "MBEDTLS_HAVE_TIME_DATE: waiting for time synchronization indefinitely...");
    while (!osal_timesync_is_synced()) {
        osal_task_delay(osal_ticks_from_ms(2000));
    }
    OSAL_LOGI(TAG, "Time synchronized.");
#endif

    /* Initialize shadows */
    OSAL_LOGD(TAG, "Initializing shadows...");
    rmaker_err = esp_rmaker_shadows_init();
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize shadows with esp_rmaker_error_t: %d", rmaker_err);
        goto start_task_fail;
    }

    /* Service on starts */
    // Schedule service
    OSAL_LOGD(TAG, "Loading schedule details from NVS...");
    rmaker_err = esp_rmaker_schedule_service_on_start();
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to load schedule details from NVS");
        goto start_task_fail;
    }
    // Automation service
    OSAL_LOGD(TAG, "Loading automation details from NVS...");
    rmaker_err = esp_rmaker_automation_service_on_start();
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to load automation details from NVS");
        goto start_task_fail;
    }

    /* Gate on network connectivity before attempting MQTT, so we don't burn DNS/connect
     * retries while the device is still provisioning/connecting. Blocks until the first IP
     * is acquired (no-op on POSIX), then disarms the trapdoor. */
    OSAL_LOGD(TAG, "Waiting for network connectivity...");
    osal_netstatus_trap();

    /* Connect to MQTT */
    OSAL_LOGD(TAG, "Connecting to MQTT broker...");
#ifdef CONFIG_ESP_RMAKER_MQTT_ENABLE_BUDGETING
    // Start the MQTT budget reviver
    mqtt_err = mqtt_budget_impl_start_reviver();
    if (mqtt_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to start MQTT budget reviver");
        goto start_task_fail;
    }
#endif /* CONFIG_ESP_RMAKER_MQTT_ENABLE_BUDGETING */
    mqtt_err = esp_rmaker_mqtt_impl.connect();
    if (mqtt_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to start MQTT connection task");
        goto start_task_fail;
    }
    if (!osal_mqtt_event_wait_client_connected( UINT32_MAX )) { /**< Infinite timeout */
        OSAL_LOGW(TAG, "MQTT client did not connect within timeout");
    } else {
        OSAL_LOGI(TAG, "Connected to MQTT broker");
    }

    /* Register the MQTT on complete event handler */
    esp_rmaker_error_t err = event_loop_register_mqtt_on_complete_handler(__esp_rmaker_mqtt_cloud_on_complete_event_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to register MQTT on complete event handler: %d", err);
        goto start_task_fail;
    }

    /* Fully started: open the runtime gate so deferrable work may run. */
    esp_rmaker_runtime_gate_set_active(true);

    /* Do actions only on the first MQTT connection */
    esp_rmaker_on_first_mqtt_connection();

    /* Post the core started event */
    osal_event_post(RMAKER_EVENT, RMAKER_EVENT_CORE_STARTED, NULL, 0, OSAL_MAX_DELAY);

    priv_data.trackers.state = ESP_RMAKER_STATE_STARTED;
    esp_rmaker_event_flags_set_started();
    return;

start_task_fail:
    priv_data.trackers.state = ESP_RMAKER_STATE_ERROR;
}

static void esp_rmaker_stop_task(void *unused)
{
    OSAL_LOGI(TAG, "Stopping ESP RainMaker...");
    osal_err_t mqtt_err;
    esp_rmaker_error_t rmaker_err;

    /* Stop any existing retries */
    OSAL_LOGD(TAG, "Stopping any existing retries...");
    retry_manager_stop_context(&priv_data.retry_contexts.cloud_setup); // Cloud setup retry
    retry_manager_stop_context(&priv_data.retry_contexts.node_config); // Node configuration reporting retry
#if TIME_SYNC_DECOUPLED_FLOW
    retry_manager_stop_context(&priv_data.retry_contexts.time_sync); // Time-sync poll (decoupled flow)
#endif
#if CONFIG_RMNG_HOST_CTRL
    retry_manager_stop_context(&priv_data.retry_contexts.indexed_shadow_subscribe); // Indexed shadow subscribe retry
#endif /* CONFIG_RMNG_HOST_CTRL */

    /* Unregister the MQTT on complete event handler (not critical) */
    rmaker_err = event_loop_unregister_mqtt_on_complete_handler(__esp_rmaker_mqtt_cloud_on_complete_event_handler);
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to unregister MQTT on complete event handler: %d", rmaker_err);
    }

    /* Stop listening for state changes (not critical) */
    OSAL_LOGD(TAG, "Stopping listening for state changes...");
    rmaker_err = esp_rmaker_state_stop_listening(RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_CRITICAL);
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to stop listening for state changes");
        // goto stop_task_fail;
    }

    /* Stop reporting state changes and timeseries publishing (not critical) */
    OSAL_LOGD(TAG, "Stopping reporting state changes and timeseries publishing...");
    rmaker_err = esp_rmaker_state_stop_reporting();
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to stop reporting state changes");
        // goto stop_task_fail;
    }

    /* Stop timeseries publishing (not critical) */
    OSAL_LOGD(TAG, "Stopping timeseries publishing...");
    rmaker_err = timeseries_stop();
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to stop timeseries publishing");
        // goto stop_task_fail;
    }

    /* Stop listening on named shadow (not critical) */
    OSAL_LOGD(TAG, "Stopping listening on named shadow...");
    rmaker_err = esp_rmaker_named_shadow_unsubscribe_get_accepted(RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_CRITICAL);
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to stop listening on named shadow");
        // goto stop_task_fail;
    }

    /* Stop listening for cloud events (not critical) */
    OSAL_LOGD(TAG, "Stopping listening for cloud events...");
    rmaker_err = esp_rmaker_cloud_manager_stop_listening(RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_CRITICAL);
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to stop listening for cloud events");
        // goto stop_task_fail;
    }

#if CONFIG_RMNG_HOST_CTRL
    /* Unsubscribe from the indexed shadow (not critical) */
    OSAL_LOGD(TAG, "Unsubscribing from the indexed shadow...");
    rmaker_err = esp_rmaker_indexed_shadow_unsubscribe_get_accepted(RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_CRITICAL);
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to unsubscribe from the indexed shadow");
        // goto stop_task_fail;
    }
#endif /* CONFIG_RMNG_HOST_CTRL */

    /* Disconnect from MQTT */
    OSAL_LOGD(TAG, "Disconnecting from MQTT broker...");
#ifdef CONFIG_ESP_RMAKER_MQTT_ENABLE_BUDGETING
    // Stop the MQTT budget reviver
    mqtt_budget_impl_stop_reviver();
#endif /* CONFIG_ESP_RMAKER_MQTT_ENABLE_BUDGETING */
    mqtt_err = esp_rmaker_mqtt_impl.disconnect();
    if (mqtt_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to disconnect from MQTT");
        goto stop_task_fail;
    }
    if (!osal_mqtt_event_wait_client_has_disconnected( UINT32_MAX )) { /**< Infinite timeout */
        OSAL_LOGW(TAG, "MQTT client did not disconnect within timeout");
    } else {
        OSAL_LOGI(TAG, "Disconnected from MQTT broker");
    }

    has_disconnected_before = false; // Reset the disconnected flag

    /* De-initialize shadows */
    OSAL_LOGD(TAG, "De-initializing shadows...");
    rmaker_err = esp_rmaker_shadows_deinit();
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize shadows");
        goto stop_task_fail;
    }

    /* Stop the Work Queue */
    OSAL_LOGD(TAG, "Stopping Work Queue...");
    rmaker_err = esp_rmaker_work_queue_stop();
    if (rmaker_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to stop Work Queue");
        goto stop_task_fail;
    }

    /* Set state to stopped */
    priv_data.trackers.state = ESP_RMAKER_STATE_STOPPED;
    OSAL_LOGI(TAG, "ESP RainMaker stopped");

    esp_rmaker_event_flags_set_stopped();
    return;

stop_task_fail:
    priv_data.trackers.state = ESP_RMAKER_STATE_ERROR;
}

static void esp_rmaker_on_reconnect_task(void *unused)
{
    /* Attempt to get cloud information using a retry manager */
    retry_manager_execute_context(&priv_data.retry_contexts.cloud_setup);
#if CONFIG_RMNG_HOST_CTRL
    /* Attempt to subscribe to the indexed shadow (for use with remote control) */
    retry_manager_execute_context(&priv_data.retry_contexts.indexed_shadow_subscribe);
#endif /* CONFIG_RMNG_HOST_CTRL */
}

static esp_rmaker_error_t esp_rmaker_get_cloud_info(void)
{
    esp_rmaker_cloud_event_t events[7];
    esp_rmaker_cloud_event_t *p_event = events;
    esp_rmaker_error_t err = esp_rmaker_cloud_event_getGroupInfo(p_event++);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add getGroupInfo event");
        goto get_cloud_info_fail;
    }
    err = esp_rmaker_cloud_event_getAlexaEn(p_event++);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add getAlexaEn event");
        goto get_cloud_info_fail;
    }
    err = esp_rmaker_cloud_event_getGVAEn(p_event++);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add getGVAEn event");
        goto get_cloud_info_fail;
    }
    err = esp_rmaker_cloud_event_getSTEn(p_event++);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add getSTEn event");
        goto get_cloud_info_fail;
    }
    err = esp_rmaker_cloud_event_getSchedVer(p_event++);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add getSchedVer event");
        goto get_cloud_info_fail;
    }
    err = esp_rmaker_cloud_event_getTriggerVer(p_event++);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add getTriggerVer event");
        goto get_cloud_info_fail;
    }
    /* Request server time only when the clock is not yet valid (at populate
     * time). Once synced, SNTP is authoritative and the round-trip is
     * skipped. Requested in both time-sync flows. */
    if (!osal_timesync_is_synced()) {
        err = esp_rmaker_cloud_event_getTimeSync(p_event++);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to add getTimeSync event");
            goto get_cloud_info_fail;
        }
    }
    err = esp_rmaker_cloud_manager_send(&esp_rmaker_topic_ctx_self, events, (size_t)(p_event - events), MQTT_CHANNEL_SUB_CLOUD_MANAGER_CLOUD_SETUP);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to send cloud events");
        goto get_cloud_info_fail;
    }
    return ESP_RMAKER_OK;
get_cloud_info_fail:
    return err;
}

static void esp_rmaker_cloud_setup_on_failure(void)
{
    OSAL_LOGW(TAG, "Cloud setup failed. Setting cloud information to defaults and clearing any flags that were set");
    /* Set the cloud information to defaults and clear any flags that were set */
    esp_rmaker_cloud_events_tracker_t tracker = {0};
    esp_rmaker_cloud_event_response_getGroupInfo(&tracker, "", NULL, 0);
    esp_rmaker_event_flags_clear_group_info_received();
    esp_rmaker_cloud_event_response_getAlexaEn(&tracker, false);
    esp_rmaker_event_flags_clear_alexa_enabled_received();
    esp_rmaker_cloud_event_response_getGVAEn(&tracker, false);
    esp_rmaker_event_flags_clear_gva_enabled_received();
    esp_rmaker_cloud_event_response_getSTEn(&tracker, false);
    esp_rmaker_event_flags_clear_st_enabled_received();
    esp_rmaker_cloud_event_response_getSchedVer(&tracker, -1);
    esp_rmaker_event_flags_clear_sched_version_received();
    esp_rmaker_cloud_event_response_getTriggerVer(&tracker, -1);
    esp_rmaker_event_flags_clear_trigger_version_received();
}

static esp_rmaker_error_t esp_rmaker_cloud_setup_work_fn(void *unused)
{
    esp_rmaker_error_t err;

    if (!esp_rmaker_core_is_subscribed_to_cloud()) {
        err = esp_rmaker_cloud_manager_start_listening(RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_CRITICAL);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to start listening for cloud events");
            goto esp_rmaker_cloud_setup_work_fn_end;
        }
    }

    err = esp_rmaker_get_cloud_info();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get cloud information");
        goto esp_rmaker_cloud_setup_work_fn_end;
    }

    /* Cloud is now listening + cloud-info has been fetched. Kick the
     * node-config drain here, after the cloud is genuinely ready -
     * firing earlier (from on_first_mqtt_connection) races subscribe
     * ack and the first drain rejects with "cloud manager not listening". */
    esp_rmaker_core_kick_node_config_retry();

esp_rmaker_cloud_setup_work_fn_end:
    return err;
}

static esp_rmaker_error_t esp_rmaker_node_config_report_work_fn(void *unused)
{
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    /* Drain self + every pending child ctx in one tick. The pending
     * module dedups via per-ctx checksum compare and clears entries
     * on clean ack. */
    return node_config_pending_fire_all();
#else
    return esp_rmaker_report_node_config();
#endif
}

void esp_rmaker_core_kick_node_config_retry(void)
{
    retry_manager_execute_context(&priv_data.retry_contexts.node_config);
}

#if TIME_SYNC_DECOUPLED_FLOW
static esp_rmaker_error_t esp_rmaker_time_sync_work_fn(void *unused)
{
    (void)unused;
    if (!osal_timesync_is_synced()) {
        /* Not yet synced: fail so the retry manager reschedules on the
         * flat poll cadence. No max-attempts -> retries forever. */
        return ESP_RMAKER_FAIL;
    }
    OSAL_LOGI(TAG, "Time synchronized; arming deferred schedules.");
    esp_rmaker_schedule_service_arm_all();
    return ESP_RMAKER_OK; /* stop the poll */
}
#endif

#if CONFIG_RMNG_HOST_CTRL
static esp_rmaker_error_t esp_rmaker_indexed_shadow_subscribe_work_fn(void *unused)
{
    return esp_rmaker_indexed_shadow_subscribe_get_accepted(RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_CRITICAL);
}
#endif /* CONFIG_RMNG_HOST_CTRL */

static esp_rmaker_error_t esp_rmaker_register_node(const esp_rmaker_node_t *node)
{
    if (priv_data.node) {
        OSAL_LOGE(TAG, "A node has already been registered. Cannot register another.");
        return ESP_RMAKER_FAIL;
    }
    if (!node) {
        OSAL_LOGE(TAG, "Node handle cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    priv_data.node = node;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t esp_rmaker_check_and_report_online_status(void)
{
    if ( (priv_data.trackers.state == ESP_RMAKER_STATE_STARTED) // SDK is started
            && (priv_data.node != NULL) // Node is registered
            && ((priv_data.sub_flags & ESP_RMAKER_SUB_ALL) == ESP_RMAKER_SUB_ALL) // All required subscriptions are active
       ) {
        OSAL_LOGD(TAG, "Reporting node as online");
        esp_rmaker_node_set_online(true);
        esp_rmaker_event_flags_set_online();
        esp_rmaker_state_mark_for_update_online_for_node(NULL, true);
        return ESP_RMAKER_OK;
    }

    OSAL_LOGD(TAG, "Node is not online: state: %d, node: %p, sub_flags: 0x%02x", priv_data.trackers.state, priv_data.node, priv_data.sub_flags);
    esp_rmaker_node_set_online(false);
    return ESP_RMAKER_OK;
}

/* Public function definitions *****************************************************/

char *esp_rmaker_get_node_id(void)
{
    char *thing_name = NULL;
    esp_rmaker_error_t err = esp_rmaker_credentials_get_thing_name(&thing_name);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get thing name");
        return NULL;
    }
    return thing_name;
}

esp_rmaker_error_t esp_rmaker_pre_prov_init(void)
{
    if (priv_data.trackers.is_init_pre_prov) {
        // already initialized, do nothing
        return ESP_RMAKER_OK;
    }

    /* Initialise common components */
    esp_rmaker_error_t common_err = __init_common_components();
    if (common_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize common components");
        return common_err;
    }

    /* Initialise factory partition */
    OSAL_LOGD(TAG, "Initializing factory partition...");
    esp_rmaker_error_t factory_part_err = esp_rmaker_factory_part_init();
    if (factory_part_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize factory partition");
        return factory_part_err;
    }

    /* Initialise challenge response */
    OSAL_LOGD(TAG, "Initializing challenge response...");
    esp_rmaker_error_t chal_resp_err = esp_rmaker_chal_resp_init();
    if (chal_resp_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize challenge response");
        return chal_resp_err;
    }

#if CONFIG_ESP_RMAKER_ASSISTED_CLAIM
    /* Initialise assisted claiming, but only if the node has no certificate yet. Once a
     * node is claimed the claiming code stays dormant: no key generation, no endpoint and
     * nothing to wait for in esp_rmaker_pre_prov_deinit().
     *
     * Registered after challenge-response so that provisioning endpoint UUIDs, which are
     * assigned in creation order, stay stable. The phone app resolves endpoints by name,
     * so the order is not load-bearing, only reproducible. */
    esp_rmaker_credential_t existing_cert = { 0 };
    if (esp_rmaker_credentials_get_client_cert(&existing_cert) == ESP_RMAKER_OK) {
        esp_rmaker_credentials_free_credential(&existing_cert);
        OSAL_LOGD(TAG, "Node already has a certificate; skipping assisted claiming.");
    } else {
        OSAL_LOGD(TAG, "Initializing assisted claiming...");
        priv_data.claim_data = esp_rmaker_assisted_claim_init();
        if (!priv_data.claim_data) {
            OSAL_LOGE(TAG, "Failed to initialize assisted claiming");
            return ESP_RMAKER_FAIL;
        }
    }
#endif /* CONFIG_ESP_RMAKER_ASSISTED_CLAIM */

    priv_data.trackers.is_init_pre_prov = true;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_pre_prov_deinit(void)
{
    if (!priv_data.trackers.is_init_pre_prov) {
        OSAL_LOGE(TAG, "ESP RainMaker is not initialized for pre-provisioning.");
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Teardown runs to completion even when a step fails: leaving challenge-response or the
     * factory partition initialised, and is_init_pre_prov set, would strand pre-provisioning
     * resources for the rest of the boot. The first error is remembered and returned. */
    esp_rmaker_error_t first_err = ESP_RMAKER_OK;

#if CONFIG_ESP_RMAKER_ASSISTED_CLAIM
    /* Wait for claiming to finish before tearing anything down: claiming writes to the
     * factory partition, so it needs that still open, and the credentials it stores must be
     * in place before esp_rmaker_node_init() reads them. */
    if (priv_data.claim_data) {
        esp_rmaker_error_t claim_err = esp_rmaker_assisted_claim_perform(priv_data.claim_data);
        priv_data.claim_data = NULL; /* perform() takes ownership and frees it */
        if (claim_err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Assisted claiming did not complete: %d", claim_err);
            first_err = claim_err;
        }
    }
#endif /* CONFIG_ESP_RMAKER_ASSISTED_CLAIM */

    /* De-initialize challenge response */
    esp_rmaker_error_t chal_resp_err = esp_rmaker_chal_resp_deinit();
    if (chal_resp_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize challenge response");
        if (first_err == ESP_RMAKER_OK) {
            first_err = chal_resp_err;
        }
    }

    /* De-initialize factory partition */
    esp_rmaker_error_t factory_part_err = esp_rmaker_factory_part_deinit();
    if (factory_part_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize factory partition");
        if (first_err == ESP_RMAKER_OK) {
            first_err = factory_part_err;
        }
    }

    priv_data.trackers.is_init_pre_prov = false;
    return first_err;
}

esp_rmaker_node_t *esp_rmaker_node_init(const esp_rmaker_config_t *config, const char *name, const char *type)
{
    OSAL_LOGI(TAG, "Initializing RMNG firmware SDK (version %s)...", ESP_RMAKER_VERSION_STR);

    esp_rmaker_node_t *node = NULL;

    esp_rmaker_error_t err = esp_rmaker_init(config);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize ESP RainMaker");
        return NULL;
    }

    /* Print the version together with the non-sensitive factory configuration.
     * conn_params is populated by esp_rmaker_init above; on missing/undetectable
     * config esp_rmaker_init aborts and frees, so we never reach here. Secrets
     * (cert/key/username/password) are intentionally not logged. */
    const osal_mqtt_conn_params_t *cp = priv_data.mqtt_conn_params;
    OSAL_LOGI(TAG,
              "\n========= RMNG firmware SDK initialized =========\n\n"
              "\tVersion:       %s\n"
              "\tNode ID:       %s\n"
              "\tMQTT endpoint: %s:%" PRIu16  "\n"
              "\n=================================================",
              ESP_RMAKER_VERSION_STR,
              (cp && cp->client_id) ? cp->client_id : "<unknown>",
              (cp && cp->hostname) ? cp->hostname : "<unknown>",
              cp ? cp->port : 0U);

    node = esp_rmaker_node_create(name, type);
    if (!node) {
        OSAL_LOGE(TAG, "Failed to create node");
        goto fail;
    }
    err = esp_rmaker_register_node(node);
    if (err != ESP_RMAKER_OK) {
        goto fail;
    }

    /* Setup cloud retry context */
    priv_data.retry_contexts.cloud_setup = (retry_manager_context_t) {
        .backoff = {
            .base_delay_ms = RMAKER_CORE_CLOUD_SETUP_BASE_DELAY_MS,
            .reset_on_success = false,
            .ctx = ESP_RMAKER_BACKOFF_DEFAULT_RETRY_CONTEXT(),
        },
        .task = {
            .func = esp_rmaker_cloud_setup_work_fn,
            .priv_data = NULL,
        },
        .callbacks = {
            .on_failure = esp_rmaker_cloud_setup_on_failure,
        },
    };

    /* Setup node configuration retry context */
    priv_data.retry_contexts.node_config = (retry_manager_context_t) {
        .backoff = {
            .base_delay_ms = RMAKER_CORE_NODE_CONFIG_REPORT_BASE_DELAY_MS,
            .reset_on_success = false,
            .ctx = ESP_RMAKER_BACKOFF_DEFAULT_RETRY_CONTEXT(),
        },
        .task = {
            .func = esp_rmaker_node_config_report_work_fn,
            .priv_data = NULL,
        },
        .callbacks = {
            .on_failure = NULL,
        },
    };

#if TIME_SYNC_DECOUPLED_FLOW
    /* Setup time-sync poll retry context (decoupled flow only). Flat cadence:
     * exp_factor 1, no jitter, current == max == the configured interval,
     * so every reschedule fires at exactly CONFIG_RMNG_TIME_SYNC_POLL_INTERVAL_S.
     * The work fn returns FAIL until synced (retries forever) then OK to
     * stop. */
    {
        const uint64_t __ts_poll_ms = (uint64_t)CONFIG_RMNG_TIME_SYNC_POLL_INTERVAL_S * 1000ULL;
        priv_data.retry_contexts.time_sync = (retry_manager_context_t) {
            .backoff = {
                .base_delay_ms = __ts_poll_ms,
                .reset_on_success = false,
                .ctx = {
                    .handle = NULL,
                    .delay_ctx = {
                        .delay_ms = { .current = __ts_poll_ms, .max = __ts_poll_ms },
                        .params = { .exp_factor = 1, .max_jitter_ms = 0 },
                    },
                },
            },
            .task = {
                .func = esp_rmaker_time_sync_work_fn,
                .priv_data = NULL,
            },
            .callbacks = {
                .on_failure = NULL,
            },
        };
    }
#endif /* TIME_SYNC_DECOUPLED_FLOW */

#if CONFIG_RMNG_HOST_CTRL
    /* Setup indexed shadow subscribe retry context */
    priv_data.retry_contexts.indexed_shadow_subscribe = (retry_manager_context_t) {
        .backoff = {
            .base_delay_ms = RMAKER_CORE_INDEXED_SHADOW_SUBSCRIBE_BASE_DELAY_MS,
            .reset_on_success = false,
            .ctx = ESP_RMAKER_BACKOFF_DEFAULT_RETRY_CONTEXT(),
        },
        .task = {
            .func = esp_rmaker_indexed_shadow_subscribe_work_fn,
            .priv_data = NULL,
        },
        .callbacks = {
            .on_failure = NULL,
        },
    };
#endif /* CONFIG_RMNG_HOST_CTRL */

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    /* Initialise the node-config pending list eagerly so the very first
     * retry tick has the self ctx seeded. */
    err = node_config_pending_init();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "node_config_pending_init failed: %d", err);
        goto fail;
    }
#endif

    priv_data.trackers.is_init_full = true;
    priv_data.trackers.state = ESP_RMAKER_STATE_INIT_DONE;

    /* Post the core initialized event */
    osal_event_post(RMAKER_EVENT, RMAKER_EVENT_INIT_DONE, NULL, 0, OSAL_MAX_DELAY);

    return node;

fail:
    /* Teardown created components */
    if (esp_rmaker_deinit() != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Teardown after failed node init reported errors");
    }
    if (node) {
        esp_rmaker_node_delete(node);
        priv_data.node = NULL;
    }
    return NULL;
}

esp_rmaker_error_t esp_rmaker_node_deinit(const esp_rmaker_node_t *node)
{
    if (!priv_data.trackers.is_init_full) {
        OSAL_LOGE(TAG, "ESP RainMaker is not initialized. Please initialize it first.");
        return ESP_RMAKER_INVALID_STATE;
    }

    if (priv_data.trackers.state == ESP_RMAKER_STATE_DEINIT) {
        OSAL_LOGE(TAG, "ESP RainMaker already de-initialized.");
        return ESP_RMAKER_INVALID_ARG;
    }

    if (priv_data.trackers.state == ESP_RMAKER_STATE_STOP_REQUESTED) {
        OSAL_LOGD(TAG, "ESP RainMaker is stopping. Waiting for it to stop...");
        while (priv_data.trackers.state != ESP_RMAKER_STATE_STOPPED && priv_data.trackers.state != ESP_RMAKER_STATE_ERROR) {
            osal_task_delay(osal_ticks_from_ms(500));
        }
    }

    if (priv_data.trackers.state == ESP_RMAKER_STATE_STARTED || priv_data.trackers.state == ESP_RMAKER_STATE_STARTING) {
        OSAL_LOGE(TAG, "ESP RainMaker is still running. Please stop it first.");
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Deinit BEFORE freeing the node. */
    esp_rmaker_error_t err = esp_rmaker_deinit();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to de-initialize ESP RainMaker");
        return err;
    }

    /* De-allocate node (safe: worker joined, no report can be running) */
    err = esp_rmaker_node_delete(node);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to delete node");
        return err;
    }
    priv_data.node = NULL;

    priv_data.trackers.is_init_full = false;
    priv_data.trackers.state = ESP_RMAKER_STATE_DEINIT;
    return ESP_RMAKER_OK;
}

const esp_rmaker_node_t *esp_rmaker_get_node(void)
{
    return priv_data.node;
}

const esp_rmaker_topic_ctx_t *esp_rmaker_node_topic_ctx(const esp_rmaker_node_t *node)
{
    if (!node) {
        return NULL;
    }
    /* Self node aliases the program-lifetime ``esp_rmaker_topic_ctx_self``
     * singleton so identity-by-pointer compares against either form
     * resolve to the same ctx. Cloud-manager dispatch + event-response
     * inbox both key on the singleton; reporting code key off the node. */
    if (esp_rmaker_node_is_self(node)) {
        return &esp_rmaker_topic_ctx_self;
    }
    return &((const _esp_rmaker_node_t *)node)->topic_ctx;
}

bool esp_rmaker_node_is_self(const esp_rmaker_node_t *node)
{
    if (!node) {
        return false;
    }
    return ((const _esp_rmaker_node_t *)node)->topic_ctx.ops == &esp_rmaker_topic_ops_self;
}

int esp_rmaker_node_resolve_thing_name(const esp_rmaker_node_t *node, char *buf, size_t buf_size)
{
    return esp_rmaker_topic_ctx_resolve_thing_name(esp_rmaker_node_topic_ctx(node), buf, buf_size);
}

void esp_rmaker_node_for_each(esp_rmaker_node_visitor_t visitor, void *priv)
{
    if (!visitor) {
        return;
    }
    if (priv_data.node) {
        (void)visitor(priv_data.node, priv);
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    bridge_internal_for_each_ready_node(visitor, priv);
#endif
}

bool esp_rmaker_node_is_in_subgroup(const esp_rmaker_node_t *node, const char *subgroup)
{
    if (!node || !subgroup || subgroup[0] == '\0') {
        return false;
    }
    char *group_info_str = NULL;
    bool free_after = false;
    if (esp_rmaker_node_is_self(node)) {
        group_info_str = esp_rmaker_local_config_get_group_info_str();
        free_after = (group_info_str != NULL);
    } else {
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
        if (child) {
            group_info_str = (char *)bridge_internal_child_group_info_str(child);
        }
#endif
    }
    if (!group_info_str || group_info_str[0] == '\0') {
        if (free_after) {
            free(group_info_str);
        }
        return false;
    }
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
    char subgroups[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE];
    size_t num_subgroups = 0;
    bool found = false;
    if (esp_rmaker_local_config_parse_group_info_str(group_info_str,
            primary, sizeof(primary),
            subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT,
            &num_subgroups) == ESP_RMAKER_OK) {
        for (size_t i = 0; i < num_subgroups; i++) {
            if (strcmp(subgroups[i], subgroup) == 0) {
                found = true;
                break;
            }
        }
    }
    if (free_after) {
        free(group_info_str);
    }
    return found;
}

esp_rmaker_error_t esp_rmaker_start(void)
{
    OSAL_LOGD(TAG, "Starting ESP RainMaker...");
    if (!priv_data.trackers.is_init_full) {
        OSAL_LOGE(TAG, "ESP RainMaker is not initialized. Please initialize it first.");
        return ESP_RMAKER_INVALID_STATE;
    }

    if (priv_data.trackers.state == ESP_RMAKER_STATE_STARTED) {
        OSAL_LOGW(TAG, "ESP RainMaker is already started.");
        return ESP_RMAKER_OK;
    }

    /* Add work function to start the ESP RainMaker SDK */
    if (!priv_data.trackers.start_task_in_queue) {
        esp_rmaker_work_queue_add_task(esp_rmaker_start_task, NULL);
    }

    /* Start Work Queue */
    OSAL_LOGD(TAG, "Starting Work Queue...");
    esp_rmaker_error_t work_queue_err = esp_rmaker_work_queue_start();
    if (work_queue_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to start Work Queue");
        return work_queue_err;
    }

    priv_data.trackers.state = ESP_RMAKER_STATE_STARTING;
    return ESP_RMAKER_OK;
}


esp_rmaker_error_t esp_rmaker_stop(void)
{
    if (priv_data.trackers.state != ESP_RMAKER_STATE_STARTED) {
        OSAL_LOGE(TAG, "ESP RainMaker is not started. Please start it first.");
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Close the runtime gate so deferrable work may stop. */
    esp_rmaker_runtime_gate_set_active(false);

    /* Add work function to stop the ESP RainMaker SDK */
    esp_rmaker_work_queue_add_task(esp_rmaker_stop_task, NULL);

    priv_data.trackers.state = ESP_RMAKER_STATE_STOP_REQUESTED;
    return ESP_RMAKER_OK;
}

/* --- Checking online status --- */

void esp_rmaker_core_subscribed_to_params(void)
{
    esp_rmaker_network_clear_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_STATE_CHANGES);
    esp_rmaker_network_set_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_STATE_CHANGES);
    priv_data.sub_flags |= ESP_RMAKER_SUB_PARAMS;
    esp_rmaker_check_and_report_online_status();
    esp_rmaker_event_flags_set_state_started_listening();
}

void esp_rmaker_core_unsubscribed_from_params(void)
{
    esp_rmaker_network_clear_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_STATE_CHANGES);
    esp_rmaker_network_set_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_STATE_CHANGES);
    priv_data.sub_flags &= ~ESP_RMAKER_SUB_PARAMS;
    esp_rmaker_check_and_report_online_status();
}

bool esp_rmaker_core_is_subscribed_to_params(void)
{
    return priv_data.sub_flags & ESP_RMAKER_SUB_PARAMS;
}

void esp_rmaker_core_subscribed_to_cloud(void)
{
    esp_rmaker_network_clear_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_CLOUD);
    esp_rmaker_network_set_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_CLOUD);
    priv_data.sub_flags |= ESP_RMAKER_SUB_CLOUD;
    esp_rmaker_check_and_report_online_status();
}

void esp_rmaker_core_unsubscribed_from_cloud(void)
{
    esp_rmaker_network_clear_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_CLOUD);
    esp_rmaker_network_set_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_CLOUD);
    priv_data.sub_flags &= ~ESP_RMAKER_SUB_CLOUD;
    esp_rmaker_check_and_report_online_status();
}

bool esp_rmaker_core_is_subscribed_to_cloud(void)
{
    return priv_data.sub_flags & ESP_RMAKER_SUB_CLOUD;
}

/* --- Signing challenges --- */

esp_rmaker_error_t esp_rmaker_core_sign_challenge(const uint8_t *challenge, size_t challenge_len, uint8_t **signature, size_t *signature_len)
{
    if (!challenge || challenge_len == 0 || !signature || !signature_len) {
        OSAL_LOGE(TAG, "Invalid arguments: challenge=%p, challenge_len=%d, signature=%p, signature_len=%p", challenge, (int)challenge_len, signature, signature_len);
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    esp_rmaker_credential_t private_key;
    err = esp_rmaker_credentials_get_private_key(&private_key);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get private key");
        return err;
    }

    err = esp_rmaker_crypto_sign_data(private_key.credential, private_key.len, challenge, challenge_len, signature, signature_len);
    esp_rmaker_credentials_free_credential(&private_key);
    return err;
}
