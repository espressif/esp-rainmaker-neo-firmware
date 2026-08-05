/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file node_config_pending.c
 * @brief Drain-all pending list for the node-config retry context.
 *
 * State lives on the node itself (``node->pending_cfg``; see
 * ::node_config_pending_state_t in node_internal.h). There is no slot
 * table - iteration is via ::esp_rmaker_node_for_each.
 *
 * Concurrency: a private mutex guards the per-node ``pending`` and
 * ``inflight`` flags so concurrent add / remove / clear / fire calls
 * stay coherent. The publish dispatch (``_for_node`` -> cloud manager)
 * is invoked outside the mutex so the cloud-manager mutex isn't taken
 * while ours is held.
 *
 * Lock order: callers of ``fire_all`` walk nodes through the bridge
 * visitor, which takes the bridge mutex; the visitor then takes our
 * mutex. No other code path takes our mutex before the bridge mutex,
 * so the ordering is one-way (bridge -> pending).
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "node_config_pending.h"
#include "node_internal.h"
#include "esp_rmaker_core.h"

#include "esp_rmaker_error_types.h"

#include "osal_log.h"
#include "osal_semaphore.h"

#include "sdkconfig.h"

static const char *TAG = "rmng_node_cfg_pend";

/* Worst-case nodes we may need to snapshot per fire_all tick: self
 * plus every possible bridge child. Used to size the heap-free snapshot
 * buffer in ::node_config_pending_fire_all. */
#define NCP_MAX_NODES (1 + CONFIG_RMNG_BRIDGE_MAX_CHILDREN)

static osal_semaphore_handle_t __mutex = NULL;
static bool __initialized = false;

static inline node_config_pending_state_t *__state_of(const esp_rmaker_node_t *node)
{
    return node ? &((_esp_rmaker_node_t *)node)->pending_cfg : NULL;
}

esp_rmaker_error_t node_config_pending_init(void)
{
    if (!__initialized) {
        __mutex = osal_semaphore_create_mutex();
        if (!__mutex) {
            OSAL_LOGE(TAG, "Failed to create pending mutex");
            return ESP_RMAKER_FAIL;
        }
        __initialized = true;
    }
    /* Seed with the self node so the very first retry tick publishes
     * it. Always re-seed on every init call. */
    (void)node_config_pending_add(esp_rmaker_get_node());
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t node_config_pending_add(const esp_rmaker_node_t *node)
{
    node_config_pending_state_t *st = __state_of(node);
    if (!__initialized || !st) {
        return ESP_RMAKER_INVALID_ARG;
    }
    osal_semaphore_take(__mutex, OSAL_MAX_DELAY);
    /* Re-add clears inflight so the next tick refires (handles the
     * "devices changed after first publish" case). */
    st->pending = true;
    st->inflight = false;
    osal_semaphore_give(__mutex);
    return ESP_RMAKER_OK;
}

void node_config_pending_remove(const esp_rmaker_node_t *node)
{
    node_config_pending_state_t *st = __state_of(node);
    if (!__initialized || !st) {
        return;
    }
    osal_semaphore_take(__mutex, OSAL_MAX_DELAY);
    st->pending = false;
    st->inflight = false;
    osal_semaphore_give(__mutex);
}

void node_config_pending_clear_inflight(const esp_rmaker_node_t *node)
{
    node_config_pending_state_t *st = __state_of(node);
    if (!__initialized || !st) {
        return;
    }
    osal_semaphore_take(__mutex, OSAL_MAX_DELAY);
    st->inflight = false;
    osal_semaphore_give(__mutex);
}

static esp_rmaker_error_t __clear_all_inflight_visitor(const esp_rmaker_node_t *node, void *priv)
{
    (void)priv;
    node_config_pending_state_t *st = __state_of(node);
    if (st) {
        st->inflight = false;
    }
    return ESP_RMAKER_OK;
}

void node_config_pending_clear_all_inflight(void)
{
    if (!__initialized) {
        return;
    }
    osal_semaphore_take(__mutex, OSAL_MAX_DELAY);
    esp_rmaker_node_for_each(__clear_all_inflight_visitor, NULL);
    osal_semaphore_give(__mutex);
}

typedef struct {
    const esp_rmaker_node_t *to_fire[NCP_MAX_NODES];
    int n_fire;
} __ncp_collect_priv_t;

static esp_rmaker_error_t __collect_pending_visitor(const esp_rmaker_node_t *node, void *priv)
{
    __ncp_collect_priv_t *p = (__ncp_collect_priv_t *)priv;
    node_config_pending_state_t *st = __state_of(node);
    if (!st) {
        return ESP_RMAKER_OK;
    }
    if (st->pending && !st->inflight && p->n_fire < NCP_MAX_NODES) {
        st->inflight = true;
        p->to_fire[p->n_fire++] = node;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t node_config_pending_fire_all(void)
{
    if (!__initialized) {
        return ESP_RMAKER_INVALID_STATE;
    }

    __ncp_collect_priv_t collect = { .n_fire = 0 };

    osal_semaphore_take(__mutex, OSAL_MAX_DELAY);
    esp_rmaker_node_for_each(__collect_pending_visitor, &collect);
    osal_semaphore_give(__mutex);

    esp_rmaker_error_t agg = ESP_RMAKER_OK;
    for (int i = 0; i < collect.n_fire; i++) {
        esp_rmaker_error_t err = esp_rmaker_internal_report_node_config_for_node(collect.to_fire[i]);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Publish failed for node %p (err %d); clearing inflight for retry", (const void *)collect.to_fire[i], err);
            node_config_pending_clear_inflight(collect.to_fire[i]);
            /* Bubble up so the retry manager re-fires the drain on its
             * backoff. Returning OK here would terminate the retry chain
             * and leave the still-pending entries stuck. */
            agg = err;
        }
        /* On success: either the publish was issued (inflight remains
         * set; ack callback will clear it / remove the entry) or
         * checksum was unchanged (the reporter already removed the
         * entry via node_config_pending_remove). */
    }
    return agg;
}
