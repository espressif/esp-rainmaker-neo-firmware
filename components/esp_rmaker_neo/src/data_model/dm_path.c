/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_path.c
 * @brief Path translation for the data model.
 *
 * Path format: "<device_id>.<param_id>"
 */

/* Includes *******************************************************/

/* RMNG includes */
#include "data_model_internal.h"
#include "esp_rmaker_node.h"
#include "constants/path.h"

/* Standard includes */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Platform includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Global variables *******************************************************/

static const char *TAG = "rmng_dm_path";

/* Function definitions *******************************************************/

char *data_model_update_id_to_path(const esp_rmaker_state_update_id_t update_id)
{
    const _esp_rmaker_state_update_id_t *u = (const _esp_rmaker_state_update_id_t *)update_id;
    if (!u) {
        OSAL_LOGE(TAG, "Invalid update ID");
        return NULL;
    }
    if (!u->device || !u->device->id || !u->param || !u->param->id) {
        OSAL_LOGE(TAG, "Invalid device or param");
        return NULL;
    }

    size_t dev_len = strlen(u->device->id);
    size_t param_len = strlen(u->param->id);
    size_t path_len = dev_len + 1 /* separator */ + param_len + 1 /* NUL */;
    char *path = OSAL_MALLOC_EXTRAM(path_len);
    if (!path) {
        OSAL_LOGE(TAG, "Failed to allocate path buffer");
        return NULL;
    }
    int written = snprintf(path, path_len, "%s%s%s", u->device->id, RMAKER_PATH_SEPARATOR_STR, u->param->id);
    if (written < 0 || (size_t)written >= path_len) {
        OSAL_LOGE(TAG, "Failed to format path");
        free(path);
        return NULL;
    }
    return path;
}

esp_rmaker_state_update_id_t data_model_path_to_update_id_for_node(const esp_rmaker_node_t *node, const char *path)
{
    if (!node) {
        OSAL_LOGE(TAG, "Node cannot be NULL");
        return NULL;
    }
    if (!path) {
        OSAL_LOGE(TAG, "Path cannot be NULL");
        return NULL;
    }
    const char *sep = strchr(path, RMAKER_PATH_SEPARATOR_CHAR);
    if (!sep || sep == path || sep[1] == '\0') {
        OSAL_LOGE(TAG, "Path '%s' is malformed; expected '<device_id>%s<param_id>'", path, RMAKER_PATH_SEPARATOR_STR);
        return NULL;
    }
    size_t dev_id_len = (size_t)(sep - path);
    char *device_id = OSAL_MALLOC_EXTRAM(dev_id_len + 1);
    if (!device_id) {
        OSAL_LOGE(TAG, "Failed to allocate device id buffer");
        return NULL;
    }
    memcpy(device_id, path, dev_id_len);
    device_id[dev_id_len] = '\0';
    const char *param_id = sep + 1;

    esp_rmaker_device_t *device = esp_rmaker_node_get_device_by_id(node, device_id);
    if (!device) {
        OSAL_LOGE(TAG, "Device '%s' not found", device_id);
        free(device_id);
        return NULL;
    }
    free(device_id);

    esp_rmaker_param_t *param = esp_rmaker_device_get_param_by_id(device, param_id);
    if (!param) {
        OSAL_LOGE(TAG, "Param '%s' not found", param_id);
        return NULL;
    }
    return esp_rmaker_state_update_id_create(param);
}

/* Self-only back-compat wrapper. */
esp_rmaker_state_update_id_t data_model_path_to_update_id(const char *path)
{
    return data_model_path_to_update_id_for_node(esp_rmaker_get_node(), path);
}
