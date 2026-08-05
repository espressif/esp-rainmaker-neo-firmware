/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file host_ctrl_core.c
 * @brief Host control core implementation.
 */

/* Includes ******************************************************************/

/* Declarations */
#include "esp_rmaker_host_ctrl.h"

/* Standard includes */
#include <stdlib.h>
#include <string.h>

/* Constants */
#include "esp_rmaker_host_ctrl_constants.h"
#include "sdkconfig.h"

/* Platform common includes */
#include "osal_task.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* External I/O common includes */
#include "osal_ext_io.h"

/* RMNG includes */
#include "esp_rmaker_flow.h"
#include "node_internal.h"

/* Event flags includes */
#include "event_flags.h"

/* System internal includes */
#include "system_ctrl_internal.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "esp_rmaker_bridge.h"
#endif

/* Preprocessor definitions ***************************************************/

#define RMAKER_HOST_CTRL_TASK_STACK_DEPTH CONFIG_RMNG_HOST_CTRL_TASK_STACK_DEPTH
#define RMAKER_HOST_CTRL_TASK_PRIORITY CONFIG_RMNG_HOST_CTRL_TASK_PRIORITY

/* Private variables *********************************************************/

/**
 * @brief Tag for the host control.
 */
static const char *TAG = "rmng_hc_core";

static const char *__node_name = "host_ctrl";
static const char *__node_type = "esp.rmaker.host_ctrl";

/**
 * @brief Host control task handle.
 */
static osal_task_handle_t __esp_rmaker_host_ctrl_task_handle = NULL;

/**
 * @brief The node for the host control.
 */
static esp_rmaker_node_t *__esp_rmaker_host_ctrl_node;

/**
 * @brief The config for the host control.
 */
static esp_rmaker_config_t *__esp_rmaker_host_ctrl_config = NULL;

/* Private function declarations *********************************************/

/**
 * @brief Host control task.
 * @param[in] unused Unused parameter.
 */
static void __esp_rmaker_host_ctrl_task(void *unused);

/**
 * @brief Handle the host control buffer.
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
extern void __esp_rmaker_host_ctrl_handle_buffer(uint8_t *buffer, size_t buffer_length);


/* Private function definitions **********************************************/

static void __esp_rmaker_host_ctrl_task(void *unused)
{
    uint8_t buffer[1024];

    while (1) {
        size_t read_length = 0;
        if (!osal_ext_io_read_until_sync(buffer, sizeof(buffer), RMAKER_HOST_CTRL_END_CHAR, &read_length)) {
            osal_task_delay(osal_ticks_from_ms(500));
            continue;
        }

        if (read_length > 0) {
            __esp_rmaker_host_ctrl_handle_buffer(buffer, read_length);
        }
    }
}

esp_rmaker_error_t __esp_rmaker_host_ctrl_reset(void)
{
    /* Common with the public data reset: clear the RMNG data namespaces (checksum, local config). */
    esp_rmaker_error_t err = esp_rmaker_system_ctrl_clear_data_namespaces();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to clear the NVS for reset");
        return err;
    }

    esp_rmaker_event_flags_clear_all();

    if (__esp_rmaker_host_ctrl_node != NULL) {
        err = esp_rmaker_node_deinit(__esp_rmaker_host_ctrl_node);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to deinitialize the RMNG node");
            return err;
        }
    }


    if (__esp_rmaker_host_ctrl_config == NULL) {
        OSAL_LOGE(TAG, "RMNG config not initialized. Cannot reset node.");
        return ESP_RMAKER_INVALID_STATE;
    }
    __esp_rmaker_host_ctrl_node = esp_rmaker_node_init(__esp_rmaker_host_ctrl_config, __node_name, __node_type);
    if (__esp_rmaker_host_ctrl_node == NULL) {
        OSAL_LOGE(TAG, "Failed to re-initialize the RMNG node");
        return ESP_RMAKER_FAIL;
    }

    OSAL_LOGI(TAG, "RMNG node reset");
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t __esp_rmaker_host_ctrl_reset_keep_nvs(void)
{
    /* Full deinit + reinit of the node WITHOUT clearing NVS. Simulates a
     * cold reboot (everything in RAM rebuilt; persisted state intact).
     */
    esp_rmaker_event_flags_clear_all();

    if (__esp_rmaker_host_ctrl_node != NULL) {
        esp_rmaker_error_t err = esp_rmaker_node_deinit(__esp_rmaker_host_ctrl_node);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to deinitialize the RMNG node");
            return err;
        }
    }

    if (__esp_rmaker_host_ctrl_config == NULL) {
        OSAL_LOGE(TAG, "RMNG config not initialized. Cannot reset node.");
        return ESP_RMAKER_INVALID_STATE;
    }
    __esp_rmaker_host_ctrl_node = esp_rmaker_node_init(__esp_rmaker_host_ctrl_config, __node_name, __node_type);
    if (__esp_rmaker_host_ctrl_node == NULL) {
        OSAL_LOGE(TAG, "Failed to re-initialize the RMNG node");
        return ESP_RMAKER_FAIL;
    }

    OSAL_LOGI(TAG, "RMNG node reset (NVS preserved)");
    return ESP_RMAKER_OK;
}

/* Public functions **********************************************************/

esp_rmaker_error_t esp_rmaker_host_ctrl_init(esp_rmaker_config_t *config)
{
    /* Initialize event flags */
    if (esp_rmaker_event_flags_init() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize the event flags");
        return ESP_RMAKER_FAIL;
    }

    /* Initialize the host control node */
    if (config == NULL) {
        OSAL_LOGE(TAG, "Configuration cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    __esp_rmaker_host_ctrl_config = OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_config_t));
    if (__esp_rmaker_host_ctrl_config == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for host control configuration");
        return ESP_RMAKER_FAIL;
    }
    memcpy(__esp_rmaker_host_ctrl_config, config, sizeof(esp_rmaker_config_t));
    __esp_rmaker_host_ctrl_node = esp_rmaker_node_init(__esp_rmaker_host_ctrl_config, __node_name, __node_type);
    if (__esp_rmaker_host_ctrl_node == NULL) {
        OSAL_LOGE(TAG, "Failed to initialize the host control node");
        return ESP_RMAKER_FAIL;
    }

    /* Initialize the external I/O */
    if (!osal_ext_io_init()) {
        OSAL_LOGE(TAG, "Failed to initialize the external I/O");
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_host_ctrl_deinit(void)
{
    /* Deinitialize the external I/O */
    if (!osal_ext_io_deinit()) {
        OSAL_LOGE(TAG, "Failed to deinitialize the external I/O");
        return ESP_RMAKER_FAIL;
    }

    /* Deinitialize the host control node */
    if (esp_rmaker_node_deinit(__esp_rmaker_host_ctrl_node) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to deinitialize the host control node");
        return ESP_RMAKER_FAIL;
    }
    if (__esp_rmaker_host_ctrl_config != NULL) {
        free(__esp_rmaker_host_ctrl_config);
        __esp_rmaker_host_ctrl_config = NULL;
    }
    __esp_rmaker_host_ctrl_node = NULL;

    /* Deinitialize the event flags */
    if (esp_rmaker_event_flags_deinit() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to deinitialize the event flags");
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_host_ctrl_start(void)
{
    if (osal_task_create(__esp_rmaker_host_ctrl_task, "esp_rmaker_host_ctrl", RMAKER_HOST_CTRL_TASK_STACK_DEPTH, NULL, RMAKER_HOST_CTRL_TASK_PRIORITY, &__esp_rmaker_host_ctrl_task_handle) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to create the host control task");
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_host_ctrl_stop(void)
{
    if (__esp_rmaker_host_ctrl_task_handle == NULL) {
        OSAL_LOGE(TAG, "Host control task not running. Cannot stop it.");
        return ESP_RMAKER_INVALID_STATE;
    }

    osal_task_delete(__esp_rmaker_host_ctrl_task_handle);
    __esp_rmaker_host_ctrl_task_handle = NULL;

    return ESP_RMAKER_OK;
}
