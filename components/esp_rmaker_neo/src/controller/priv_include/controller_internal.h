/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_internal.h
 * @brief Shared state, the controller task, and the pure helpers the unit tests exercise.
 */

#ifndef __CONTROLLER_INTERNAL_H__
#define __CONTROLLER_INTERNAL_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "osal_mqtt_prototypes.h"

#include "network/mqtt_topics.h"

#include "esp_rmaker_aws_sign.h"
#include "esp_rmaker_controller.h"
#include "esp_rmaker_error_types.h"

#include "controller_model.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bounds for the resolved state. */
#define CONTROLLER_NODE_ID_MAX_LEN    64
#define CONTROLLER_HOST_MAX_LEN       160
#define CONTROLLER_BASE_PATH_MAX_LEN  64
#define CONTROLLER_GROUP_ID_MAX_LEN   64
#define CONTROLLER_TOPIC_MAX_LEN      320
#define CONTROLLER_REST_PATH_MAX_LEN  192

/** How long a public caller waits for a slot in a full job queue. */
#define CONTROLLER_JOB_SUBMIT_TIMEOUT_MS 5000

/** Controller state, owned by controller_main.c. */
typedef struct {
    bool     enabled;
    bool     active;                                  /**< Local standby switch; true after enable. */
    char    *thing_name;                              /**< Node id, cached to avoid a leak per use. */
    char     group_id[CONTROLLER_GROUP_ID_MAX_LEN];   /**< Group of the last sync; empty before it. */
    char     group_name[CONTROLLER_GROUP_NAME_MAX_LEN]; /**< Reported by the last inventory sync. */
    bool     synced;                                  /**< An inventory sync has succeeded at least once. */
    char     synced_group_id[CONTROLLER_GROUP_ID_MAX_LEN]; /**< Group the held inventory was built from. */
} esp_rmaker_controller_state_t;

extern esp_rmaker_controller_state_t g_controller_state;

/* --- controller_task.c -------------------------------------------------------------------- */

/**
 * @brief One unit of controller work, run on the controller task.
 *
 * @param[in] arg Job argument.
 *
 * @return The status the submitter receives.
 */
typedef esp_rmaker_error_t (*controller_job_fn_t)(void *arg);

/** @brief Create the job queue and start the controller task. */
esp_rmaker_error_t controller_task_start(void);

/** @brief Run queued jobs, then park the controller task. */
void controller_task_stop(void);

/** @brief Whether the caller runs on the controller task. */
bool controller_task_is_current(void);

/**
 * @brief Run @p fn on the controller task and wait for its status.
 *
 * Executes @p fn inline when the caller already is the controller task, so a controller API called
 * from inside a callback cannot deadlock.
 *
 * @param[in] fn  Work to run.
 * @param[in] arg Argument passed to @p fn. Owned by the caller.
 *
 * @return The status @p fn returned.
 * @return ESP_RMAKER_TIMEOUT if the job queue stayed full.
 * @return ESP_RMAKER_NOT_INITIALIZED if the controller is not enabled.
 * @return ESP_RMAKER_INVALID_STATE if a disable discarded the job before it ran.
 */
esp_rmaker_error_t controller_task_run_sync(controller_job_fn_t fn, void *arg);

/**
 * @brief Queue @p fn on the controller task without waiting.
 *
 * Never blocks: a full queue drops the job with a log. Use this from the MQTT task.
 *
 * @note @p arg must be a single heap block, or NULL. The controller task frees it after @p fn
 *       returns, and also frees it when a full queue drops the job.
 *
 * @param[in] fn  Work to run.
 * @param[in] arg Argument passed to @p fn.
 *
 * @return ESP_RMAKER_OK when the job was queued.
 */
esp_rmaker_error_t controller_task_submit_async(controller_job_fn_t fn, void *arg);

/* --- controller_changes.c ---------------------------------------------------------------- */

/** @brief One change list while it is collected. */
typedef struct {
    esp_rmaker_controller_change_t *items;
    size_t num;
    size_t cap;
    bool lost;                          /**< An append ran out of memory. */
} ctrl_changes_t;

/**
 * @brief Start a change list. Call under the model lock.
 *
 * @return @p changes, or NULL when no callback is registered. Every other call accepts NULL.
 */
ctrl_changes_t *controller_changes_begin(ctrl_changes_t *changes);

/** @brief Add one entry to @p changes. Call under the model lock. */
void controller_changes_add(ctrl_changes_t *changes, esp_rmaker_controller_change_type_t type,
                            esp_rmaker_controller_kind_t kind, esp_rmaker_handle_t handle,
                            esp_rmaker_handle_t parent);

/**
 * @brief Order @p changes and queue it for delivery. Call under the model lock.
 *
 * An empty list is not queued.
 *
 * @return true when a list was queued.
 */
bool controller_changes_commit(ctrl_changes_t *changes);

/** @brief Queue a job, so that the controller task delivers the queued lists. */
void controller_changes_kick(void);

/**
 * @brief Deliver each queued list to the callback, without the model lock.
 *
 * Call on the controller task. The job runner calls this after each job.
 */
void controller_changes_drain(void);

/* --- controller_parse.c, controller_rest.c ------------------------------------------------------------------- */

/**
 * @brief Split "https://host/base/path" into the host and the base path.
 *
 * The base path keeps its leading '/' and loses any trailing '/', so a caller can append
 * "/v1/..." to it directly. It is empty when the URL carries no path. A host longer than
 * @p host_len is truncated.
 *
 * @param[in]  url           URL to split. A missing "://" is tolerated.
 * @param[out] host          Receives the host.
 * @param[in]  host_len      Capacity of @p host.
 * @param[out] base_path     Receives the base path.
 * @param[in]  base_path_len Capacity of @p base_path.
 */
void controller_parse_base_url(const char *url, char *host, size_t host_len,
                               char *base_path, size_t base_path_len);

/**
 * @brief Report whether a 403 body says that the signing credentials expired.
 *
 * API Gateway answers {"message":"The security token included in the request is expired"}; the
 * cure is fresh credentials, not a replay with the same ones.
 */
bool controller_is_expired_credentials(const char *body, size_t body_len);

/**
 * @brief Issue one signed GET. Resolves the endpoints and creates the signer on first use.
 *
 * Fresh credentials are fetched once and the request replayed when the API reports expired ones.
 *
 * @param[out] resp Response. The caller frees it with esp_rmaker_aws_sign_response_free().
 *
 * @return ESP_RMAKER_OK when a response arrived, whatever its status.
 */
esp_rmaker_error_t controller_rest_get(const char *path_suffix, esp_rmaker_aws_sign_response_t *resp);

#if CONFIG_ESP_RMAKER_NEO_TESTING
/** @brief Answers a controller REST GET in a unit test. The body must come from malloc(). */
typedef esp_rmaker_error_t (*controller_test_rest_get_t)(const char *path_suffix,
        esp_rmaker_aws_sign_response_t *resp);

/** @brief Send each controller REST GET to @p fn and not to the signer. NULL restores the signer. */
void controller_test_set_rest_get(controller_test_rest_get_t fn);
#endif

/**
 * @brief Map a REST status to an error, logging any status other than 2xx.
 *
 * @return ESP_RMAKER_OK on 2xx, ESP_RMAKER_NOT_FOUND on 404, ESP_RMAKER_INVALID_STATE on 401 or 403,
 *         ESP_RMAKER_FAIL otherwise.
 */
esp_rmaker_error_t controller_rest_status_to_error(const char *path, int status);

/** @brief Create the lock the REST calls share. */
esp_rmaker_error_t controller_rest_init(void);

/** @brief Release the signer and the cached credentials. */
void controller_rest_deinit(void);

/**
 * @brief Get the controller's group id, logging when the node is in no group.
 *
 * @param[out] group_id Caller frees.
 *
 * @return ESP_RMAKER_OK, or ESP_RMAKER_NOT_FOUND when the node is in no group.
 */
esp_rmaker_error_t controller_group_id(char **group_id);

/* --- controller_parse.c ------------------------------------------------------------------- */

/** @brief The callbacks controller_parse_group_info() reports a GroupInfo body through. */
typedef struct {
    /** The group's own ``group_name``, reported first and only when the body carries one. */
    esp_rmaker_error_t (*group)(void *ctx, const char *group_name);
    /** One entry of the group's top-level ``node_ids``. */
    esp_rmaker_error_t (*node)(void *ctx, const char *node_id);
    /** One entry of ``subgroups``, before its members are reported. */
    esp_rmaker_error_t (*subgroup)(void *ctx, const char *subgroup_id, const char *subgroup_name);
    /** One entry of a subgroup's own ``node_ids``. */
    esp_rmaker_error_t (*member)(void *ctx, const char *subgroup_id, const char *node_id);
} controller_group_info_cbs_t;

/**
 * @brief Parse a GroupInfo body, reporting its name, then its nodes, then its subgroups.
 *
 * Nodes come before subgroups, so the member callback can stamp membership onto nodes that
 * already exist.
 *
 * @param[in] body     Response body.
 * @param[in] body_len Length of @p body.
 * @param[in] cbs      Callbacks. Any member may be NULL.
 * @param[in] ctx      Opaque pointer passed to every callback.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if the body does not parse,
 *         ESP_RMAKER_NOT_FOUND if it carries no node_ids array, or the error a callback stopped
 *         with.
 */
esp_rmaker_error_t controller_parse_group_info(const char *body, size_t body_len,
        const controller_group_info_cbs_t *cbs, void *ctx);

/**
 * @brief Read the ``data_model`` field of a node config body.
 *
 * @param[out] out     Receives the value, or "" when the field is absent.
 * @param[in]  out_len Capacity of @p out.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if the body does not parse.
 */
esp_rmaker_error_t controller_parse_data_model(const char *body, size_t body_len,
        char *out, size_t out_len);

/**
 * @brief Parse a "default" node config body into @p node's device and param tree.
 *
 * Walks ``devices[]`` and ``services[]`` into one list, flagging the latter. Reads each id from
 * ``id``, and from the legacy ``name`` only when ``id`` is missing. A device or param whose id or
 * type does not fit its buffer is skipped, and so is a second one with the same id.
 *
 * Call under the model lock. Devices and params are matched to existing ones by id, so a survivor
 * keeps its handle. Each one the new config omits is freed, also on an error. The parse queues a
 * change list of what it added, updated and freed.
 *
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t controller_parse_node_config(ctrl_node_t *node, const char *body, size_t body_len);

/**
 * @brief Reports one param whose value a reported-state payload changed.
 *
 * @note Called under the model lock, so it must not call a controller API. Stopping early is not
 *       possible: the payload is applied in full whatever the callback does.
 */
typedef void (*controller_param_changed_cb_t)(void *ctx, ctrl_param_t *param);

/** @brief What one ``state.reported`` payload moved, and how to report its changed params. */
typedef struct {
    /** Called once per changed param, in tree order. May be NULL. */
    controller_param_changed_cb_t on_param;
    /** Opaque pointer passed to @p on_param. */
    void *ctx;
    /** How many params changed, whether or not @p on_param is set. */
    size_t num_changed;
    /** Set when the reported ncfg_ver differs from the recorded one. A move also marks the node's
     *  config stale. */
    bool ncfg_ver_moved;
    /** Set when the reported online flag differs from the recorded one. ``online`` is a shadow
     *  field rather than a param, so this is the only notification of it. */
    bool online_moved;
} controller_state_delta_t;

/**
 * @brief Apply a shadow ``state.reported`` object onto @p node.
 *
 * Reads ``params``, ``online`` and ``ncfg_ver``. A param the object does not mention keeps its
 * cached value, which is what makes a delta merge safe.
 *
 * Call under the model lock.
 *
 * @param[in]     node     Node the payload addresses.
 * @param[in]     body     Shadow payload.
 * @param[in]     body_len Length of @p body.
 * @param[in,out] delta    Carries the callback in and the outcome out. May be NULL. Its output
 *                         fields are cleared on entry.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if the body does not parse,
 *         ESP_RMAKER_NOT_FOUND if it carries no ``state.reported``.
 */
esp_rmaker_error_t controller_parse_reported_state(ctrl_node_t *node, const char *body, size_t body_len,
        controller_state_delta_t *delta);

/**
 * @brief Map a config ``data_type`` string to a value type.
 *
 * The inverse of the table esp_rmaker_report_data_type() emits.
 *
 * @return The value type, or RMAKER_VAL_TYPE_INVALID for an unknown string.
 */
esp_rmaker_val_type_t controller_parse_data_type(const char *data_type);

/**
 * @brief Map a config ``ui_type`` string to a UI hint.
 *
 * @return The hint, RMAKER_UI_TYPE_NONE for NULL, or RMAKER_UI_TYPE_UNKNOWN for an unmapped string.
 */
esp_rmaker_ui_type_t controller_parse_ui_type(const char *ui_type);

/**
 * @brief Map a config ``properties[]`` entry to a property flag.
 *
 * @return The flag, or 0 for an unknown string.
 */
uint8_t controller_parse_property_flag(const char *property);

/* --- controller_model.c ------------------------------------------------------------------- */

/**
 * @brief Point a topic ctx at @p node, so the SDK topic builders address it.
 *
 * @note @p ctx must not outlive the call it is built for. The header's static-storage rule covers
 *       ctxs held across publish cycles by the state and timeseries pipelines; the controller only
 *       formats a topic and publishes it within one call.
 *
 * @param[in]  node Node to address.
 * @param[out] ctx  Receives the ctx.
 */
void controller_node_topic_ctx(ctrl_node_t *node, esp_rmaker_topic_ctx_t *ctx);

/* --- controller_group.c ------------------------------------------------------------------- */

/** @brief Run one inventory sync. Called on the controller task. */
esp_rmaker_error_t controller_group_sync(void *unused);

/** @brief Re-sync after the cloud reported a group migration. Called on the controller task. */
esp_rmaker_error_t controller_group_resync_migrated(void *unused);

/**
 * @brief Re-sync only when the held inventory no longer matches the node's group.
 *
 * The retry for a migration re-sync that failed. Costs nothing when nothing is owed.
 */
esp_rmaker_error_t controller_group_resync_check(void *unused);

/** @brief Unsubscribe every shadow filter that the model holds. The model is not changed. */
void controller_group_unwatch_all(void);

/* --- controller_config.c ------------------------------------------------------------------ */

/** @brief Fetch and parse one node's config. Called on the controller task. */
esp_rmaker_error_t controller_config_fetch(ctrl_node_t *node);

/** @brief Fetch one node's config, then seed its values from the shadow. Called on the controller task. */
esp_rmaker_error_t controller_node_refetch(ctrl_node_t *node);

/* --- controller_state.c ------------------------------------------------------------------- */

/** @brief Blocking named-shadow get for one node. Called on the controller task. */
esp_rmaker_error_t controller_state_refresh(ctrl_node_t *node);

/**
 * @brief Deliver a shadow get/accepted payload to a waiting refresh.
 *
 * Called on the MQTT task by the watch layer, which owns the subscription.
 */
void controller_state_deliver_get_reply(esp_rmaker_handle_t node, const void *payload, size_t payload_len);

/** @brief Forget the get in flight. The reply buffer and its semaphore stay for the process. */
void controller_state_deinit(void);

/* --- controller_watch.c ------------------------------------------------------------------- */

/**
 * @brief Subscribe @p shadow_name's get and update planes.
 *
 * @note A plane the subscription manager already holds is skipped, so a repeat call for the same
 * name costs nothing and a name lost to a clean session is subscribed again.
 *
 * Call on the controller task.
 */
esp_rmaker_error_t controller_watch_subscribe_shadow(const char *shadow_name);

/**
 * @brief Unsubscribe @p shadow_name's get and update planes.
 *
 * @note Drops the filter from the subscription table even when the unsubscribe cannot be sent, so
 *       a reconnect does not replay a filter the refreshed policy no longer authorizes.
 *
 * Call on the controller task.
 */
esp_rmaker_error_t controller_watch_unsubscribe_shadow(const char *shadow_name);

/**
 * @brief Call the param callback for each handle in @p params that still resolves.
 *
 * Called on the controller task when a queued notification runs.
 */
void controller_watch_deliver_params(const esp_rmaker_controller_param_t *params, size_t num);

/** @brief Forget the registered param and node callbacks. */
void controller_watch_deinit(void);

/* --- controller_write.c ------------------------------------------------------------------- */

/**
 * @brief Render a single-param node write payload: ``{"<device id>":{"<param id>":v}}``.
 *
 * Call under the model lock.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if the param is not writable or a string
 *         is not UTF-8, ESP_RMAKER_NO_MEM if the payload does not fit @p out_len.
 */
esp_rmaker_error_t controller_render_node_write(ctrl_param_t *param, const esp_rmaker_param_val_t *val,
        char *out, size_t out_len);

/**
 * @brief Render @p node's staged batch, one object per device.
 *
 * Call under the model lock. The batch is left staged; the caller clears it.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_NOT_FOUND if a staged param is stale,
 *         ESP_RMAKER_INVALID_ARG if a string is not UTF-8, ESP_RMAKER_NO_MEM if it does not fit.
 */
esp_rmaker_error_t controller_render_node_batch(ctrl_node_t *node, char *out, size_t out_len);

/**
 * @brief Render a group write payload: ``{"<device type>":{"params":{"<param type>":v}}}``.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if a string is not UTF-8,
 *         ESP_RMAKER_NO_MEM if the payload does not fit @p out_len.
 */
esp_rmaker_error_t controller_render_group_write(const char *device_type, const char *param_type,
        const esp_rmaker_param_val_t *val, char *out, size_t out_len);

/**
 * @brief Render a staged group batch, one object per device type.
 *
 * ``{"<type a>":{"params":{"<p1>":v,"<p2>":v}},"<type b>":{"params":{"<p3>":v}}}``. A receiving
 * node applies the types it owns a device of and ignores the rest, so one payload drives every
 * type at once.
 *
 * Call under the model lock. The batch is left staged; the caller clears it.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_NO_MEM if the batch does not fit @p out_len,
 *         ESP_RMAKER_INVALID_ARG if a string is not UTF-8.
 */
esp_rmaker_error_t controller_render_group_batch(const ctrl_staged_group_write_t *batch,
        char *out, size_t out_len);

/**
 * @brief The staged group batch, or NULL when none is open or it holds nothing.
 *
 * The batch is controller-wide state with no handle to reach it by, so this is how the renderer's
 * tests get at what they staged.
 */
const ctrl_staged_group_write_t *controller_group_batch_peek(void);

/** @brief Publish to a controller topic on the controller MQTT channel. */
esp_rmaker_error_t controller_mqtt_publish(uint8_t sub, const char *topic, const void *data, size_t data_len);

/** @brief Subscribe to a controller topic filter on the controller MQTT channel. */
esp_rmaker_error_t controller_mqtt_subscribe(uint8_t sub, const char *topic, osal_mqtt_subscribe_cb_t cb);

/** @brief Unsubscribe from a controller topic filter on the controller MQTT channel. */
esp_rmaker_error_t controller_mqtt_unsubscribe(uint8_t sub, const char *topic);

#ifdef __cplusplus
}
#endif

#endif /* __CONTROLLER_INTERNAL_H__ */
