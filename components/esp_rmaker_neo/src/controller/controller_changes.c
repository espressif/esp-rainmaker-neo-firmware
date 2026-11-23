/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_changes.c
 * @brief The change lists: collected under the model lock, delivered on the controller task.
 *
 * Every list goes through one FIFO, so lists arrive in the order the rebuilds ran. Each list
 * carries the registration count from when it was collected, and a list with an old count is dropped.
 */

#include <stdlib.h>
#include <string.h>

#include "osal_log.h"
#include "osal_mem_alloc.h"

#include "controller_internal.h"

#define CHANGES_GROW 16

static const char *TAG = "rmng_controller";

typedef struct ctrl_change_list {
    struct ctrl_change_list *next;
    uint32_t registration;
    size_t num;
    esp_rmaker_controller_change_t items[];
} ctrl_change_list_t;

/* Guarded by the model lock, which may be absent before enable, when no other task runs. */
static esp_rmaker_controller_change_cb_t s_cb;
static void *s_cb_priv;
static uint32_t s_registration;
static ctrl_change_list_t *s_head;
static ctrl_change_list_t *s_tail;

/* Touched only on the controller task, or on the caller of disable() once the task is parked. */
static bool s_draining;

ctrl_changes_t *controller_changes_begin(ctrl_changes_t *changes)
{
    memset(changes, 0, sizeof(*changes));
    return s_cb ? changes : NULL;
}

void controller_changes_add(ctrl_changes_t *changes, esp_rmaker_controller_change_type_t type,
                            esp_rmaker_controller_kind_t kind, esp_rmaker_handle_t handle,
                            esp_rmaker_handle_t parent)
{
    if (!changes || changes->lost) {
        return;
    }
    if (changes->num == changes->cap) {
        size_t cap = changes->cap + CHANGES_GROW;
        esp_rmaker_controller_change_t *grown = OSAL_REALLOC_EXTRAM(changes->items, cap * sizeof(*grown));
        if (!grown) {
            changes->lost = true;
            return;
        }
        changes->items = grown;
        changes->cap = cap;
    }
    changes->items[changes->num++] = (esp_rmaker_controller_change_t) {
        .type = type,
        .kind = kind,
        .handle = handle,
        .parent = parent,
    };
}

/* Copy the entries of one type into @p out, in collection order. */
static size_t __copy_type(const ctrl_changes_t *changes, esp_rmaker_controller_change_type_t type,
                          esp_rmaker_controller_change_t *out)
{
    size_t num = 0;
    for (size_t i = 0; i < changes->num; i++) {
        if (changes->items[i].type == type) {
            out[num++] = changes->items[i];
        }
    }
    return num;
}

bool controller_changes_commit(ctrl_changes_t *changes)
{
    if (!changes) {
        return false;
    }
    if (changes->lost) {
        OSAL_LOGE(TAG, "Out of memory for a change list; the application view is out of date");
    }
    ctrl_change_list_t *list = changes->num
                               ? OSAL_MALLOC_EXTRAM(sizeof(*list) + changes->num * sizeof(list->items[0])) : NULL;
    if (changes->num && !list) {
        OSAL_LOGE(TAG, "Out of memory for a change list of %zu entries; dropping it", changes->num);
    }
    if (list) {
        /* REMOVED first, then UPDATED, then ADDED. ADDED keeps its order, so parents come first. */
        size_t num = __copy_type(changes, ESP_RMAKER_CONTROLLER_REMOVED, list->items);
        num += __copy_type(changes, ESP_RMAKER_CONTROLLER_UPDATED, list->items + num);
        num += __copy_type(changes, ESP_RMAKER_CONTROLLER_ADDED, list->items + num);
        list->num = num;
        list->next = NULL;
        list->registration = s_registration;
        if (s_tail) {
            s_tail->next = list;
        } else {
            s_head = list;
        }
        s_tail = list;
    }
    free(changes->items);
    memset(changes, 0, sizeof(*changes));
    return list != NULL;
}

static esp_rmaker_error_t __drain_job(void *arg)
{
    (void)arg;
    /* The job runner drains after each job, so this job only has to exist. */
    return ESP_RMAKER_OK;
}

void controller_changes_kick(void)
{
    (void)controller_task_submit_async(__drain_job, NULL);
}

void controller_changes_drain(void)
{
    if (s_draining) {
        return;
    }
    s_draining = true;
    for (;;) {
        bool locked = controller_model_lock() == ESP_RMAKER_OK;
        ctrl_change_list_t *list = s_head;
        if (list) {
            s_head = list->next;
            if (!s_head) {
                s_tail = NULL;
            }
        }
        esp_rmaker_controller_change_cb_t cb = s_cb;
        void *priv = s_cb_priv;
        bool current = list && list->registration == s_registration;
        if (locked) {
            controller_model_unlock();
        }
        if (!list) {
            break;
        }
        if (cb && current) {
            cb(list->items, list->num, priv);
        }
        free(list);
    }
    s_draining = false;
}

/* Under the model lock: ADDED for each object of the current snapshot, parents before children. */
static void __collect_snapshot(ctrl_changes_t *changes)
{
    for (ctrl_node_t *node = controller_model_node_head(); node; node = node->next) {
        controller_changes_add(changes, ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_NODE,
                               node->obj.handle, 0);
        for (ctrl_device_t *dev = node->devices; dev; dev = dev->next) {
            controller_changes_add(changes, ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_DEVICE,
                                   dev->obj.handle, node->obj.handle);
            for (ctrl_param_t *param = dev->params; param; param = param->next) {
                controller_changes_add(changes, ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_PARAM,
                                       param->obj.handle, dev->obj.handle);
            }
        }
    }
    for (ctrl_subgroup_t *sg = controller_model_subgroup_head(); sg; sg = sg->next) {
        controller_changes_add(changes, ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_SUBGROUP,
                               sg->obj.handle, 0);
    }
}

esp_rmaker_error_t esp_rmaker_controller_watch_changes(esp_rmaker_controller_change_cb_t cb, void *priv)
{
    if (!cb) {
        return ESP_RMAKER_INVALID_ARG;
    }
    bool locked = controller_model_lock() == ESP_RMAKER_OK;
    s_cb = cb;
    s_cb_priv = priv;
    s_registration++;
    ctrl_changes_t changes;
    ctrl_changes_t *collect = controller_changes_begin(&changes);
    __collect_snapshot(collect);
    bool queued = controller_changes_commit(collect);
    if (locked) {
        controller_model_unlock();
    }
    if (queued) {
        controller_changes_kick();
    }
    return ESP_RMAKER_OK;
}

void esp_rmaker_controller_unwatch_changes(void)
{
    bool locked = controller_model_lock() == ESP_RMAKER_OK;
    s_cb = NULL;
    s_cb_priv = NULL;
    s_registration++;
    while (s_head) {
        ctrl_change_list_t *next = s_head->next;
        free(s_head);
        s_head = next;
    }
    s_tail = NULL;
    if (locked) {
        controller_model_unlock();
    }
}
