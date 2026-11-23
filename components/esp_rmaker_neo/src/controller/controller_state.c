/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_state.c
 * @brief Seeding a node's param values with a blocking named-shadow get.
 *
 * A node publishes only the params that changed, so a delta never carries full state. This is what
 * seeds it; the update subscription keeps it current afterwards.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_semaphore.h"
#include "osal_task.h"
#include "osal_ticks.h"

#include "controller_internal.h"
#include "network/mqtt_channels.h"
#include "sdkconfig.h"

#define SHADOW_RESPONSE_MAX   CONFIG_ESP_RMAKER_NEO_CONTROLLER_SHADOW_RESPONSE_MAX
#define SHADOW_GET_TIMEOUT_MS CONFIG_ESP_RMAKER_NEO_CONTROLLER_SHADOW_GET_TIMEOUT_MS

static const char *TAG = "rmng_controller";

/* One get is in flight at a time: every get runs on the controller task, which is single. The
 * reply arrives on the MQTT task and is copied here. Kept for the process, so a late reply never
 * writes to freed memory. */
static osal_semaphore_handle_t s_reply_sem;
static char *s_reply;
static size_t s_reply_len;
static size_t s_reply_payload_len;
static esp_rmaker_handle_t s_want;
static esp_rmaker_handle_t s_reply_for;  /**< The node whose reply s_reply holds. */

void controller_state_deliver_get_reply(esp_rmaker_handle_t node, const void *payload, size_t payload_len)
{
    if (!s_reply_sem || !s_reply || !node || node != s_want) {
        return;
    }
    /* A truncated document does not parse, so the length is kept and reported by the caller. */
    s_reply_payload_len = payload_len;
    s_reply_len = payload_len < SHADOW_RESPONSE_MAX ? payload_len : SHADOW_RESPONSE_MAX;
    memcpy(s_reply, payload, s_reply_len);
    s_reply_for = node;
    osal_semaphore_give(s_reply_sem);
}

esp_rmaker_error_t controller_state_refresh(ctrl_node_t *node)
{
    if (!node || !g_controller_state.enabled) {
        return ESP_RMAKER_NOT_INITIALIZED;
    }
    if (node->shadow[0] == '\0') {
        return ESP_RMAKER_INVALID_STATE;
    }

    esp_rmaker_topic_ctx_t ctx;
    controller_node_topic_ctx(node, &ctx);
    char topic[CONTROLLER_TOPIC_MAX_LEN];
    if (esp_rmaker_mqtt_topic_params_named_shadow_get(&ctx, topic, sizeof(topic)) < 0) {
        return ESP_RMAKER_INVALID_ARG;
    }

    if (!s_reply) {
        s_reply = OSAL_CALLOC_EXTRAM(1, SHADOW_RESPONSE_MAX);
        if (!s_reply) {
            return ESP_RMAKER_NO_MEM;
        }
    }
    if (!s_reply_sem) {
        s_reply_sem = osal_semaphore_create_binary();
        if (!s_reply_sem) {
            return ESP_RMAKER_NO_MEM;
        }
    }
    /* Drop a reply left over from a timed-out get. */
    osal_semaphore_take(s_reply_sem, 0);
    s_reply_len = 0;
    s_reply_payload_len = 0;
    s_reply_for = 0;
    s_want = CONTROLLER_HANDLE(node);

    /* The AsyncAPI specifies an empty JSON object, not a zero-length payload. */
    esp_rmaker_error_t err = controller_mqtt_publish(MQTT_CHANNEL_SUB_CONTROLLER_SHADOW_GET,
                             topic, "{}", 2);
    if (err != ESP_RMAKER_OK) {
        s_want = 0;
        return err;
    }
    /* A reply to an earlier, timed-out get can pass its node check just before this get starts,
     * and land after it. It is tagged with that node, so skip it and keep waiting for this one. */
    const osal_tick_type_t timeout = osal_ticks_from_ms(SHADOW_GET_TIMEOUT_MS);
    const osal_tick_type_t start = osal_task_get_tick_count();
    bool got = false;
    for (;;) {
        osal_tick_type_t elapsed = osal_task_get_tick_count() - start;
        if (elapsed >= timeout || osal_semaphore_take(s_reply_sem, timeout - elapsed) != OSAL_ERR_OK) {
            break;
        }
        if (s_reply_for == s_want) {
            got = true;
            break;
        }
    }
    s_want = 0;
    if (!got) {
        OSAL_LOGE(TAG, "No shadow reply for node '%s'", node->node_id);
        return ESP_RMAKER_TIMEOUT;
    }
    if (s_reply_payload_len > SHADOW_RESPONSE_MAX) {
        OSAL_LOGE(TAG, "Node '%s' reported a %zu-byte shadow document; raise "
                  "CONFIG_ESP_RMAKER_NEO_CONTROLLER_SHADOW_RESPONSE_MAX above %d",
                  node->node_id, s_reply_payload_len, SHADOW_RESPONSE_MAX);
        return ESP_RMAKER_NO_MEM;
    }

    err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    err = controller_parse_reported_state(node, s_reply, s_reply_len, NULL);
    controller_model_unlock();
    return err;
}

static esp_rmaker_error_t __refresh_job(void *arg)
{
    ctrl_node_t *node = controller_model_node_on_task((esp_rmaker_handle_t)(uintptr_t)arg);
    return node ? controller_state_refresh(node) : ESP_RMAKER_NOT_FOUND;
}

esp_rmaker_error_t esp_rmaker_controller_node_refresh_params(esp_rmaker_controller_node_t node)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return controller_task_run_sync(__refresh_job, (void *)(uintptr_t)node);
}

void controller_state_deinit(void)
{
    s_want = 0;
    if (s_reply_sem) {
        osal_semaphore_take(s_reply_sem, 0);
    }
    s_reply_len = 0;
    s_reply_payload_len = 0;
    s_reply_for = 0;
}
