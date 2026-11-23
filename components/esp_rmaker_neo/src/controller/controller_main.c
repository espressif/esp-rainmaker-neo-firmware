/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_main.c
 * @brief Group controller lifecycle and the local standby switch.
 */

#include <stdlib.h>
#include <string.h>

#include "osal_err.h"
#include "osal_log.h"

#include "esp_rmaker_node.h"

#include "controller_internal.h"

static const char *TAG = "rmng_controller";

esp_rmaker_controller_state_t g_controller_state = {0};

esp_rmaker_error_t esp_rmaker_controller_enable(void)
{
    esp_rmaker_controller_state_t *st = &g_controller_state;
    if (st->enabled) {
        return ESP_RMAKER_ALREADY_INITIALIZED;
    }
    memset(st, 0, sizeof(*st));

    /* esp_rmaker_get_node_id() hands back an allocation; hold one copy rather than leaking one
     * per signer creation. */
    st->thing_name = esp_rmaker_get_node_id();
    if (!st->thing_name) {
        OSAL_LOGE(TAG, "No node id - start the agent before enabling the controller");
        return ESP_RMAKER_NOT_INITIALIZED;
    }

    esp_rmaker_error_t err = controller_rest_init();
    if (err == ESP_RMAKER_OK) {
        err = controller_model_init();
    }
    if (err == ESP_RMAKER_OK) {
        /* Every blocking call runs its work here, so no caller needs a deep stack of its own. */
        err = controller_task_start();
    }
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to start the controller task: %s", osal_err_strerror(err));
        controller_model_deinit();
        controller_rest_deinit();
        free(st->thing_name);
        st->thing_name = NULL;
        return err;
    }

    st->active = true;
    st->enabled = true;
    OSAL_LOGI(TAG, "Controller enabled");
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_controller_disable(void)
{
    esp_rmaker_controller_state_t *st = &g_controller_state;
    if (!st->enabled) {
        return ESP_RMAKER_OK;
    }
    /* The task cannot wait for itself to stop. */
    if (controller_task_is_current()) {
        OSAL_LOGE(TAG, "Cannot disable the controller from a controller callback");
        return ESP_RMAKER_INVALID_STATE;
    }
    /* Queued jobs run before the task parks, so nothing is left half-done. */
    controller_task_stop();
    /* After the stop, so no queued sync subscribes again. */
    controller_group_unwatch_all();
    st->enabled = false;
    esp_rmaker_controller_group_write_abort();
    controller_state_deinit();
    controller_watch_deinit();
    controller_model_deinit();
    controller_rest_deinit();
    free(st->thing_name);
    memset(st, 0, sizeof(*st));
    return ESP_RMAKER_OK;
}

void esp_rmaker_controller_set_active(bool active)
{
    g_controller_state.active = active;
    OSAL_LOGI(TAG, "Controller %s", active ? "active" : "on standby");
}

bool esp_rmaker_controller_is_active(void)
{
    return g_controller_state.active;
}
