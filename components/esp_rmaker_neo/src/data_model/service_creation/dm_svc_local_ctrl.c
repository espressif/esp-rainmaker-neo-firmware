/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_svc_local_ctrl.c
 * @brief Local control service creation implementation
 */

/* Includes *******************************************************/

/* Declaration includes. */
#include "services/standard_creation.h"

/* Constants includes */
#include "esp_rmaker_standard_types.h"
#include "constants/services.h"

/* RMNG includes */
#include "esp_rmaker_node.h"
#include "esp_rmaker_data_model.h"

/* Platform common includes */
#include "osal_log.h"

/* Constants *******************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "rmng_dm_svc_lctrl";

/* Private variables *******************************************************/

/**
 * @brief The local control service.
 */
static esp_rmaker_device_t *__local_ctrl_service = NULL;

/* Private function declarations *******************************************************/

/**
 * @brief Create the local control service.
 * @param[in] version The version of the local control service.
 * @param[in] pop The PoP for security versions 1 and 2.
 * @param[in] username The SRP6a username for security version 2.
 * @return The service, or NULL if failed.
 */
static esp_rmaker_device_t *__local_ctrl_service_create(esp_rmaker_local_ctrl_sec_t version, const char *pop, const char *username);

/* Private function definitions *******************************************************/

static esp_rmaker_device_t *__local_ctrl_service_create(esp_rmaker_local_ctrl_sec_t version, const char *pop, const char *username)
{
    if (!pop && version == ESP_RMAKER_LOCAL_CTRL_SEC2) {
        OSAL_LOGE(TAG, "PoP is required for security version 2");
        return NULL;
    }
    if (!username && version == ESP_RMAKER_LOCAL_CTRL_SEC2) {
        OSAL_LOGE(TAG, "Username is required for security version 2");
        return NULL;
    }

    /* Create the service */
    esp_rmaker_device_t *service;
    service = esp_rmaker_service_create(RMAKER_SERVICES_LOCAL_CTRL_SERVICE_ID, ESP_RMAKER_SERVICE_LOCAL_CONTROL, NULL);
    if (!service) {
        OSAL_LOGE(TAG, "Failed to create system service");
        return NULL;
    }

    /* Add the type parameter */
    int type = 0;
    switch (version) {
    case ESP_RMAKER_LOCAL_CTRL_SEC1:
        type = 1;
        break;
    case ESP_RMAKER_LOCAL_CTRL_SEC2:
        type = 2;
        break;
    default:
        OSAL_LOGE(TAG, "Unsupported security version %d", version);
        goto __local_ctrl_service_create_fail;
    }
    esp_rmaker_param_t *type_param = esp_rmaker_param_create(RMAKER_SERVICES_LOCAL_CTRL_TYPE_PARAM_ID, ESP_RMAKER_PARAM_LOCAL_CONTROL_TYPE, esp_rmaker_int(type), PROP_FLAG_READ);
    if (!type_param) {
        OSAL_LOGE(TAG, "Failed to create type parameter");
        goto __local_ctrl_service_create_fail;
    }
    if (esp_rmaker_device_add_param(service, type_param) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add type parameter to service");
        goto __local_ctrl_service_create_fail;
    }

    /* Add PoP parameter if security version is 1 or 2.
     *
     * Deliberately keyed on the security version, not on whether a PoP is actually in
     * use: the phone apps expect the field to be present for a version that has one,
     * and take "is a PoP required" from the "no_pop" capability on the version
     * endpoint. So a security-1-without-PoP build reports an empty string here, which
     * is harmless - do not "fix" this into a POP_IN_USE guard. */
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_1 || CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2
    /* Add the PoP parameter */
    esp_rmaker_param_t *pop_param = esp_rmaker_param_create(RMAKER_SERVICES_LOCAL_CTRL_POP_PARAM_ID, ESP_RMAKER_PARAM_LOCAL_CONTROL_POP, esp_rmaker_str(pop), PROP_FLAG_READ);
    if (!pop_param) {
        OSAL_LOGE(TAG, "Failed to create PoP parameter");
        goto __local_ctrl_service_create_fail;
    }
    if (esp_rmaker_device_add_param(service, pop_param) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add PoP parameter to service");
        goto __local_ctrl_service_create_fail;
    }
#endif

    /* Add Username parameter if security version is 2 */
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2
    esp_rmaker_param_t *username_param = esp_rmaker_param_create(RMAKER_SERVICES_LOCAL_CTRL_USERNAME_PARAM_ID, ESP_RMAKER_PARAM_LOCAL_CONTROL_USERNAME, esp_rmaker_str(username), PROP_FLAG_READ);
    if (!username_param) {
        OSAL_LOGE(TAG, "Failed to create Username parameter");
        goto __local_ctrl_service_create_fail;
    }
    if (esp_rmaker_device_add_param(service, username_param) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add Username parameter to service");
        goto __local_ctrl_service_create_fail;
    }
#endif
    return service;

__local_ctrl_service_create_fail:
    if (service) {
        esp_rmaker_device_delete(service);
    }
    return NULL;
}

/* Public function definitions *******************************************************/

esp_rmaker_error_t esp_rmaker_local_ctrl_service_add_to_node(esp_rmaker_local_ctrl_sec_t version, const char *pop, const char *username)
{
    __local_ctrl_service = __local_ctrl_service_create(version, pop, username);
    if (!__local_ctrl_service) {
        OSAL_LOGE(TAG, "Failed to create local control service");
        return ESP_RMAKER_INVALID_STATE;
    }
    esp_rmaker_error_t err = esp_rmaker_node_add_device(esp_rmaker_get_node(), __local_ctrl_service);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add local control service to node");
        esp_rmaker_device_delete(__local_ctrl_service);
        return ESP_RMAKER_INVALID_STATE;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_local_ctrl_service_remove_from_node(void)
{
    if (!__local_ctrl_service) {
        OSAL_LOGE(TAG, "Local control service not found");
        return ESP_RMAKER_INVALID_STATE;
    }
    esp_rmaker_error_t err = esp_rmaker_node_remove_device(esp_rmaker_get_node(), __local_ctrl_service);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to remove local control service from node");
        return ESP_RMAKER_INVALID_STATE;
    }
    esp_rmaker_device_delete(__local_ctrl_service);
    __local_ctrl_service = NULL;
    return ESP_RMAKER_OK;
}
