/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cloud_events.c
 * @brief Implementation of the cloud events.
 */

/* Declarations */
#include "network/cloud/events.h"

/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Local configuration includes */
#include "local_config.h"

/* Platform includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Network includes */
#include "network/common.h"
#include "network/cloud/manager.h"
#include "constants/identity.h"
#include "network/state_changes.h"
#include "network/shadows.h"
#include "constants/network.h"

/* Core internal includes */
#include "core_internal.h"

/* NVS includes */
#include "constants/nvs.h"

/* Event flags includes */
#include "event_flags.h"

/* Time sync includes */
#include "osal_timesync.h"

/* Services includes */
#include "services/schedules.h"
#include "services/automation.h"

/* Bridge includes (per-ctx version state lives on the child slot) */
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "bridge/bridge_internal.h"
#include "bridge/bridge_child_nvs.h"
#endif

/* Private types ****************************************************************/

/**
 * @brief Self-only version progress: mirrors esp_rmaker_bridge_version_progress_t
 *        but keyed by NVS key (for the device's own local_config namespace).
 * - Save to NVS only after both the version and details are new. Details
 *   saving is handled by the service.
 * - When a new version/details is received, if the other is not new,
 *   then queue the appropriate cloud event.
 */
typedef struct {
    bool is_new_version;
    bool is_new_details;
    int  pending_version;        /**< Tentative version awaiting commit, -1 if unset. */
    const char *nvs_key_version; /**< local_config NVS key for the committed version. */
} __version_self_progress_t;

/* Global variables ***************************************************************/

/**
 * @brief Cloud event builders. Indexed by RMAKER_CLOUD_EVENT_FLAG_POS_*.
 */
const esp_rmaker_cloud_event_builder_t RMAKER_CLOUD_EVENT_BUILDERS[] = {
    esp_rmaker_cloud_event_getGroupInfo,
    esp_rmaker_cloud_event_getAlexaEn,
    esp_rmaker_cloud_event_getSchedVer,
    esp_rmaker_cloud_event_getSchedDetails,
    esp_rmaker_cloud_event_getTriggerVer,
    esp_rmaker_cloud_event_getTriggerDetails,
    esp_rmaker_cloud_event_getGVAEn,
    esp_rmaker_cloud_event_getTimeSync,
    esp_rmaker_cloud_event_getSTEn,
};

/* Private variables **************************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "rmng_net_cloud_evt";

/**
 * @brief Self-only version-handshake progress for the two services that
 *        use the version/details handshake.
 */
static __version_self_progress_t __self_schedule_progress = {
    .is_new_version = false,
    .is_new_details = false,
    .pending_version = -1,
    .nvs_key_version = RMAKER_NVS_LOCAL_CONFIG_KEY_SCHED_VER,
};

static __version_self_progress_t __self_trigger_progress = {
    .is_new_version = false,
    .is_new_details = false,
    .pending_version = -1,
    .nvs_key_version = RMAKER_NVS_LOCAL_CONFIG_KEY_TRIGGER_VER,
};

/**
 * @brief Which version-handshake stream a response applies to.
 */
typedef enum {
    __VERSION_KIND_SCHED = 0,
    __VERSION_KIND_TRIGGER = 1,
} __version_kind_t;

/* Private function declarations ***************************************************/

/* --- Versioning --- */

/**
 * @brief Read the persisted version for (ctx, kind).
 * @return Stored version, or -1 if absent.
 */
static int __version_persisted_get(const esp_rmaker_topic_ctx_t *ctx, __version_kind_t kind);

/**
 * @brief Persist a committed version for (ctx, kind).
 */
static void __version_persisted_set(const esp_rmaker_topic_ctx_t *ctx, __version_kind_t kind, int version);

/**
 * @brief Drive the version side of the (version, details) handshake.
 */
static void __on_version_updated(const esp_rmaker_topic_ctx_t *ctx, __version_kind_t kind,
                                 esp_rmaker_cloud_events_tracker_t *p_events_tracker,
                                 esp_rmaker_cloud_event_flag_pos_t details_flag_pos, int version);

/**
 * @brief Drive the details side of the (version, details) handshake.
 */
static void __on_details_updated(const esp_rmaker_topic_ctx_t *ctx, __version_kind_t kind,
                                 esp_rmaker_cloud_events_tracker_t *p_events_tracker,
                                 esp_rmaker_cloud_event_flag_pos_t version_flag_pos, int version);

/* --- Shorthand macros --- */

#define __on_schedule_version_updated(ctx, p_events_tracker, version) \
    __on_version_updated((ctx), __VERSION_KIND_SCHED, (p_events_tracker), RMAKER_CLOUD_EVENT_FLAG_POS_getSchedDetails, (version))
#define __on_schedule_details_updated(ctx, p_events_tracker, version) \
    __on_details_updated((ctx), __VERSION_KIND_SCHED, (p_events_tracker), RMAKER_CLOUD_EVENT_FLAG_POS_getSchedVer, (version))
#define __on_trigger_version_updated(ctx, p_events_tracker, version) \
    __on_version_updated((ctx), __VERSION_KIND_TRIGGER, (p_events_tracker), RMAKER_CLOUD_EVENT_FLAG_POS_getTriggerDetails, (version))
#define __on_trigger_details_updated(ctx, p_events_tracker, version) \
    __on_details_updated((ctx), __VERSION_KIND_TRIGGER, (p_events_tracker), RMAKER_CLOUD_EVENT_FLAG_POS_getTriggerVer, (version))

/* Private function definitions ****************************************************/

/* Resolve the relevant in-memory progress slot for (ctx, kind).
 * Self ctx -> static singletons; child ctx -> field on the bridge slot.
 * Returns NULL if a child ctx can't be resolved (slot invalidated). */
typedef struct {
    bool *is_new_version;
    bool *is_new_details;
    int  *pending_version;
} __version_progress_view_t;

static __version_progress_view_t __resolve_progress_view(const esp_rmaker_topic_ctx_t *ctx, __version_kind_t kind)
{
    __version_progress_view_t v = {0};
    if (ctx == NULL || ctx == &esp_rmaker_topic_ctx_self) {
        __version_self_progress_t *p = (kind == __VERSION_KIND_SCHED)
                                       ? &__self_schedule_progress
                                       : &__self_trigger_progress;
        v.is_new_version = &p->is_new_version;
        v.is_new_details = &p->is_new_details;
        v.pending_version = &p->pending_version;
        return v;
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_ctx(ctx);
    if (!child) {
        return v;
    }
    bridge_version_kind_t bkind = (kind == __VERSION_KIND_SCHED)
                                  ? BRIDGE_VERSION_KIND_SCHED
                                  : BRIDGE_VERSION_KIND_TRIGGER;
    esp_rmaker_bridge_version_progress_t *p = bridge_internal_child_version_progress(child, bkind);
    if (!p) {
        return v;
    }
    v.is_new_version = &p->is_new_version;
    v.is_new_details = &p->is_new_details;
    v.pending_version = &p->pending_version;
#endif
    return v;
}

static void __reset_progress(const esp_rmaker_topic_ctx_t *ctx, __version_kind_t kind)
{
    __version_progress_view_t v = __resolve_progress_view(ctx, kind);
    if (v.is_new_version) {
        *v.is_new_version = false;
        *v.is_new_details = false;
        *v.pending_version = -1;
    }
}

static int __version_persisted_get(const esp_rmaker_topic_ctx_t *ctx, __version_kind_t kind)
{
    if (ctx == NULL || ctx == &esp_rmaker_topic_ctx_self) {
        const char *key = (kind == __VERSION_KIND_SCHED)
                          ? RMAKER_NVS_LOCAL_CONFIG_KEY_SCHED_VER
                          : RMAKER_NVS_LOCAL_CONFIG_KEY_TRIGGER_VER;
        return esp_rmaker_local_config_get_version(key);
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_ctx(ctx);
    if (!child) {
        return -1;
    }
    bridge_child_nvs_record_t r;
    if (bridge_child_nvs_load(child, &r) != ESP_RMAKER_OK) {
        return -1;
    }
    return (kind == __VERSION_KIND_SCHED) ? r.sched_ver : r.trigger_ver;
#else
    (void)kind;
    return -1;
#endif
}

static void __version_persisted_set(const esp_rmaker_topic_ctx_t *ctx, __version_kind_t kind, int version)
{
    if (ctx == NULL || ctx == &esp_rmaker_topic_ctx_self) {
        const char *key = (kind == __VERSION_KIND_SCHED)
                          ? RMAKER_NVS_LOCAL_CONFIG_KEY_SCHED_VER
                          : RMAKER_NVS_LOCAL_CONFIG_KEY_TRIGGER_VER;
        esp_rmaker_local_config_set_version(key, version);
        return;
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_ctx(ctx);
    if (!child) {
        return;
    }
    if (kind == __VERSION_KIND_SCHED) {
        bridge_child_nvs_set_sched_ver(child, (int32_t)version);
    } else {
        bridge_child_nvs_set_trigger_ver(child, (int32_t)version);
    }
#endif
}

static void __on_version_updated(const esp_rmaker_topic_ctx_t *ctx, __version_kind_t kind,
                                 esp_rmaker_cloud_events_tracker_t *p_events_tracker,
                                 esp_rmaker_cloud_event_flag_pos_t details_flag_pos, int version)
{
    __version_progress_view_t v = __resolve_progress_view(ctx, kind);
    if (!v.is_new_version) {
        return; /* unknown ctx */
    }

    if (version < 0) {
        /* A new version of -1 indicates that the version is not known. */
        __version_persisted_set(ctx, kind, version);
        __reset_progress(ctx, kind);
        return;
    }

    int current_version = __version_persisted_get(ctx, kind);
    OSAL_LOGI(TAG, "version handshake (kind=%d): cloud=%d cached=%d", (int)kind, version, current_version);

    bool is_new_version = current_version < 0 || version != current_version;
    if (!is_new_version) {
        __reset_progress(ctx, kind);
        return;
    }

    *v.pending_version = version;
    *v.is_new_version = true;
    if (!*v.is_new_details) {
        OSAL_LOGI(TAG, "version new, details not yet - queueing get*Details");
        p_events_tracker->events_pending |= (1 << details_flag_pos);
    } else {
        OSAL_LOGI(TAG, "version + details both new - committing version %d", version);
        __version_persisted_set(ctx, kind, version);
        __reset_progress(ctx, kind);
    }
}

static void __on_details_updated(const esp_rmaker_topic_ctx_t *ctx, __version_kind_t kind,
                                 esp_rmaker_cloud_events_tracker_t *p_events_tracker,
                                 esp_rmaker_cloud_event_flag_pos_t version_flag_pos, int version)
{
    __version_progress_view_t v = __resolve_progress_view(ctx, kind);
    if (!v.is_new_version) {
        return;
    }
    *v.is_new_details = true;
    if (version >= 0) {
        if (*v.is_new_version && *v.pending_version != version) {
            OSAL_LOGW(TAG, "get*Ver gave %d but get*Details gave %d; using details version",
                      *v.pending_version, version);
        }
        *v.is_new_version = true;
        *v.pending_version = version;
    }
    if (!*v.is_new_version) {
        p_events_tracker->events_pending |= (1 << version_flag_pos);
    } else {
        OSAL_LOGI(TAG, "details + version both new - committing version %d", *v.pending_version);
        __version_persisted_set(ctx, kind, *v.pending_version);
        __reset_progress(ctx, kind);
    }
}

/* Public function definitions ****************************************************/

esp_rmaker_error_t esp_rmaker_cloud_event_getGroupInfo(esp_rmaker_cloud_event_t *p_event)
{
    p_event->name = "getGroupInfo";
    p_event->data = NULL;
    p_event->p_set_response_cb_context = NULL;
    OSAL_LOGD(TAG, "Built getGroupInfo event");
    return ESP_RMAKER_OK;
}

/* Self-only handler. Owns: local_config NVS, named-shadow + state
 * subscriptions on the bridge / device's own connection, and the
 * primary-ID-changed force-reconnect path (which refreshes the device
 * IoT policy). */
static void __getGroupInfo_self(esp_rmaker_cloud_events_tracker_t *p_events_tracker,
                                const char *primary, const char *group_info_str)
{
    bool should_setup_for_group_info = false;
    char *old_group_info_str = esp_rmaker_local_config_get_group_info_str();
    if (old_group_info_str == NULL) {
        /* No old group info string found, so we can assume that the group info has changed */
        should_setup_for_group_info = true;
        OSAL_LOGI(TAG, "No old group info string found.");
    } else if (strcmp(old_group_info_str, group_info_str) != 0) {
        /* Old group info string found and it is different from the new group info string */
        OSAL_LOGI(TAG, "Group info has changed from '%s' to '%s' - this is a migration from an old group", old_group_info_str, group_info_str);

        /* Delete the old named shadow using the OLD group info (the
         * self ctx's group ops still read the unchanged local_config
         * value). */
        if (esp_rmaker_state_delete_named_shadow_for_node(esp_rmaker_get_node()) != ESP_RMAKER_OK) {
            OSAL_LOGW(TAG, "Failed to delete old named shadow on old group info '%s'", old_group_info_str);
        }

        /* Stop listening on the old state change topic (unicast + group control) */
        if (esp_rmaker_state_stop_listening(RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_NON_CRITICAL) != ESP_RMAKER_OK) {
            /* Failure to stop listening for state changes is not critical, but will take up stale space in the subscription list */
            OSAL_LOGW(TAG, "Failed to stop listening for state changes on old group info '%s', resulting in stale subscription", old_group_info_str);
        }

        /* Stop listening on the old named shadow */
        if (esp_rmaker_named_shadow_unsubscribe_get_accepted(RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_NON_CRITICAL) != ESP_RMAKER_OK) {
            /* Failure to stop listening on named shadow is not critical, but will take up stale space in the subscription list */
            OSAL_LOGW(TAG, "Failed to stop listening on named shadow on old group info '%s', resulting in stale subscription", old_group_info_str);
        }

        /* Primary-ID change forces a reconnect to refresh IoT policy. */
        bool primary_changed = false;
        if (primary[0] != '\0') {
            char old_primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
            size_t num_old_subgroups = 0;
            if (esp_rmaker_local_config_parse_group_info_str(old_group_info_str, old_primary, sizeof(old_primary), NULL, 0, &num_old_subgroups) == ESP_RMAKER_OK) {
                primary_changed = strcmp(old_primary, primary) != 0;
            } else {
                OSAL_LOGW(TAG, "Failed to parse old group info string; treating as a primary group ID change");
                primary_changed = true;
            }
        }

        /* If the primary group ID has changed, then we need to setup for the new group info */
        should_setup_for_group_info = !primary_changed;

        /* If the primary group ID has changed, then we need to force a reconnect to the cloud to refresh the device policy */
        if (primary_changed) {
            OSAL_LOGI(TAG, "Primary group ID has changed; forcing reconnect to cloud");
            osal_err_t mqtt_err = esp_rmaker_mqtt_impl.force_reconnect();
            if (mqtt_err != OSAL_ERR_OK) {
                OSAL_LOGW(TAG, "Failed to force reconnect to MQTT");
            }
        }

        /* Free the old group info string */
        free(old_group_info_str);
    } else {
        /* The group info string has not changed, so we should check if we need to setup for the new group info */
        /* This might occur if the device is being reset and the group info string is set in NVS */
        should_setup_for_group_info = !esp_rmaker_core_is_subscribed_to_params();
        free(old_group_info_str);
    }

    /* Set the group info string in local config NVS */
    if (esp_rmaker_local_config_set_group_info_str(group_info_str) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to set group info string in local config");
        return;
    }
    OSAL_LOGI(TAG, "Group info string set to '%s'", group_info_str);
    p_events_tracker->events_processed |= (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getGroupInfo);
    esp_rmaker_event_flags_set_group_info_received();

    /* Setup for new group info */
    if (should_setup_for_group_info) {
        /* Start listening on named shadow */
        if (esp_rmaker_named_shadow_subscribe_get_accepted(RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_CRITICAL) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to start listening for named shadow on new group info '%s'", group_info_str);
        }
        esp_rmaker_state_attempt_start_listening(RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_CRITICAL);
        esp_rmaker_state_schedule_report(true);
    }
}

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
/* Child handler. Children share the bridge's MQTT connection - no
 * unsubscribe / state stop_listening / force_reconnect concerns. Just:
 *   1. Compare old (in-memory) group_info_str against new.
 *   2. If migrating, delete the old named shadow via the child ctx
 *      BEFORE updating the in-memory string (the ctx ops read it).
 *   3. Update in-memory + per-child NVS to the new value.
 */
static void __getGroupInfo_child(esp_rmaker_cloud_events_tracker_t *p_events_tracker,
                                 esp_rmaker_bridge_child_handle_t child,
                                 const char *group_info_str)
{
    const esp_rmaker_node_t *child_node = bridge_internal_child_node(child);
    const char *old = bridge_internal_child_group_info_str(child);
    /* group_info_str is in-memory only (inits to ""), so we can't distinguish a
     * genuine first boot from a child explicitly demoted to the no-group ("")
     * state - both present old="". Treat any change incl. ""->X as a migration
     * and delete the old shadow. */
    bool migrated = (old != NULL) && (strcmp(old, group_info_str) != 0);

    if (migrated) {
        OSAL_LOGI(TAG, "Child group info migrating from '%s' to '%s'", old, group_info_str);
        /* Delete OLD shadow first - node's topic-ctx ops still read the
         * in-memory old value at this point. */
        if (esp_rmaker_state_delete_named_shadow_for_node(child_node) != ESP_RMAKER_OK) {
            OSAL_LOGW(TAG, "Failed to delete old child named shadow on group info '%s'", old);
        }
    }

    /* Update in-memory ctx state. */
    if (bridge_internal_child_set_group_info_str(child, group_info_str) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update child in-memory group_info_str");
        return;
    }

    OSAL_LOGI(TAG, "Child group info set to '%s'", group_info_str);
    p_events_tracker->events_processed |= (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getGroupInfo);

    /* Mirror self at the top of esp_rmaker_cloud_event_response_getGroupInfo:
     * once group info is confirmed by the cloud, schedule a full state
     * report for this child.
     *
     * Gate: fire on first set, on migration, OR on the first getGroupInfo
     * since boot. */
    const bool first_set = (old == NULL) || (old[0] == '\0');
    const bool needs_session_setup = !bridge_internal_child_group_setup_done(child);
    const bool committed = bridge_internal_child_devices_committed(child);
    /* Only report once the consumer has committed the child's devices, else the
     * report races an incomplete local view (e.g. bridge-side capability table not
     * yet built). If a getGroupInfo lands before commit, group_setup_done stays false
     * and commit_devices re-drives getGroupInfo, so the report fires post-commit. */
    if ((first_set || migrated || needs_session_setup) && committed) {
        if (esp_rmaker_state_schedule_report_for_node(child_node, true) != ESP_RMAKER_OK) {
            OSAL_LOGW(TAG, "Failed to schedule full state report for child");
        } else {
            bridge_internal_child_set_group_setup_done(child, true);
        }
    }

    /* Notify any registered observer (e.g. the remote-layer bridge
     * handler) that the child's group info has been updated. */
    bridge_internal_dispatch_child_event(child, BRIDGE_CHILD_EVENT_GROUP_INFO);
}
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

void esp_rmaker_cloud_event_response_getGroupInfo(esp_rmaker_cloud_events_tracker_t *p_events_tracker, const char *primary, char subgroups[][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE], size_t num_subgroups)
{
    char *group_info_str;
    if (esp_rmaker_local_config_format_group_info_str(primary, subgroups, num_subgroups, &group_info_str) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to format group info string");
        return;
    }

    const esp_rmaker_topic_ctx_t *ctx = esp_rmaker_cloud_events_tracker_ctx(p_events_tracker);
    if (ctx == &esp_rmaker_topic_ctx_self) {
        __getGroupInfo_self(p_events_tracker, primary, group_info_str);
    } else {
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_ctx(ctx);
        if (child) {
            __getGroupInfo_child(p_events_tracker, child, group_info_str);
        } else {
            char __tname[RMAKER_THING_NAME_BUFFER_SIZE];
            esp_rmaker_topic_ctx_resolve_thing_name(ctx, __tname, sizeof(__tname));
            OSAL_LOGW(TAG, "getGroupInfo: thing '%s' has no resolvable child", __tname);
        }
#else
        OSAL_LOGW(TAG, "getGroupInfo on non-self ctx without bridge support - ignoring");
#endif
    }

    free(group_info_str);
}

esp_rmaker_error_t esp_rmaker_cloud_event_getAlexaEn(esp_rmaker_cloud_event_t *p_event)
{
    p_event->name = "getAlexaEn";
    p_event->data = NULL;
    p_event->p_set_response_cb_context = NULL;
    OSAL_LOGD(TAG, "Built getAlexaEn event");
    return ESP_RMAKER_OK;
}

void esp_rmaker_cloud_event_response_getAlexaEn(esp_rmaker_cloud_events_tracker_t *p_events_tracker, bool alexa_en)
{
    OSAL_LOGI(TAG, "Updating Alexa enable: %d", alexa_en);
    esp_rmaker_local_config_set_alexa_en(alexa_en);
    p_events_tracker->events_processed |= (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getAlexaEn);
    esp_rmaker_event_flags_set_alexa_enabled_received();
}

esp_rmaker_error_t esp_rmaker_cloud_event_getGVAEn(esp_rmaker_cloud_event_t *p_event)
{
    p_event->name = "getGVAEn";
    p_event->data = NULL;
    p_event->p_set_response_cb_context = NULL;
    OSAL_LOGD(TAG, "Built getGVAEn event");
    return ESP_RMAKER_OK;
}

void esp_rmaker_cloud_event_response_getGVAEn(esp_rmaker_cloud_events_tracker_t *p_events_tracker, bool gva_en)
{
    OSAL_LOGI(TAG, "Updating GVA enable: %d", gva_en);
    esp_rmaker_local_config_set_gva_en(gva_en);
    p_events_tracker->events_processed |= (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getGVAEn);
    esp_rmaker_event_flags_set_gva_enabled_received();
}

esp_rmaker_error_t esp_rmaker_cloud_event_getTimeSync(esp_rmaker_cloud_event_t *p_event)
{
    p_event->name = "getTimeSync";
    p_event->data = NULL;
    p_event->p_set_response_cb_context = NULL;
    OSAL_LOGD(TAG, "Built getTimeSync event");
    return ESP_RMAKER_OK;
}

void esp_rmaker_cloud_event_response_getTimeSync(esp_rmaker_cloud_events_tracker_t *p_events_tracker, int64_t time_ms)
{
    if (!osal_timesync_is_synced()) {
        if (osal_timesync_set_time(time_ms) == 0) {
            OSAL_LOGI(TAG, "System time set from cloud");
        } else {
            OSAL_LOGW(TAG, "Failed to set system time from cloud");
        }
    } else {
        OSAL_LOGD(TAG, "Time already synchronized, ignoring cloud time");
    }
    p_events_tracker->events_processed |= (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getTimeSync);
}

esp_rmaker_error_t esp_rmaker_cloud_event_getSTEn(esp_rmaker_cloud_event_t *p_event)
{
    p_event->name = "getSTEn";
    p_event->data = NULL;
    p_event->p_set_response_cb_context = NULL;
    OSAL_LOGD(TAG, "Built getSTEn event");
    return ESP_RMAKER_OK;
}

void esp_rmaker_cloud_event_response_getSTEn(esp_rmaker_cloud_events_tracker_t *p_events_tracker, bool st_en)
{
    OSAL_LOGI(TAG, "Updating SmartThings enable: %d", st_en);
    esp_rmaker_local_config_set_st_en(st_en);
    p_events_tracker->events_processed |= (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getSTEn);
    esp_rmaker_event_flags_set_st_enabled_received();
}

esp_rmaker_error_t esp_rmaker_cloud_event_getSchedVer(esp_rmaker_cloud_event_t *p_event)
{
    p_event->name = "getSchedVer";
    p_event->data = NULL;
    p_event->p_set_response_cb_context = NULL;
    OSAL_LOGD(TAG, "Built getSchedVer event");
    return ESP_RMAKER_OK;
}

void esp_rmaker_cloud_event_response_getSchedVer(esp_rmaker_cloud_events_tracker_t *p_events_tracker, int sched_ver)
{
    const esp_rmaker_topic_ctx_t *ctx = esp_rmaker_cloud_events_tracker_ctx(p_events_tracker);
    __on_schedule_version_updated(ctx, p_events_tracker, sched_ver);
    p_events_tracker->events_processed |= (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getSchedVer);
    if (ctx == &esp_rmaker_topic_ctx_self) {
        esp_rmaker_event_flags_set_sched_version_received();
    }
}

esp_rmaker_error_t esp_rmaker_cloud_event_getSchedDetails(esp_rmaker_cloud_event_t *p_event)
{
    p_event->name = "getSchedDetails";
    p_event->data = NULL;
    p_event->p_set_response_cb_context = NULL;
    OSAL_LOGD(TAG, "Built getSchedDetails event");
    return ESP_RMAKER_OK;
}

void esp_rmaker_cloud_event_response_getSchedDetails(esp_rmaker_cloud_events_tracker_t *p_events_tracker, const char *sched_details, int version)
{
    const esp_rmaker_topic_ctx_t *ctx = esp_rmaker_cloud_events_tracker_ctx(p_events_tracker);
    char __tname_sd[RMAKER_THING_NAME_BUFFER_SIZE];
    esp_rmaker_topic_ctx_resolve_thing_name(ctx, __tname_sd, sizeof(__tname_sd));
    OSAL_LOGI(TAG, "Got new schedule details (thing=%s)", __tname_sd);
    OSAL_LOGD(TAG, "New schedule details: %s", sched_details);
    /* Resolve the incoming ctx to the owning node once, here at the cloud
     * boundary; the schedule service is node-keyed from this point on. */
    const esp_rmaker_node_t *sched_node = NULL;
    if (ctx == &esp_rmaker_topic_ctx_self) {
        sched_node = esp_rmaker_get_node();
    } else {
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_ctx(ctx);
        sched_node = child ? bridge_internal_child_node(child) : NULL;
#endif
    }
    if (sched_node) {
        esp_rmaker_schedule_service_update_details_for_node(sched_node, sched_details);
    } else {
        OSAL_LOGW(TAG, "Schedule details for thing '%s' - no resolvable node, dropping", __tname_sd);
    }
    __on_schedule_details_updated(ctx, p_events_tracker, version);
    p_events_tracker->events_processed |= (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getSchedDetails);
}

esp_rmaker_error_t esp_rmaker_cloud_event_getTriggerVer(esp_rmaker_cloud_event_t *p_event)
{
    p_event->name = "getTriggerVer";
    p_event->data = NULL;
    p_event->p_set_response_cb_context = NULL;
    OSAL_LOGD(TAG, "Built getTriggerVer event");
    return ESP_RMAKER_OK;
}

void esp_rmaker_cloud_event_response_getTriggerVer(esp_rmaker_cloud_events_tracker_t *p_events_tracker, int trigger_ver)
{
    const esp_rmaker_topic_ctx_t *ctx = esp_rmaker_cloud_events_tracker_ctx(p_events_tracker);
    __on_trigger_version_updated(ctx, p_events_tracker, trigger_ver);
    p_events_tracker->events_processed |= (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getTriggerVer);
    if (ctx == &esp_rmaker_topic_ctx_self) {
        esp_rmaker_event_flags_set_trigger_version_received();
    }
}

esp_rmaker_error_t esp_rmaker_cloud_event_getTriggerDetails(esp_rmaker_cloud_event_t *p_event)
{
    p_event->name = "getTriggerDetails";
    p_event->data = NULL;
    p_event->p_set_response_cb_context = NULL;
    OSAL_LOGD(TAG, "Built getTriggerDetails event");
    return ESP_RMAKER_OK;
}

void esp_rmaker_cloud_event_response_getTriggerDetails(esp_rmaker_cloud_events_tracker_t *p_events_tracker, const char *trigger_details, int version)
{
    const esp_rmaker_topic_ctx_t *ctx = esp_rmaker_cloud_events_tracker_ctx(p_events_tracker);
    char __tname_td[RMAKER_THING_NAME_BUFFER_SIZE];
    esp_rmaker_topic_ctx_resolve_thing_name(ctx, __tname_td, sizeof(__tname_td));
    OSAL_LOGI(TAG, "Got new trigger details (thing=%s)", __tname_td);
    OSAL_LOGD(TAG, "New trigger details: %s", trigger_details);
    /* Resolve the incoming ctx to the owning node once, here at the cloud
     * boundary; the automation service is node-keyed from this point on. */
    const esp_rmaker_node_t *node = NULL;
    if (ctx == &esp_rmaker_topic_ctx_self) {
        node = esp_rmaker_get_node();
    } else {
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_ctx(ctx);
        node = child ? bridge_internal_child_node(child) : NULL;
#endif
    }
    if (node) {
        esp_rmaker_automation_service_update_trigger_details(node, trigger_details);
    } else {
        OSAL_LOGW(TAG, "Trigger details for thing '%s' - no resolvable node, dropping", __tname_td);
    }
    __on_trigger_details_updated(ctx, p_events_tracker, version);
    p_events_tracker->events_processed |= (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getTriggerDetails);
}

esp_rmaker_error_t esp_rmaker_cloud_event_setNodeConfig(esp_rmaker_cloud_event_t *p_event, char *node_config_str, esp_rmaker_cloud_event_set_response_cb_context_t *p_set_response_cb_context)
{
    p_event->name = "setNodeConfig";
    p_event->data = node_config_str;
    p_event->p_set_response_cb_context = p_set_response_cb_context;
    return ESP_RMAKER_OK;
}

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

esp_rmaker_error_t esp_rmaker_cloud_event_addChild(esp_rmaker_cloud_event_t *p_event, char *request_payload_json)
{
    p_event->name = "addChild";
    p_event->data = request_payload_json;
    p_event->p_set_response_cb_context = NULL;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_cloud_event_removeChild(esp_rmaker_cloud_event_t *p_event, char *request_payload_json)
{
    p_event->name = "removeChild";
    p_event->data = request_payload_json;
    p_event->p_set_response_cb_context = NULL;
    return ESP_RMAKER_OK;
}

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
