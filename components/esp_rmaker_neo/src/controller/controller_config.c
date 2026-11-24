/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_config.c
 * @brief Fetching and parsing one node's data model.
 *
 * A re-fetch keeps each device and param the new config still holds, with its handle, and frees the
 * others. Public calls pass the node handle to the job, which resolves it on the controller task.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "osal_err.h"
#include "osal_log.h"

#include "controller_internal.h"
#include "sdkconfig.h"

static const char *TAG = "rmng_controller";

esp_rmaker_error_t controller_config_fetch(ctrl_node_t *node)
{
    esp_rmaker_controller_state_t *st = &g_controller_state;
    if (!node || !st->enabled) {
        return ESP_RMAKER_NOT_INITIALIZED;
    }
    char path[CONTROLLER_REST_PATH_MAX_LEN];
    if (snprintf(path, sizeof(path), "/v1/groups/%s/nodes/%s/config",
                 st->group_id, node->node_id) >= (int)sizeof(path)) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_aws_sign_response_t resp = {0};
    esp_rmaker_error_t err = controller_rest_get(path, &resp);
    if (err == ESP_RMAKER_OK) {
        err = controller_rest_status_to_error(path, resp.status_code);
    }
    if (err != ESP_RMAKER_OK) {
        esp_rmaker_aws_sign_response_free(&resp);
        return err;
    }

    /* The data model is read first, and the check is an allow-list, so a model added later is
     * rejected rather than half-parsed. */
    char data_model[CONTROLLER_DATA_MODEL_MAX_LEN] = {0};
    err = controller_parse_data_model((const char *)resp.body, resp.body_len,
                                      data_model, sizeof(data_model));
    if (err != ESP_RMAKER_OK) {
        esp_rmaker_aws_sign_response_free(&resp);
        return err;
    }

    err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        esp_rmaker_aws_sign_response_free(&resp);
        return err;
    }
    snprintf(node->data_model, sizeof(node->data_model), "%s", data_model);
    node->data_model_supported = strcmp(data_model, CONTROLLER_DATA_MODEL_DEFAULT) == 0;
    if (!node->data_model_supported) {
        OSAL_LOGW(TAG, "Node '%s' reports data model '%s'; not parsing it",
                  node->node_id, data_model);
        controller_model_unlock();
        esp_rmaker_aws_sign_response_free(&resp);
        return ESP_RMAKER_NOT_SUPPORTED;
    }
    /* Under the lock throughout, so an application iterating on another task sees either the old
     * or the new list, never a half-linked one. */
    err = controller_parse_node_config(node, (const char *)resp.body, resp.body_len);
    controller_model_unlock();
    esp_rmaker_aws_sign_response_free(&resp);
    return err;
}

/* The job argument carries the node handle, not a pointer. */
static ctrl_node_t *__job_node(void *arg)
{
    return controller_model_node_on_task((esp_rmaker_handle_t)(uintptr_t)arg);
}

static void *__job_arg(esp_rmaker_controller_node_t node)
{
    return (void *)(uintptr_t)node;
}

static esp_rmaker_error_t __fetch_job(void *arg)
{
    ctrl_node_t *node = __job_node(arg);
    return node ? controller_config_fetch(node) : ESP_RMAKER_NOT_FOUND;
}

esp_rmaker_error_t esp_rmaker_controller_node_fetch_config(esp_rmaker_controller_node_t node)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return controller_task_run_sync(__fetch_job, __job_arg(node));
}

esp_rmaker_error_t controller_node_refetch(ctrl_node_t *node)
{
    esp_rmaker_error_t err = controller_config_fetch(node);
    /* A delta never carries full state, so seed the fresh tree from the shadow. */
    return err == ESP_RMAKER_OK ? controller_state_refresh(node) : err;
}

static esp_rmaker_error_t __node_sync_job(void *arg)
{
    ctrl_node_t *node = __job_node(arg);
    if (!node) {
        return ESP_RMAKER_NOT_FOUND;
    }
    esp_rmaker_error_t err;
    if (node->config_held) {
        err = controller_state_refresh(node);
        if (err != ESP_RMAKER_OK) {
            return err;
        }
        /* The get reports the ncfg_ver, so a data model that did not move needs no config fetch. */
        if (!node->config_dirty) {
            return ESP_RMAKER_OK;
        }
    }
    return controller_node_refetch(node);
}

esp_rmaker_error_t esp_rmaker_controller_node_sync(esp_rmaker_controller_node_t node)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return controller_task_run_sync(__node_sync_job, __job_arg(node));
}

void esp_rmaker_controller_node_release_config(esp_rmaker_controller_node_t node)
{
    if (!node || controller_model_lock() != ESP_RMAKER_OK) {
        return;
    }
    ctrl_node_t *n = controller_node_from(node);
    ctrl_changes_t changes;
    ctrl_changes_t *collect = controller_changes_begin(&changes);
    for (ctrl_device_t *dev = n ? n->devices : NULL; dev; dev = dev->next) {
        controller_changes_add(collect, ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_DEVICE,
                               dev->obj.handle, node);
    }
    if (n) {
        controller_model_release_config(n);
    }
    bool queued = controller_changes_commit(collect);
    controller_model_unlock();
    if (queued) {
        controller_changes_kick();
    }
}
