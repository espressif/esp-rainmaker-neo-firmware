/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file migration.c
 * @brief MQTT host migration.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "credentials/migration.h"

/* Standard includes */
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Credentials includes */
#include "esp_rmaker_credentials.h"
#include "util/esp_rmaker_crypto.h"

/* Event includes */
#include "esp_rmaker_common_events.h"
#include "event_loop.h"

/* Platform includes */
#include "osal_event_loop.h"
#include "osal_log.h"
#include "osal_ota.h"
#include "osal_scheduler.h"
#include "osal_ticks.h"

/* Constants **********************************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "rmng_migration";

/**
 * @brief The write-back handlers are registered.
 */
#define RMAKER_MIGRATION_STARTED (1U << 8)

/**
 * @brief The node connected to the target host at least once.
 */
#define RMAKER_MIGRATION_MQTT_CONNECTED (1U << 9)

/**
 * @brief The running image cannot roll back.
 */
#define RMAKER_MIGRATION_IMAGE_VALID (1U << 10)

/**
 * @brief The flags that allow the write-back.
 */
#define RMAKER_MIGRATION_WRITE_READY (RMAKER_MIGRATION_MQTT_CONNECTED | RMAKER_MIGRATION_IMAGE_VALID)

/* Variables **********************************************************************/

/**
 * @brief The MQTT host to move to.
 */
static const char *__target_host = NULL;

/**
 * @brief The certificate CN, in use as the node ID.
 */
static char *__cn_client_id = NULL;

/**
 * @brief The RMAKER_MIGRATION_* flags.
 */
static uint32_t __flags = 0;

/**
 * @brief The target host connection timeout.
 */
static osal_scheduler_task_handle_t __timeout_task = NULL;

/**
 * @brief The target host connection timeout in seconds.
 */
static uint32_t __timeout_s = 0;

/* Private function definitions ****************************************************/

static esp_rmaker_error_t __get_target_mqtt_host(char **p_mqtt_host)
{
    if (!p_mqtt_host || !__target_host) {
        return ESP_RMAKER_INVALID_ARG;
    }
    char *mqtt_host = strdup(__target_host);
    if (!mqtt_host) {
        return ESP_RMAKER_NO_MEM;
    }
    *p_mqtt_host = mqtt_host;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __get_cn_client_id(char **p_client_id)
{
    if (!p_client_id || !__cn_client_id) {
        return ESP_RMAKER_INVALID_ARG;
    }
    char *client_id = strdup(__cn_client_id);
    if (!client_id) {
        return ESP_RMAKER_NO_MEM;
    }
    *p_client_id = client_id;
    return ESP_RMAKER_OK;
}

/**
 * @brief Read the certificate CN, and use it as the node ID in memory.
 * @return ESP_RMAKER_NOT_FOUND if there is no certificate or the certificate has no CN.
 */
static esp_rmaker_error_t __use_cert_cn(void)
{
    esp_rmaker_credential_t cert = { 0 };
    esp_rmaker_error_t err = esp_rmaker_credentials_get_client_cert(&cert);
    if (err != ESP_RMAKER_OK) {
        return ESP_RMAKER_NOT_FOUND;
    }
    char *cn = NULL;
    err = esp_rmaker_crypto_get_cert_cn(cert.credential, cert.len, &cn);
    esp_rmaker_credentials_free_credential(&cert);
    if (err != ESP_RMAKER_OK) {
        return ESP_RMAKER_NOT_FOUND;
    }

    __cn_client_id = cn;
    esp_rmaker_credentials_providers_t providers = { .client_id = __get_cn_client_id };
    err = esp_rmaker_credentials_override(&providers);
    if (err != ESP_RMAKER_OK) {
        free(__cn_client_id);
        __cn_client_id = NULL;
        return err;
    }
    OSAL_LOGW(TAG, "No node ID is stored. Using the certificate CN: %s", cn);
    return ESP_RMAKER_OK;
}

static bool __image_pending_verify(void)
{
    const osal_ota_partition_t *running = osal_ota_get_running_partition();
    osal_ota_img_states_t ota_state = OSAL_OTA_IMG_UNDEFINED;
    /* A partition without an OTA state cannot roll back. */
    if (!running || osal_ota_get_state_partition(running, &ota_state) != OSAL_ERR_OK) {
        return false;
    }
    return ota_state == OSAL_OTA_IMG_PENDING_VERIFY;
}

/**
 * @brief Write the node ID, then the MQTT host, to the credentials storage.
 */
static esp_rmaker_error_t __write_back(void)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    if (__flags & RMAKER_MIGRATION_WRITE_CLIENT_ID) {
        char *client_id = NULL;
        err = esp_rmaker_credentials_get_thing_name(&client_id);
        if (err == ESP_RMAKER_OK) {
            err = esp_rmaker_credentials_set_client_id(client_id);
        }
        free(client_id);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to write the node ID: %d", err);
            return err;
        }
        __flags &= ~RMAKER_MIGRATION_WRITE_CLIENT_ID;
    }
    if (__flags & RMAKER_MIGRATION_WRITE_MQTT_HOST) {
        err = esp_rmaker_credentials_set_mqtt_host(__target_host);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to write the MQTT host: %d", err);
            return err;
        }
        __flags &= ~RMAKER_MIGRATION_WRITE_MQTT_HOST;
    }
    OSAL_LOGI(TAG, "Wrote the migrated credentials");
    return ESP_RMAKER_OK;
}

static void __cancel_timeout(void)
{
    if (__timeout_task) {
        osal_scheduler_cancel_task(&__timeout_task);
    }
}

static void __on_timeout(void)
{
    __cancel_timeout();
    if (__flags & RMAKER_MIGRATION_MQTT_CONNECTED) {
        return;
    }
    char *client_id = NULL;
    char *mqtt_host = NULL;
    esp_rmaker_credentials_get_thing_name(&client_id);
    esp_rmaker_credentials_get_mqtt_host(&mqtt_host);
    OSAL_LOGE(TAG, "Node %s did not connect to %s within %" PRIu32 " s",
              client_id ? client_id : "<none>", mqtt_host ? mqtt_host : "<none>", __timeout_s);
    free(client_id);
    free(mqtt_host);
    /* Check now, because the app can mark the image valid after the start. */
    if (__image_pending_verify()) {
        OSAL_LOGE(TAG, "Roll back the pending image to keep the stored MQTT host");
        osal_err_t osal_err = osal_ota_mark_app_invalid_rollback_and_reboot();
        if (osal_err == OSAL_ERR_OK) {
            return;
        }
        OSAL_LOGE(TAG, "Failed to roll back: %d", osal_err);
    }
    OSAL_LOGE(TAG, "The MQTT retries continue");
    osal_err_t osal_err = osal_event_post(RMAKER_EVENT, RMAKER_EVENT_MQTT_HOST_MIGRATION_TIMEOUT, NULL, 0, OSAL_MAX_DELAY);
    if (osal_err != OSAL_ERR_OK) {
        OSAL_LOGD(TAG, "Failed to post the migration timeout event: %d", osal_err);
    }
}

static void __timeout_task_fn(void *arg)
{
    osal_event_post(RMAKER_INTERNAL_EVENT, RMAKER_INTERNAL_EVENT_MIGRATION_TIMEOUT, NULL, 0, OSAL_MAX_DELAY);
}

/**
 * @brief Handle the migration events. All run on the event loop task.
 */
static void __on_event(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == RMAKER_COMMON_EVENT && event_id == RMAKER_MQTT_EVENT_CONNECTED) {
        __flags |= RMAKER_MIGRATION_MQTT_CONNECTED;
        __cancel_timeout();
        /* The app can mark the image valid without the OSAL call, which posts no event. */
        if (!(__flags & RMAKER_MIGRATION_IMAGE_VALID) && !__image_pending_verify()) {
            __flags |= RMAKER_MIGRATION_IMAGE_VALID;
        }
    } else if (event_base == RMAKER_INTERNAL_EVENT && event_id == RMAKER_INTERNAL_EVENT_OTA_APP_VALID) {
        __flags |= RMAKER_MIGRATION_IMAGE_VALID;
    } else if (event_base == RMAKER_INTERNAL_EVENT && event_id == RMAKER_INTERNAL_EVENT_MIGRATION_TIMEOUT) {
        __on_timeout();
        return;
    } else {
        return;
    }
    if ((__flags & RMAKER_MIGRATION_WRITE_READY) == RMAKER_MIGRATION_WRITE_READY && (__flags & RMAKER_MIGRATION_WRITE_ANY)) {
        __write_back();
    }
}

/* Public function definitions ****************************************************/

esp_rmaker_error_t esp_rmaker_migration_check(const char *target_host)
{
    if (!target_host || target_host[0] == '\0') {
        OSAL_LOGE(TAG, "MQTT host migration is on, but the target host is empty");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* A second node init after a deinit sees our own providers, so keep the first result. */
    if (__flags & RMAKER_MIGRATION_WRITE_ANY) {
        return ESP_RMAKER_OK;
    }

    char *client_id = NULL;
    esp_rmaker_error_t err = esp_rmaker_credentials_get_thing_name(&client_id);
    if (err == ESP_RMAKER_NOT_FOUND) {
        err = __use_cert_cn();
        if (err == ESP_RMAKER_NOT_FOUND) {
            OSAL_LOGE(TAG, "Cannot migrate to %s: no node ID and no certificate CN", target_host);
            return err;
        }
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to use the certificate CN: %d", err);
            return err;
        }
        __flags |= RMAKER_MIGRATION_WRITE_CLIENT_ID;
        err = esp_rmaker_credentials_get_thing_name(&client_id);
    }
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to read the node ID: %d", err);
        return err;
    }

    char *stored_host = NULL;
    err = esp_rmaker_credentials_get_mqtt_host(&stored_host);
    if (err != ESP_RMAKER_OK && err != ESP_RMAKER_NOT_FOUND) {
        OSAL_LOGE(TAG, "Failed to read the MQTT host: %d", err);
        free(client_id);
        return err;
    }
    if (stored_host && strcmp(stored_host, target_host) == 0) {
        free(stored_host);
        free(client_id);
        return ESP_RMAKER_OK;
    }

    __target_host = target_host;
    esp_rmaker_credentials_providers_t providers = { .mqtt_host = __get_target_mqtt_host };
    err = esp_rmaker_credentials_override(&providers);
    if (err == ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Node %s moves from %s to %s", client_id, stored_host ? stored_host : "<none>", target_host);
        __flags |= RMAKER_MIGRATION_WRITE_MQTT_HOST;
    } else {
        OSAL_LOGE(TAG, "Failed to use the target MQTT host: %d", err);
        __target_host = NULL;
    }
    free(stored_host);
    free(client_id);
    return err;
}

esp_rmaker_error_t esp_rmaker_migration_start(void)
{
    if (!(__flags & RMAKER_MIGRATION_WRITE_ANY) || (__flags & RMAKER_MIGRATION_STARTED)) {
        return ESP_RMAKER_OK;
    }
    if (!__image_pending_verify()) {
        __flags |= RMAKER_MIGRATION_IMAGE_VALID;
    }
    osal_err_t osal_err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, __on_event, NULL);
    if (osal_err == OSAL_ERR_OK) {
        osal_err = osal_event_handler_register(RMAKER_INTERNAL_EVENT, RMAKER_INTERNAL_EVENT_OTA_APP_VALID, __on_event, NULL);
    }
    if (osal_err == OSAL_ERR_OK) {
        osal_err = osal_event_handler_register(RMAKER_INTERNAL_EVENT, RMAKER_INTERNAL_EVENT_MIGRATION_TIMEOUT, __on_event, NULL);
    }
    __flags |= RMAKER_MIGRATION_STARTED;
    if (osal_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register the write-back handlers: %d", osal_err);
        esp_rmaker_migration_stop();
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_migration_on_network_up(uint32_t timeout_s)
{
    if (!(__flags & RMAKER_MIGRATION_STARTED) || __timeout_task) {
        return ESP_RMAKER_OK;
    }
    __timeout_s = timeout_s;
    osal_err_t osal_err = osal_scheduler_schedule_task(&__timeout_task, (uint64_t) timeout_s * 1000, __timeout_task_fn, NULL);
    if (osal_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to start the target host timeout: %d", osal_err);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

void esp_rmaker_migration_stop(void)
{
    if (!(__flags & RMAKER_MIGRATION_STARTED)) {
        return;
    }
    __cancel_timeout();
    osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, __on_event);
    osal_event_handler_unregister(RMAKER_INTERNAL_EVENT, RMAKER_INTERNAL_EVENT_OTA_APP_VALID, __on_event);
    osal_event_handler_unregister(RMAKER_INTERNAL_EVENT, RMAKER_INTERNAL_EVENT_MIGRATION_TIMEOUT, __on_event);
    __flags &= RMAKER_MIGRATION_WRITE_ANY;
}

esp_rmaker_migration_state_t esp_rmaker_migration_get_state(void)
{
    return __flags & RMAKER_MIGRATION_WRITE_ANY;
}

void esp_rmaker_migration_reset(void)
{
    esp_rmaker_migration_stop();
    __flags = 0;
    __target_host = NULL;
    free(__cn_client_id);
    __cn_client_id = NULL;
}
