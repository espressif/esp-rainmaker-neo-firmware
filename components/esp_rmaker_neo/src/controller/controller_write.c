/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_write.c
 * @brief Driving the group's params, and the MQTT plumbing every controller topic rides on.
 *
 * The SDK owns the keying difference the application would otherwise have to know: a node write is
 * keyed by device and param NAME, a group write by device and param TYPE. Both serialize values
 * through esp_rmaker_report_value(), so escaping and float formatting match the node side exactly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_generator.h"

#include "osal_err.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_mqtt_events.h"

#include "esp_rmaker_mqtt_impl.h"

#include "node_internal.h"

#include "controller_internal.h"
#include "network/mqtt_channels.h"
#include "sdkconfig.h"

/** Ceiling on a params publish. One node's whole param set is well under this. */
#define CONTROLLER_WRITE_PAYLOAD_MAX 1024

static const char *TAG = "rmng_controller";

/* MQTT plumbing ***************************************************************/

/* The agent owns the connection; every controller topic rides it on the controller channel. */

static bool __mqtt_ready(void)
{
    return osal_mqtt_event_get_bits(OSAL_MQTT_CLIENT_CONNECTED_BIT) == OSAL_MQTT_CLIENT_CONNECTED_BIT;
}

esp_rmaker_error_t controller_mqtt_publish(uint8_t sub, const char *topic, const void *data, size_t data_len)
{
    if (!esp_rmaker_mqtt_impl.publish || !__mqtt_ready()) {
        OSAL_LOGE(TAG, "Cannot publish to '%s': MQTT not connected", topic);
        return ESP_RMAKER_INVALID_STATE;
    }
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_CONTROLLER,
        .sub = sub,
    };
    osal_err_t err = esp_rmaker_mqtt_impl.publish(&channel, topic, strlen(topic),
                     (void *)data, data_len, QoS1, false);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Publish to '%s' failed: %d", topic, (int)err);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t controller_mqtt_subscribe(uint8_t sub, const char *topic, osal_mqtt_subscribe_cb_t cb)
{
    if (!esp_rmaker_mqtt_impl.subscribe || !__mqtt_ready()) {
        OSAL_LOGE(TAG, "Cannot subscribe to '%s': MQTT not connected", topic);
        return ESP_RMAKER_INVALID_STATE;
    }
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_CONTROLLER,
        .sub = sub,
    };
    osal_err_t err = esp_rmaker_mqtt_impl.subscribe(&channel, topic, strlen(topic), cb, QoS1, NULL);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Subscribe to '%s' failed: %d", topic, (int)err);
        if (err == OSAL_ERR_INVALID_STATE) {
            /* The agent's own filters share the table, and each shadow name costs two. */
            OSAL_LOGE(TAG, "The MQTT subscription table is full; raise "
                      "CONFIG_OSAL_MQTT_SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS (now %d)",
                      CONFIG_OSAL_MQTT_SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS);
        }
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t controller_mqtt_unsubscribe(uint8_t sub, const char *topic)
{
    if (!esp_rmaker_mqtt_impl.unsubscribe || !__mqtt_ready()) {
        OSAL_LOGW(TAG, "Cannot unsubscribe from '%s': MQTT not connected", topic);
        return ESP_RMAKER_INVALID_STATE;
    }
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_CONTROLLER,
        .sub = sub,
    };
    osal_err_t err = esp_rmaker_mqtt_impl.unsubscribe(&channel, topic, strlen(topic), QoS1);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Unsubscribe from '%s' failed: %d", topic, (int)err);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

/* Publish gate ****************************************************************/

static esp_rmaker_error_t __publish_params(uint8_t sub, const char *topic, const char *payload)
{
    if (!esp_rmaker_controller_is_active()) {
        OSAL_LOGW(TAG, "Controller is on standby; refusing to publish");
        return ESP_RMAKER_INVALID_STATE;
    }
    return controller_mqtt_publish(sub, topic, payload, strlen(payload));
}

/* Node writes *****************************************************************/

/**
 * One node write. The job resolves the handles and renders on the controller task, where a
 * re-fetch cannot run between the check and the publish.
 */
typedef struct {
    esp_rmaker_handle_t target;             /**< The param of a single write, or the node of a batch. */
    const esp_rmaker_param_val_t *val;      /**< The value of a single write; NULL for a batch. */
    char topic[CONTROLLER_TOPIC_MAX_LEN];
    char payload[CONTROLLER_WRITE_PAYLOAD_MAX];
} node_write_t;

static esp_rmaker_error_t __node_topic(ctrl_node_t *node, char *topic, size_t topic_len)
{
    esp_rmaker_topic_ctx_t ctx;
    controller_node_topic_ctx(node, &ctx);
    return esp_rmaker_mqtt_topic_params_to_node_for_node(&ctx, topic, topic_len) < 0
           ? ESP_RMAKER_INVALID_ARG : ESP_RMAKER_OK;
}

/* Under the model lock: render one param write. */
static esp_rmaker_error_t __render_single(node_write_t *write)
{
    void *obj = NULL;
    esp_rmaker_error_t err = controller_handle_lookup(write->target, CTRL_KIND_PARAM, &obj);
    ctrl_param_t *param = (ctrl_param_t *)obj;
    if (err == ESP_RMAKER_OK) {
        err = controller_render_node_write(param, write->val, write->payload, sizeof(write->payload));
    }
    if (err == ESP_RMAKER_OK) {
        err = __node_topic(param->parent->parent, write->topic, sizeof(write->topic));
    }
    return err;
}

/* Under the model lock: render and close a node's batch. A stale staged param drops the batch. */
static esp_rmaker_error_t __render_batch(node_write_t *write)
{
    void *obj = NULL;
    esp_rmaker_error_t err = controller_handle_lookup(write->target, CTRL_KIND_NODE, &obj);
    ctrl_node_t *node = (ctrl_node_t *)obj;
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!node->batch_open) {
        return ESP_RMAKER_INVALID_STATE;
    }
    err = controller_render_node_batch(node, write->payload, sizeof(write->payload));
    if (err == ESP_RMAKER_NOT_FOUND) {
        OSAL_LOGW(TAG, "Node '%s' changed since the batch began; batch dropped", node->node_id);
    }
    controller_model_batch_clear(node);
    if (err == ESP_RMAKER_OK) {
        err = __node_topic(node, write->topic, sizeof(write->topic));
    }
    return err;
}

static esp_rmaker_error_t __node_write_job(void *arg)
{
    node_write_t *write = (node_write_t *)arg;
    esp_rmaker_error_t err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    err = write->val ? __render_single(write) : __render_batch(write);
    controller_model_unlock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    /* Published outside the lock, which the MQTT task needs to apply a delta. */
    return __publish_params(MQTT_CHANNEL_SUB_CONTROLLER_PARAMS, write->topic, write->payload);
}

static esp_rmaker_error_t __run_node_write(esp_rmaker_handle_t target, const esp_rmaker_param_val_t *val)
{
    node_write_t *write = OSAL_CALLOC_EXTRAM(1, sizeof(*write));
    if (!write) {
        return ESP_RMAKER_NO_MEM;
    }
    write->target = target;
    write->val = val;
    esp_rmaker_error_t err = controller_task_run_sync(__node_write_job, write);
    free(write);
    return err;
}

static esp_rmaker_error_t __check_writable(ctrl_param_t *param)
{
    /* The node does not enforce this, so refusing here turns a silently-ignored publish into an
     * immediate error. */
    if (!(param->prop_flags & PROP_FLAG_WRITE)) {
        OSAL_LOGE(TAG, "Param '%s' is not writable", param->id ? param->id : "");
        return ESP_RMAKER_INVALID_ARG;
    }
    return ESP_RMAKER_OK;
}

/* Close @p jstr, and turn a generator error into an SDK error. */
static esp_rmaker_error_t __render_end(json_gen_str_t *jstr, const char *what)
{
    int len = json_gen_str_end(jstr);
    if (len >= 0) {
        return ESP_RMAKER_OK;
    }
    OSAL_LOGE(TAG, "Could not render the %s payload: %d", what, len);
    if (len == JSON_GEN_ERR_BUF_FULL) {
        return ESP_RMAKER_NO_MEM;
    }
    return len == JSON_GEN_ERR_INVALID_UTF8 ? ESP_RMAKER_INVALID_ARG : ESP_RMAKER_FAIL;
}

/* The node keys an inbound params payload by device and param ID. */
esp_rmaker_error_t controller_render_node_write(ctrl_param_t *param, const esp_rmaker_param_val_t *val,
        char *out, size_t out_len)
{
    if (!param || !val || !out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = __check_writable(param);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, out, (int)out_len, NULL, NULL);
    json_gen_start_object(&jstr);
    json_gen_push_object(&jstr, param->parent->id);
    esp_rmaker_report_value(val, param->id, &jstr);
    json_gen_pop_object(&jstr);
    json_gen_end_object(&jstr);
    return __render_end(&jstr, "node write");
}

esp_rmaker_error_t controller_render_node_batch(ctrl_node_t *node, char *out, size_t out_len)
{
    if (!node || !out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    for (ctrl_staged_write_t *staged = node->batch; staged; staged = staged->next) {
        if (!controller_param_from(staged->param)) {
            return ESP_RMAKER_NOT_FOUND;
        }
    }
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, out, (int)out_len, NULL, NULL);
    json_gen_start_object(&jstr);
    /* One object per device, so params of the same device land together. */
    for (ctrl_device_t *dev = node->devices; dev; dev = dev->next) {
        bool opened = false;
        for (ctrl_staged_write_t *staged = node->batch; staged; staged = staged->next) {
            ctrl_param_t *param = controller_param_from(staged->param);
            if (param->parent != dev) {
                continue;
            }
            if (!opened) {
                json_gen_push_object(&jstr, dev->id);
                opened = true;
            }
            esp_rmaker_report_value(&staged->val, param->id, &jstr);
        }
        if (opened) {
            json_gen_pop_object(&jstr);
        }
    }
    json_gen_end_object(&jstr);
    return __render_end(&jstr, "node batch");
}

/* The group control plane is keyed by TYPE, and wraps the params in a "params" envelope. */
esp_rmaker_error_t controller_render_group_write(const char *device_type, const char *param_type,
        const esp_rmaker_param_val_t *val, char *out, size_t out_len)
{
    if (!device_type || device_type[0] == '\0' || !param_type || param_type[0] == '\0' || !val || !out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, out, (int)out_len, NULL, NULL);
    json_gen_start_object(&jstr);
    json_gen_push_object(&jstr, device_type);
    json_gen_push_object(&jstr, "params");
    esp_rmaker_report_value(val, (char *)param_type, &jstr);
    json_gen_pop_object(&jstr);
    json_gen_pop_object(&jstr);
    json_gen_end_object(&jstr);
    return __render_end(&jstr, "group write");
}

/* Whether an earlier staged write already opened @p type's object. */
static bool __group_type_opened(const ctrl_staged_group_write_t *batch,
                                const ctrl_staged_group_write_t *type)
{
    for (const ctrl_staged_group_write_t *w = batch; w != type; w = w->next) {
        if (strcmp(w->device_type, type->device_type) == 0) {
            return true;
        }
    }
    return false;
}

esp_rmaker_error_t controller_render_group_batch(const ctrl_staged_group_write_t *batch,
        char *out, size_t out_len)
{
    if (!batch || !out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, out, (int)out_len, NULL, NULL);
    json_gen_start_object(&jstr);
    /* One object per device type, so params of the same type share one "params" envelope. */
    for (const ctrl_staged_group_write_t *type = batch; type; type = type->next) {
        if (__group_type_opened(batch, type)) {
            continue;
        }
        json_gen_push_object(&jstr, type->device_type);
        json_gen_push_object(&jstr, "params");
        for (const ctrl_staged_group_write_t *w = type; w; w = w->next) {
            if (strcmp(w->device_type, type->device_type) == 0) {
                esp_rmaker_report_value(&w->val, w->param_type, &jstr);
            }
        }
        json_gen_pop_object(&jstr);
        json_gen_pop_object(&jstr);
    }
    json_gen_end_object(&jstr);
    return __render_end(&jstr, "group batch");
}

esp_rmaker_error_t esp_rmaker_controller_param_write(esp_rmaker_controller_param_t param,
        esp_rmaker_param_val_t val)
{
    if (!param) {
        return ESP_RMAKER_INVALID_ARG;
    }
    /* The caller blocks until the job returns, so the job may read @p val in place. */
    return __run_node_write(param, &val);
}

/* Batch writes ****************************************************************/

esp_rmaker_error_t esp_rmaker_controller_node_write_begin(esp_rmaker_controller_node_t node)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    void *obj = NULL;
    err = controller_handle_lookup(node, CTRL_KIND_NODE, &obj);
    ctrl_node_t *n = (ctrl_node_t *)obj;
    if (err == ESP_RMAKER_OK && n->batch_open) {
        err = ESP_RMAKER_INVALID_STATE;
    } else if (err == ESP_RMAKER_OK) {
        n->batch_open = true;
    }
    controller_model_unlock();
    return err;
}

esp_rmaker_error_t esp_rmaker_controller_node_write_add(esp_rmaker_controller_node_t node,
        esp_rmaker_controller_param_t param, esp_rmaker_param_val_t val)
{
    if (!node || !param) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    void *obj = NULL;
    err = controller_handle_lookup(node, CTRL_KIND_NODE, &obj);
    ctrl_node_t *n = (ctrl_node_t *)obj;
    if (err == ESP_RMAKER_OK) {
        err = controller_handle_lookup(param, CTRL_KIND_PARAM, &obj);
    }
    ctrl_param_t *p = (ctrl_param_t *)obj;
    if (err != ESP_RMAKER_OK) {
        goto out;
    }
    if (!n->batch_open) {
        err = ESP_RMAKER_INVALID_STATE;
        goto out;
    }
    if (p->parent->parent != n) {
        err = ESP_RMAKER_INVALID_ARG;
        goto out;
    }
    err = __check_writable(p);
    if (err != ESP_RMAKER_OK) {
        goto out;
    }

    ctrl_staged_write_t *staged = OSAL_CALLOC_EXTRAM(1, sizeof(*staged));
    if (!staged) {
        err = ESP_RMAKER_NO_MEM;
        goto out;
    }
    err = esp_rmaker_val_copy(&val, &staged->val);
    if (err != ESP_RMAKER_OK) {
        free(staged);
        goto out;
    }
    staged->param = param;
    /* Append, so the publish follows the order the application staged. */
    ctrl_staged_write_t **tail = &n->batch;
    while (*tail) {
        tail = &(*tail)->next;
    }
    *tail = staged;

out:
    controller_model_unlock();
    return err;
}

esp_rmaker_error_t esp_rmaker_controller_node_write_commit(esp_rmaker_controller_node_t node)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __run_node_write(node, NULL);
}

void esp_rmaker_controller_node_write_abort(esp_rmaker_controller_node_t node)
{
    if (!node || controller_model_lock() != ESP_RMAKER_OK) {
        return;
    }
    ctrl_node_t *n = controller_node_from(node);
    if (n) {
        controller_model_batch_clear(n);
    }
    controller_model_unlock();
}

/* Group writes ****************************************************************/

/** One group or subgroup write, staged so the whole operation runs on the controller task. */
typedef struct {
    char subgroup_id[RMAKER_SUBGROUP_BUFFER_SIZE];
    char payload[CONTROLLER_WRITE_PAYLOAD_MAX];
} group_write_t;

static esp_rmaker_error_t __group_write_job(void *arg)
{
    group_write_t *write = (group_write_t *)arg;
    char *group_id = NULL;
    esp_rmaker_error_t err = controller_group_id(&group_id);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    char topic[CONTROLLER_TOPIC_MAX_LEN];
    int written = write->subgroup_id[0] != '\0'
                  ? esp_rmaker_mqtt_topic_group_control_subgroup(topic, sizeof(topic), group_id,
                          write->subgroup_id)
                  : esp_rmaker_mqtt_topic_group_control_broadcast(topic, sizeof(topic), group_id);
    free(group_id);
    if (written < 0) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __publish_params(MQTT_CHANNEL_SUB_CONTROLLER_PARAMS, topic, write->payload);
}

/* The open group batch. A group has no handle of its own, so unlike a node batch this is
 * controller-wide state; the model lock guards it alongside the model. */
static bool s_batch_open;
static char s_batch_subgroup[RMAKER_SUBGROUP_BUFFER_SIZE];
static ctrl_staged_group_write_t *s_batch;

/* Call under the model lock. */
static void __group_batch_clear(void)
{
    ctrl_staged_group_write_t *staged = s_batch;
    while (staged) {
        ctrl_staged_group_write_t *next = staged->next;
        esp_rmaker_val_free(&staged->val);
        free(staged->device_type);
        free(staged->param_type);
        free(staged);
        staged = next;
    }
    s_batch = NULL;
    s_batch_open = false;
    s_batch_subgroup[0] = '\0';
}

esp_rmaker_error_t esp_rmaker_controller_group_write(const char *subgroup_id, const char *device_type,
        const char *param_type, esp_rmaker_param_val_t val)
{
    if (!device_type || device_type[0] == '\0' || !param_type || param_type[0] == '\0') {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (subgroup_id && strlen(subgroup_id) >= RMAKER_SUBGROUP_BUFFER_SIZE) {
        return ESP_RMAKER_INVALID_ARG;
    }
    group_write_t *write = OSAL_CALLOC_EXTRAM(1, sizeof(*write));
    if (!write) {
        return ESP_RMAKER_NO_MEM;
    }
    if (subgroup_id) {
        snprintf(write->subgroup_id, sizeof(write->subgroup_id), "%s", subgroup_id);
    }

    esp_rmaker_error_t err = controller_render_group_write(device_type, param_type, &val,
                             write->payload, sizeof(write->payload));
    if (err == ESP_RMAKER_OK) {
        err = controller_task_run_sync(__group_write_job, write);
    }
    free(write);
    return err;
}

const ctrl_staged_group_write_t *controller_group_batch_peek(void)
{
    return s_batch;
}

esp_rmaker_error_t esp_rmaker_controller_group_write_begin(const char *subgroup_id)
{
    if (subgroup_id && strlen(subgroup_id) >= RMAKER_SUBGROUP_BUFFER_SIZE) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (s_batch_open) {
        err = ESP_RMAKER_INVALID_STATE;
    } else {
        s_batch_open = true;
        snprintf(s_batch_subgroup, sizeof(s_batch_subgroup), "%s", subgroup_id ? subgroup_id : "");
    }
    controller_model_unlock();
    return err;
}

esp_rmaker_error_t esp_rmaker_controller_group_write_add(const char *device_type,
        const char *param_type, esp_rmaker_param_val_t val)
{
    if (!device_type || device_type[0] == '\0' || !param_type || param_type[0] == '\0') {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!s_batch_open) {
        controller_model_unlock();
        return ESP_RMAKER_INVALID_STATE;
    }

    ctrl_staged_group_write_t *staged = OSAL_CALLOC_EXTRAM(1, sizeof(*staged));
    if (!staged) {
        controller_model_unlock();
        return ESP_RMAKER_NO_MEM;
    }
    staged->device_type = OSAL_STRDUP_EXTRAM(device_type);
    staged->param_type = OSAL_STRDUP_EXTRAM(param_type);
    if (!staged->device_type || !staged->param_type) {
        err = ESP_RMAKER_NO_MEM;
    } else {
        err = esp_rmaker_val_copy(&val, &staged->val);
    }
    if (err != ESP_RMAKER_OK) {
        free(staged->device_type);
        free(staged->param_type);
        free(staged);
        controller_model_unlock();
        return err;
    }
    /* Append, so the publish follows the order the application staged. */
    ctrl_staged_group_write_t **tail = &s_batch;
    while (*tail) {
        tail = &(*tail)->next;
    }
    *tail = staged;

    controller_model_unlock();
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_controller_group_write_commit(void)
{
    group_write_t *write = OSAL_CALLOC_EXTRAM(1, sizeof(*write));
    if (!write) {
        return ESP_RMAKER_NO_MEM;
    }
    esp_rmaker_error_t err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        free(write);
        return err;
    }
    if (!s_batch_open) {
        controller_model_unlock();
        free(write);
        return ESP_RMAKER_INVALID_STATE;
    }
    snprintf(write->subgroup_id, sizeof(write->subgroup_id), "%s", s_batch_subgroup);
    /* An empty batch closes with no publish: there is nothing to say. */
    err = s_batch ? controller_render_group_batch(s_batch, write->payload, sizeof(write->payload))
          : ESP_RMAKER_INVALID_STATE;
    __group_batch_clear();
    controller_model_unlock();

    if (err == ESP_RMAKER_OK) {
        err = controller_task_run_sync(__group_write_job, write);
    }
    free(write);
    return err;
}

void esp_rmaker_controller_group_write_abort(void)
{
    if (controller_model_lock() != ESP_RMAKER_OK) {
        return;
    }
    __group_batch_clear();
    controller_model_unlock();
}
