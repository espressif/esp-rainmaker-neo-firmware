/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file automation.h
 * @brief Automation service declarations.
 */

#ifndef __SERVICES_AUTOMATION_H__
#define __SERVICES_AUTOMATION_H__

/* Includes **********************************************************************/

/* Error types includes */
#include "esp_rmaker_error_types.h"

/* JSON parser includes */
#include "json_parser.h"

/* Node includes */
#include "node_internal.h"

/* Preprocessor definitions *********************************************************/

/**
 * @brief The maximum number of triggers that can be registered for a node.
 */
#define RMAKER_TRIGGER_MAX_COUNT 128

/**
 * @brief The flag to indicate that the trigger has changed.
 */
#define RMAKER_TRIGGER_FLAG_CHANGED (1 << 0)
/**
 * @brief The flag to indicate that the trigger has been met.
 */
#define RMAKER_TRIGGER_FLAG_MET (1 << 1)

/* Types **********************************************************************/

/**
 * @brief Trigger event parameter information.
 */
typedef struct __automation_trigger_t {
    char *id; /**< The ID of the trigger */
    uint8_t flags; /**< The flags of the trigger */
    esp_rmaker_state_update_id_t update_id; /**< The update ID that the trigger is registered to */
    esp_rmaker_val_compare_t compare_op; /**< The compare operation to perform on the trigger */
    esp_rmaker_param_val_t expected_val; /**< The value to compare the parameter value to */
} esp_rmaker_automation_trigger_t;

/* Public function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/* --- Initialization/deinitialization --- */

/**
 * @brief Initialize the automation service.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_NO_MEM if memory allocation fails.
 */
esp_rmaker_error_t esp_rmaker_automation_service_init(void);

/**
 * @brief Deinitialize the automation service.
 * @note If the service is not initialized, this function returns ESP_RMAKER_OK without doing anything.
 * @return ESP_RMAKER_OK on success (including if not initialized).
 * @return Error code if clearing the trigger list fails.
 */
esp_rmaker_error_t esp_rmaker_automation_service_deinit(void);

/* --- On start --- */

/**
 * @brief On start, load the automation details from NVS.
 */
esp_rmaker_error_t esp_rmaker_automation_service_on_start(void);

/* --- Trigger handling --- */

/**
 * @brief Handle trigger details for ``node``.
 *
 * Replaces only that node's triggers (other nodes untouched), persists to the
 * node's NVS store, and leaves the node's existing triggers intact on parse
 * failure. Queued to the work queue; the build runs under the node lock.
 *
 * @note Should only be called after that node's parameters exist, otherwise
 *       trigger paths will not resolve.
 * @param[in] node            Owning node (self or a bridge child).
 * @param[in] trigger_details Trigger details as a NUL-terminated JSON array string.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_automation_service_update_trigger_details(const esp_rmaker_node_t *node, const char *trigger_details);

/**
 * @brief Reload a node's triggers from its NVS store (no re-persist).
 *
 * Self reads ``trg_det`` (local_config); a child reads the per-child
 * ``bridge_triggers`` blob. Synchronous; runs under the node lock.
 * @param[in] node Owning node (self or a bridge child).
 * @return ESP_RMAKER_OK on success (including "nothing stored").
 */
esp_rmaker_error_t esp_rmaker_automation_service_reload_for_node(const esp_rmaker_node_t *node);

/**
 * @brief Free a node's embedded trigger list. Called from
 *        ::_esp_rmaker_node_reset so a node's triggers are torn down with it.
 *        Safe on a node that never had triggers.
 * @param[in] node Node handle.
 */
void esp_rmaker_automation_drop_node(const esp_rmaker_node_t *node);

/**
 * @brief Check if an update ID should trigger any triggers it is registered to. If so, fire the triggers.
 * @param[in] update_id The update ID to check.
 * @param[in] val The value of the parameter to check.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if update_id is NULL.
 * @return ESP_RMAKER_FAIL if value comparison fails or if scheduling the trigger state report fails.
 */
esp_rmaker_error_t esp_rmaker_automation_service_update_id_check_and_fire(const esp_rmaker_state_update_id_t update_id, esp_rmaker_param_val_t val);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICES_AUTOMATION_H__ */
