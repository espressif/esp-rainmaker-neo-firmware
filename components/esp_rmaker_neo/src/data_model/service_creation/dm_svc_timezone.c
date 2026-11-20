/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_svc_timezone.c
 * @brief Timezone service creation implementation
 */

/* Includes *******************************************************/

/* Declarations */
#include "esp_rmaker_standard_services.h"
#include "services/standard_creation.h"

/* Standard includes */
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

/* Constants */
#include "esp_rmaker_standard_types.h"
#include "constants/services.h"

/* Platform common includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Timesync includes */
#include "osal_time.h"
#include "osal_timesync.h"

/* RainMaker Neo includes */
#include "node_internal.h"
#include "esp_rmaker_standard_params.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Constants *******************************************************/

/**
 * @brief Tag for the timezone service
 */
static const char *TAG = "rmng_dm_svc_timezone";

/* Private variables *******************************************************/

/**
 * @brief The timezone service
 */
static esp_rmaker_device_t *__timezone_service = NULL;

/* Private function declarations *******************************************************/

/**
 * @brief Timezone service callback
 * @param[in] device Device handle
 * @param[in] param Parameter handle
 * @param[in] val Parameter value
 * @param[in] priv_data Private data
 * @param[in] ctx Write contexts
 * @return ESP_RMAKER_OK on success
 * @return ESP_RMAKER_FAIL on failure
 */
static esp_rmaker_error_t __timezone_service_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx);

/**
 * @brief Create a timezone service
 * @param[in] timezone Timezone string
 * @param[in] timezone_posix POSIX timezone string
 * @param[in] priv_data Private data
 * @return The timezone service handle
 */
static esp_rmaker_device_t *__timezone_service_create(const char *timezone, const char *timezone_posix, void *priv_data);

/**
 * @brief Timezone change callback
 * @param[in] timezone Timezone string
 */
static void __timezone_local_change_cb(const char *timezone);

/**
 * @brief POSIX timezone change callback
 * @param[in] timezone_posix POSIX timezone string
 */
static void __timezone_posix_local_change_cb(const char *timezone_posix);

/* Private function definitions *******************************************************/

static esp_rmaker_error_t __timezone_service_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    int err = -1;
    if (strcmp(esp_rmaker_param_get_type(param), ESP_RMAKER_PARAM_TIMEZONE) == 0) {
        OSAL_LOGI(TAG, "Received value = %s for %s - %s",
                  val.val.s, esp_rmaker_device_get_id(device), esp_rmaker_param_get_id(param));
        err = osal_timesync_set_timezone(val.val.s);
        if (err == 0) {
            char *tz_posix = osal_timesync_get_timezone_posix();
            if (tz_posix) {
                esp_rmaker_param_t *tz_posix_param = esp_rmaker_device_get_param_by_type(
                        device, ESP_RMAKER_PARAM_TIMEZONE_POSIX);
                esp_rmaker_param_update(tz_posix_param, esp_rmaker_str(tz_posix));
                free(tz_posix);
            }
        }
    } else if (strcmp(esp_rmaker_param_get_type(param), ESP_RMAKER_PARAM_TIMEZONE_POSIX) == 0) {
        OSAL_LOGI(TAG, "Received value = %s for %s - %s",
                  val.val.s, esp_rmaker_device_get_id(device), esp_rmaker_param_get_id(param));
        err = osal_timesync_set_timezone_posix(val.val.s);
    }
#if CONFIG_ESP_RMAKER_NEO_TIME_CURRENT_TIME_PARAM
    else if (strcmp(esp_rmaker_param_get_type(param), ESP_RMAKER_PARAM_TIMESTAMP) == 0) {
        const int64_t epoch_s = val.val.i64;
        OSAL_LOGI(TAG, "Received value = %" PRId64 " for %s - %s",
                  epoch_s, esp_rmaker_device_get_id(device), esp_rmaker_param_get_id(param));
#if CONFIG_ESP_RMAKER_NEO_TIME_CLOCK_OWNER_EXTERNAL
        const bool owner_set_clock = osal_timesync_is_synced();
#else
        const bool owner_set_clock = osal_timesync_sntp_has_synced();
#endif
        if (owner_set_clock) {
            OSAL_LOGW(TAG, "The clock owner set the clock, so the Timestamp write is ignored");
        } else if (epoch_s <= 0 || epoch_s > (int64_t)UINT32_MAX || osal_timesync_set_time(epoch_s * 1000) != 0) {
            OSAL_LOGE(TAG, "Failed to set local time");
            return ESP_RMAKER_FAIL;
        }
        esp_rmaker_param_update(param, esp_rmaker_int64(osal_get_time(NULL)));
        return ESP_RMAKER_OK;
    }
#endif /* CONFIG_ESP_RMAKER_NEO_TIME_CURRENT_TIME_PARAM */
    if (err == 0) {
        esp_rmaker_param_update(param, val);
    }
    return err == 0 ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

static esp_rmaker_device_t *__timezone_service_create(const char *timezone, const char *timezone_posix, void *priv_data)
{
    esp_rmaker_device_t *service = esp_rmaker_service_create(RMAKER_SERVICES_TIMEZONE_SERVICE_ID, ESP_RMAKER_SERVICE_TIME, priv_data);
    if (service) {
        uint8_t properties = PROP_FLAG_READ | PROP_FLAG_WRITE;
        esp_rmaker_param_t *tz_param = esp_rmaker_param_create(RMAKER_SERVICES_TIMEZONE_PARAM_ID, ESP_RMAKER_PARAM_TIMEZONE, esp_rmaker_str(timezone), properties);
        esp_rmaker_param_t *tz_posix_param = esp_rmaker_param_create(RMAKER_SERVICES_TIMEZONE_POSIX_PARAM_ID, ESP_RMAKER_PARAM_TIMEZONE_POSIX, esp_rmaker_str(timezone_posix), properties);
        esp_rmaker_device_add_param(service, tz_param);
        esp_rmaker_device_add_param(service, tz_posix_param);
#if CONFIG_ESP_RMAKER_NEO_TIME_CURRENT_TIME_PARAM
        /* 0 until the first write; a boot-time clock reading would go stale */
        esp_rmaker_device_add_param(service, esp_rmaker_timestamp_param_create(RMAKER_SERVICES_TIMESTAMP_PARAM_ID, 0));
#endif
    }
    return service;
}

static void __timezone_local_change_cb(const char *timezone)
{
    esp_rmaker_param_t *tz_param = esp_rmaker_device_get_param_by_type(__timezone_service, ESP_RMAKER_PARAM_TIMEZONE);
    esp_rmaker_param_update(tz_param, esp_rmaker_str(timezone));
}

static void __timezone_posix_local_change_cb(const char *timezone_posix)
{
    esp_rmaker_param_t *tz_posix_param = esp_rmaker_device_get_param_by_type(__timezone_service, ESP_RMAKER_PARAM_TIMEZONE_POSIX);
    esp_rmaker_param_update(tz_posix_param, esp_rmaker_str(timezone_posix));
}

/* Public function definitions *******************************************************/

esp_rmaker_error_t esp_rmaker_timezone_service_add_to_node(const char *timezone, const char *timezone_posix, esp_rmaker_timezone_service_callbacks_t *callbacks)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    const esp_rmaker_node_t *node = esp_rmaker_get_node();
    esp_rmaker_device_t *service = __timezone_service_create(timezone, timezone_posix, NULL);
    if (!service) {
        OSAL_LOGE(TAG, "Failed to create timezone service");
        return ESP_RMAKER_NO_MEM;
    }

    __timezone_service = service;
    esp_rmaker_device_add_cb(service, __timezone_service_cb, NULL);

    err = esp_rmaker_node_add_device(node, service);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add timezone service to node");
        esp_rmaker_device_delete(service);
        __timezone_service = NULL;
        return err;
    }

    if (callbacks) {
        callbacks->timezone_local_change_cb = __timezone_local_change_cb;
        callbacks->timezone_posix_local_change_cb = __timezone_posix_local_change_cb;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_timezone_service_remove_from_node(void)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    if (__timezone_service) {
        err = esp_rmaker_node_remove_device(esp_rmaker_get_node(), __timezone_service);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to remove timezone service from node");
            return err;
        }
        esp_rmaker_device_delete(__timezone_service);
        __timezone_service = NULL;
    }
    return ESP_RMAKER_OK;
}
