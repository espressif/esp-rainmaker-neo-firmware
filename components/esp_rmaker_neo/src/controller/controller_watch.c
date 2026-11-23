/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_watch.c
 * @brief Group-wide observation of the shadow get and update planes.
 *
 * Each distinct shadow name costs two filters, and one ``+`` in the thing position covers every
 * node sharing that name, so a whole group costs two subscriptions rather than two per node.
 *
 * A delta is applied on the MQTT task, which is parsing only. The application callback is delivered
 * from the controller task, so it may call any controller API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osal_err.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"

#include "osal_mqtt_subscription_manager.h"

#include "controller_internal.h"
#include "network/mqtt_channels.h"
#include "sdkconfig.h"

/** Params one growth step adds to a notification job. */
#define WATCH_NOTIFY_GROW 8

static const char *TAG = "rmng_controller";

static esp_rmaker_controller_param_cb_t s_param_cb;
static void *s_param_cb_priv;

static esp_rmaker_controller_node_cb_t s_node_cb;
static void *s_node_cb_priv;

/** One message's changed params, handed to the controller task as a single heap block. */
typedef struct {
    size_t num;
    size_t cap;
    esp_rmaker_controller_param_t params[];
} watch_notify_job_t;

/** Collects a delta's changed params into a job that grows to fit them. */
typedef struct {
    watch_notify_job_t *job;
    bool stopped; /**< A growth failed, so the rest of the delta goes unnotified. */
} watch_notify_ctx_t;

/** One node owed a config re-fetch, or one whose online flag moved. */
typedef struct {
    esp_rmaker_controller_node_t node;
} watch_node_job_t;

/* Jobs ************************************************************************/

/* Each job resolves its handles when it runs, so an object freed after the enqueue is skipped. */
static bool __param_live(esp_rmaker_controller_param_t param)
{
    if (controller_model_lock() != ESP_RMAKER_OK) {
        return false;
    }
    bool live = controller_param_from(param) != NULL;
    controller_model_unlock();
    return live;
}

void controller_watch_deliver_params(const esp_rmaker_controller_param_t *params, size_t num)
{
    for (size_t i = 0; i < num && s_param_cb; i++) {
        if (__param_live(params[i])) {
            s_param_cb(params[i], s_param_cb_priv);
        }
    }
}

static esp_rmaker_error_t __notify_job(void *arg)
{
    watch_notify_job_t *job = (watch_notify_job_t *)arg;
    controller_watch_deliver_params(job->params, job->num);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __refetch_job(void *arg)
{
    watch_node_job_t *job = (watch_node_job_t *)arg;
    ctrl_node_t *node = controller_model_node_on_task(job->node);
    /* A failure leaves config_dirty set, so the next ncfg_ver change retries it. */
    return node ? controller_node_refetch(node) : ESP_RMAKER_OK;
}

static esp_rmaker_error_t __online_job(void *arg)
{
    watch_node_job_t *job = (watch_node_job_t *)arg;
    if (s_node_cb && controller_model_node_on_task(job->node)) {
        s_node_cb(job->node, s_node_cb_priv);
    }
    return ESP_RMAKER_OK;
}

/* Inbound messages ************************************************************/

static bool __notify_append(watch_notify_ctx_t *collector, ctrl_param_t *param)
{
    watch_notify_job_t *job = collector->job;
    if (!job || job->num == job->cap) {
        size_t cap = (job ? job->cap : 0) + WATCH_NOTIFY_GROW;
        watch_notify_job_t *grown = OSAL_REALLOC_EXTRAM(job, sizeof(*grown) + cap * sizeof(*grown->params));
        if (!grown) {
            return false;
        }
        if (!job) {
            grown->num = 0;
        }
        grown->cap = cap;
        collector->job = job = grown;
    }
    job->params[job->num++] = CONTROLLER_HANDLE(param);
    return true;
}

static void __on_param_changed(void *ctx, ctrl_param_t *param)
{
    watch_notify_ctx_t *collector = (watch_notify_ctx_t *)ctx;
    if (collector->stopped || __notify_append(collector, param)) {
        return;
    }
    collector->stopped = true;
    OSAL_LOGW(TAG, "Out of memory for a param notification; dropping the rest of this delta");
}

/* Resolve the addressed node from the topic's thing name, under the model lock. */
static ctrl_node_t *__node_from_topic(const char *topic, size_t topic_len)
{
    const char *thing = NULL;
    size_t thing_len = 0;
    if (!esp_rmaker_mqtt_topic_parse_thing_name(topic, topic_len, &thing, &thing_len)) {
        return NULL;
    }
    return controller_model_find_node_n(thing, thing_len);
}

static void __on_get_accepted(const char *topic, size_t topic_len, void *payload, size_t payload_len,
                              void *priv_data)
{
    (void)priv_data;
    if (controller_model_lock() != ESP_RMAKER_OK) {
        return;
    }
    esp_rmaker_controller_node_t node = CONTROLLER_HANDLE(__node_from_topic(topic, topic_len));
    controller_model_unlock();
    controller_state_deliver_get_reply(node, payload, payload_len);
}

static void __on_update_accepted(const char *topic, size_t topic_len, void *payload,
                                 size_t payload_len, void *priv_data)
{
    (void)priv_data;
    watch_notify_ctx_t collector = {0};
    controller_state_delta_t delta = { .on_param = __on_param_changed, .ctx = &collector };
    ctrl_node_t *node = NULL;

    if (controller_model_lock() != ESP_RMAKER_OK) {
        return;
    }
    node = __node_from_topic(topic, topic_len);
    if (node) {
        controller_parse_reported_state(node, (const char *)payload, payload_len, &delta);
    }
    esp_rmaker_controller_node_t handle = CONTROLLER_HANDLE(node);
    bool want_notify = collector.job && collector.job->num > 0 && s_param_cb;
    bool want_refetch = node && delta.ncfg_ver_moved && node->config_held;
    bool want_online = node && delta.online_moved && s_node_cb;
    controller_model_unlock();

    if (want_notify) {
        /* Dropping this costs nothing: the model is already current, only the notification is
         * lost. */
        controller_task_submit_async(__notify_job, collector.job);
    } else {
        free(collector.job);
    }

    if (want_refetch) {
        watch_node_job_t *refetch = OSAL_CALLOC_EXTRAM(1, sizeof(*refetch));
        if (refetch) {
            refetch->node = handle;
            controller_task_submit_async(__refetch_job, refetch);
        }
    }

    if (want_online) {
        watch_node_job_t *online = OSAL_CALLOC_EXTRAM(1, sizeof(*online));
        if (online) {
            online->node = handle;
            controller_task_submit_async(__online_job, online);
        }
    }
}

/* Subscriptions ***************************************************************/

/** The two planes one shadow name is observed on. */
static const struct {
    int (*filter)(const char *shadow_name, char *buffer, size_t buffer_size);
    osal_mqtt_subscribe_cb_t cb;
} s_planes[] = {
    { esp_rmaker_mqtt_topic_params_named_shadow_get_accepted_filter, __on_get_accepted },
    { esp_rmaker_mqtt_topic_params_named_shadow_update_accepted_filter, __on_update_accepted },
};

#define WATCH_NUM_PLANES (sizeof(s_planes) / sizeof(s_planes[0]))

esp_rmaker_error_t controller_watch_subscribe_shadow(const char *shadow_name)
{
    if (!shadow_name || shadow_name[0] == '\0') {
        return ESP_RMAKER_INVALID_ARG;
    }

    for (size_t i = 0; i < WATCH_NUM_PLANES; i++) {
        char filter[CONTROLLER_TOPIC_MAX_LEN];
        int len = s_planes[i].filter(shadow_name, filter, sizeof(filter));
        if (len < 0) {
            return ESP_RMAKER_INVALID_ARG;
        }
        /* Skip if already subscribed. */
        if (osal_mqtt_subscription_exists(filter, (uint16_t)len, s_planes[i].cb, NULL)) {
            continue;
        }
        esp_rmaker_error_t err = controller_mqtt_subscribe(MQTT_CHANNEL_SUB_CONTROLLER_SHADOW_SUBSCRIBE,
                                 filter, s_planes[i].cb);
        if (err != ESP_RMAKER_OK) {
            return err;
        }
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t controller_watch_unsubscribe_shadow(const char *shadow_name)
{
    if (!shadow_name || shadow_name[0] == '\0') {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t result = ESP_RMAKER_OK;
    for (size_t i = 0; i < WATCH_NUM_PLANES; i++) {
        char filter[CONTROLLER_TOPIC_MAX_LEN];
        int len = s_planes[i].filter(shadow_name, filter, sizeof(filter));
        if (len < 0) {
            return ESP_RMAKER_INVALID_ARG;
        }
        if (!osal_mqtt_subscription_exists(filter, (uint16_t)len, s_planes[i].cb, NULL)) {
            continue;
        }
        esp_rmaker_error_t err = controller_mqtt_unsubscribe(MQTT_CHANNEL_SUB_CONTROLLER_SHADOW_UNSUBSCRIBE,
                                 filter);
        if (err != ESP_RMAKER_OK) {
            /* Drop it anyway, or the reconnect replays a filter the new policy rejects. */
            osal_mqtt_subscription_remove(filter, (uint16_t)len);
            result = err;
        }
    }
    return result;
}

void controller_watch_deinit(void)
{
    s_param_cb = NULL;
    s_param_cb_priv = NULL;
    s_node_cb = NULL;
    s_node_cb_priv = NULL;
}

/* Public API ******************************************************************/

esp_rmaker_error_t esp_rmaker_controller_watch_params(esp_rmaker_controller_param_cb_t cb, void *priv)
{
    s_param_cb = cb;
    s_param_cb_priv = priv;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_controller_unwatch_params(void)
{
    return esp_rmaker_controller_watch_params(NULL, NULL);
}

esp_rmaker_error_t esp_rmaker_controller_watch_nodes(esp_rmaker_controller_node_cb_t cb, void *priv)
{
    s_node_cb = cb;
    s_node_cb_priv = priv;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_controller_unwatch_nodes(void)
{
    return esp_rmaker_controller_watch_nodes(NULL, NULL);
}
