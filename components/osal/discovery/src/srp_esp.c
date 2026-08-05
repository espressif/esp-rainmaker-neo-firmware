/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file srp_esp.c
 * @brief OpenThread Service Registration Protocol (SRP) implementation for ESP-IDF.
 */

/* Includes *******************************************************/

/* Declaration includes. */
#include "osal_discovery.h"

/* ESP-IDF includes. */
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "openthread/srp_client.h"
#include "openthread/srp_client_buffers.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

/* Standard includes. */
#include <string.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* Constants *******************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "osal_disc_srp";

/**
 * @brief Maximum host name length for SRP.
 */
#define SRP_MAX_HOST_NAME_LEN 40

/**
 * @brief Default lease interval in seconds.
 */
#define SRP_DEFAULT_LEASE_INTERVAL 3600

/**
 * @brief Max length for full SRP service name (type.protocol).
 */
#define SRP_MAX_FULL_SERVICE_NAME_LEN 128

/**
 * @brief Max TXT entries for add_service / extra SRP services.
 */
#define SRP_MAX_USER_TXT_ENTRIES 16

/**
 * @brief Linked list node for additional SRP services (after primary registration).
 */
typedef struct srp_extra_service_node {
    struct srp_extra_service_node *next;
    otSrpClientService service;
    char full_name[SRP_MAX_FULL_SERVICE_NAME_LEN];
    char instance_name[SRP_MAX_HOST_NAME_LEN + 1];
    otDnsTxtEntry txt_entries[SRP_MAX_USER_TXT_ENTRIES];
    uint8_t txt_value_buffers[SRP_MAX_USER_TXT_ENTRIES][SRP_MAX_HOST_NAME_LEN + 1];
} srp_extra_service_node_t;

/* Private types *******************************************************/

/**
 * @brief SRP host name storage.
 */
static char srp_host_name[SRP_MAX_HOST_NAME_LEN + 1] = {0};

/**
 * @brief Primary SRP service instance label (must outlive registration).
 */
static char srp_primary_instance_name[SRP_MAX_HOST_NAME_LEN + 1] = {0};

/**
 * @brief TXT storage when primary service is updated via add_service() (user TXT only).
 */
static otDnsTxtEntry srp_primary_addon_txt_entries[SRP_MAX_USER_TXT_ENTRIES];
static uint8_t srp_primary_addon_txt_buf[SRP_MAX_USER_TXT_ENTRIES][SRP_MAX_HOST_NAME_LEN + 1];

/**
 * @brief SRP service instance.
 */
static otSrpClientService srp_client_service = {0};

/**
 * @brief Flag to track if SRP service is registered.
 */
static bool srp_service_registered = false;

/**
 * @brief HTTP port cached in on_start for add_service (same as mDNS backend).
 */
static uint16_t srp_cached_service_port = 0;

/**
 * @brief Full service name cached in on_start for add_service (same as mDNS backend).
 */
static char srp_cached_full_service_name[SRP_MAX_FULL_SERVICE_NAME_LEN + 1] = {0};

/**
 * @brief Extra SRP services registered after on_start.
 */
static srp_extra_service_node_t *srp_extra_services = NULL;

/* Private function declarations *******************************************************/

/**
 * @brief Set SRP client host name.
 * @param[in] host_name The host name to set.
 * @return OSAL_ERR_OK on success, error code otherwise.
 */
static osal_err_t srp_client_set_host(const char *host_name);

/**
 * @brief True if type/protocol match the primary local control discovery service.
 */
static bool srp_is_primary_discovery_service(const char *service_type, const char *service_protocol);

/**
 * @brief Format full SRP service name into buf as "%s.%s".
 * @return Bytes written (excluding NUL) on success, or < 0 on error/truncation risk.
 */
static int srp_format_full_service_name(char *buf, size_t buflen, const char *service_type,
                                        const char *service_protocol);

/**
 * @brief Copy txt_items into DNS TXT entries and value buffers.
 * @return Number of entries written.
 */
static size_t srp_copy_txt_items(otDnsTxtEntry *entries,
                                 uint8_t (*value_buf)[SRP_MAX_HOST_NAME_LEN + 1],
                                 size_t max_entries,
                                 const osal_discovery_txt_items_t *txt_items);

/**
 * @brief Remove and free extra-node matching type.protocol, if any.
 */
static void srp_extra_remove_by_type(otInstance *instance, const char *service_type,
                                     const char *service_protocol);

/**
 * @brief Free all extra service nodes (after host/services removed or without OT remove).
 */
static void srp_extra_free_all(void);

/**
 * @brief Reset cached SRP discovery runtime state.
 */
static void srp_reset_cached_state(void);

/* Private function definitions *******************************************************/

static osal_err_t srp_client_set_host(const char *host_name)
{
    osal_err_t ret = OSAL_ERR_OK;
    bool locked = false;

    if (!host_name || strlen(host_name) > SRP_MAX_HOST_NAME_LEN) {
        ESP_LOGE(TAG, "Invalid host name");
        return OSAL_ERR_INVALID_ARG;
    }

    // Avoid setting the same host name multiple times
    if (strcmp(srp_host_name, host_name) == 0) {
        return OSAL_ERR_OK;
    }

    strncpy(srp_host_name, host_name, SRP_MAX_HOST_NAME_LEN);
    srp_host_name[SRP_MAX_HOST_NAME_LEN] = '\0';

    esp_openthread_lock_acquire(portMAX_DELAY);
    locked = true;

    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance not available");
        ret = OSAL_ERR_FAIL;
        goto srp_client_set_host_exit;
    }

    if (otSrpClientSetHostName(instance, srp_host_name) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set SRP host name");
        ret = OSAL_ERR_FAIL;
        goto srp_client_set_host_exit;
    }

    if (otSrpClientEnableAutoHostAddress(instance) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to enable auto host address");
        ret = OSAL_ERR_FAIL;
        goto srp_client_set_host_exit;
    }

    ESP_LOGD(TAG, "SRP host name set to: %s", srp_host_name);
srp_client_set_host_exit:
    if (locked) {
        esp_openthread_lock_release();
    }
    return ret;
}

static bool srp_is_primary_discovery_service(const char *service_type, const char *service_protocol)
{
    char want[SRP_MAX_FULL_SERVICE_NAME_LEN];
    if (srp_format_full_service_name(want, sizeof(want), service_type, service_protocol) < 0) {
        return false;
    }
    return strcmp(srp_cached_full_service_name, want) == 0;
}

static int srp_format_full_service_name(char *buf, size_t buflen, const char *service_type,
                                        const char *service_protocol)
{
    int n = snprintf(buf, buflen, "%s.%s", service_type, service_protocol);
    if (n < 0 || (size_t)n >= buflen) {
        return -1;
    }
    return n;
}

static size_t srp_copy_txt_items(otDnsTxtEntry *entries,
                                 uint8_t (*value_buf)[SRP_MAX_HOST_NAME_LEN + 1],
                                 size_t max_entries,
                                 const osal_discovery_txt_items_t *txt_items)
{
    size_t idx = 0;
    for (size_t i = 0; i < txt_items->count && idx < max_entries; i++) {
        const osal_discovery_txt_item_t *txt_item = &txt_items->list[i];
        if (!txt_item || !txt_item->var || !txt_item->val) {
            continue;
        }
        size_t val_len = strlen(txt_item->val);
        if (val_len > SRP_MAX_HOST_NAME_LEN) {
            val_len = SRP_MAX_HOST_NAME_LEN;
        }
        memcpy(value_buf[idx], txt_item->val, val_len);
        value_buf[idx][val_len] = '\0';
        entries[idx] = (otDnsTxtEntry) {
            .mKey = txt_item->var,
            .mValue = value_buf[idx],
            .mValueLength = (uint16_t)val_len,
        };
        idx++;
    }
    return idx;
}

static void srp_extra_remove_by_type(otInstance *instance, const char *service_type,
                                     const char *service_protocol)
{
    char want[SRP_MAX_FULL_SERVICE_NAME_LEN];
    if (srp_format_full_service_name(want, sizeof(want), service_type, service_protocol) < 0) {
        return;
    }

    srp_extra_service_node_t **prev = &srp_extra_services;
    while (*prev) {
        srp_extra_service_node_t *node = *prev;
        if (strcmp(node->full_name, want) == 0) {
            if (instance) {
                (void)otSrpClientRemoveService(instance, &node->service);
            }
            *prev = node->next;
            free(node);
            return;
        }
        prev = &node->next;
    }
}

static void srp_extra_free_all(void)
{
    while (srp_extra_services) {
        srp_extra_service_node_t *node = srp_extra_services;
        srp_extra_services = node->next;
        free(node);
    }
}

static void srp_reset_cached_state(void)
{
    memset(srp_host_name, 0, sizeof(srp_host_name));
    memset(srp_primary_instance_name, 0, sizeof(srp_primary_instance_name));
    memset(srp_cached_full_service_name, 0, sizeof(srp_cached_full_service_name));
    memset(&srp_client_service, 0, sizeof(srp_client_service));
    srp_service_registered = false;
    srp_cached_service_port = 0;
    srp_extra_free_all();
}

/* Public function definitions *******************************************************/

osal_err_t osal_discovery_init(const osal_discovery_service_config_t *service_config)
{
    if (!service_config || !service_config->name) {
        ESP_LOGE(TAG, "Invalid service configuration");
        return OSAL_ERR_INVALID_ARG;
    }

    // Set SRP host name
    const char *service_name = service_config->name;
    osal_err_t err = srp_client_set_host(service_name);
    if (err != OSAL_ERR_OK) {
        ESP_LOGE(TAG, "Failed to set SRP host name");
        return err;
    }
    srp_cached_service_port = service_config->port;
    ESP_LOGD(TAG, "SRP initialized with hostname: %s", service_name);
    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_deinit(void)
{
    // Clean up SRP host and services
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *instance = esp_openthread_get_instance();
    if (instance) {
        otSrpClientRemoveHostAndServices(instance, false, true);
    }
    esp_openthread_lock_release();

    srp_reset_cached_state();

    ESP_LOGD(TAG, "SRP deinitialized");
    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_add_service(const char *service_type, const char *service_protocol,
                                      const char *instance_name,
                                      const osal_discovery_txt_items_t *txt_items)
{
    osal_err_t ret = OSAL_ERR_OK;
    bool locked = false;
    otInstance *instance = NULL;
    srp_extra_service_node_t *node = NULL;

    if (!service_type || !service_protocol || !instance_name || !txt_items) {
        ESP_LOGE(TAG, "Invalid arguments");
        return OSAL_ERR_INVALID_ARG;
    }
    if (srp_cached_service_port == 0) {
        return OSAL_ERR_INVALID_STATE;
    }

    esp_openthread_lock_acquire(portMAX_DELAY);
    locked = true;
    instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance not available");
        ret = OSAL_ERR_FAIL;
        goto osal_discovery_add_service_fail;
    }

    if (srp_is_primary_discovery_service(service_type, service_protocol)) {
        strncpy(srp_primary_instance_name, instance_name, SRP_MAX_HOST_NAME_LEN);
        srp_primary_instance_name[SRP_MAX_HOST_NAME_LEN] = '\0';

        size_t txt_count = srp_copy_txt_items(srp_primary_addon_txt_entries, srp_primary_addon_txt_buf,
                                              SRP_MAX_USER_TXT_ENTRIES, txt_items);

        (void)otSrpClientRemoveService(instance, &srp_client_service);

        srp_client_service.mName = srp_cached_full_service_name;
        /* mName must point to persistent string; same literal style as on_start */
        srp_client_service.mInstanceName = srp_primary_instance_name;
        srp_client_service.mTxtEntries = srp_primary_addon_txt_entries;
        srp_client_service.mPort = srp_cached_service_port;
        srp_client_service.mNumTxtEntries = (uint8_t)txt_count;
        srp_client_service.mNext = NULL;
        srp_client_service.mLease = SRP_DEFAULT_LEASE_INTERVAL;
        srp_client_service.mKeyLease = 0;

        otError error = otSrpClientAddService(instance, &srp_client_service);
        if (error != OT_ERROR_NONE) {
            ESP_LOGE(TAG, "Failed to add primary SRP service: %d", error);
            ret = OSAL_ERR_FAIL;
            goto osal_discovery_add_service_fail;
        }
        otSrpClientEnableAutoStartMode(instance, NULL, NULL);
        srp_service_registered = true;
        goto osal_discovery_add_service_done;
    }

    srp_extra_remove_by_type(instance, service_type, service_protocol);

    node = calloc(1, sizeof(srp_extra_service_node_t));
    if (!node) {
        ret = OSAL_ERR_NO_MEM;
        goto osal_discovery_add_service_fail;
    }

    if (srp_format_full_service_name(node->full_name, sizeof(node->full_name), service_type,
                                     service_protocol) < 0) {
        ESP_LOGE(TAG, "Service name too long");
        ret = OSAL_ERR_FAIL;
        goto osal_discovery_add_service_fail;
    }

    strncpy(node->instance_name, instance_name, SRP_MAX_HOST_NAME_LEN);
    node->instance_name[SRP_MAX_HOST_NAME_LEN] = '\0';

    size_t txt_count = srp_copy_txt_items(node->txt_entries, node->txt_value_buffers,
                                          SRP_MAX_USER_TXT_ENTRIES, txt_items);

    node->service.mName = node->full_name;
    node->service.mInstanceName = node->instance_name;
    node->service.mTxtEntries = node->txt_entries;
    node->service.mPort = srp_cached_service_port;
    node->service.mNumTxtEntries = (uint8_t)txt_count;
    node->service.mNext = NULL;
    node->service.mLease = SRP_DEFAULT_LEASE_INTERVAL;
    node->service.mKeyLease = 0;

    otError error = otSrpClientAddService(instance, &node->service);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to add extra SRP service: %d", error);
        ret = OSAL_ERR_FAIL;
        goto osal_discovery_add_service_fail;
    }
    otSrpClientEnableAutoStartMode(instance, NULL, NULL);

    node->next = srp_extra_services;
    srp_extra_services = node;
    node = NULL;

osal_discovery_add_service_done:
    if (locked) {
        esp_openthread_lock_release();
    }
    return ret;

osal_discovery_add_service_fail:
    if (node) {
        free(node);
    }
    if (locked) {
        esp_openthread_lock_release();
    }
    return ret;
}

osal_err_t osal_discovery_remove_service(const char *service_type, const char *service_protocol)
{
    if (!service_type || !service_protocol) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (srp_cached_service_port == 0) {
        return OSAL_ERR_INVALID_STATE;
    }

    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        esp_openthread_lock_release();
        return OSAL_ERR_FAIL;
    }

    if (srp_is_primary_discovery_service(service_type, service_protocol)) {
        (void)otSrpClientRemoveService(instance, &srp_client_service);
        esp_openthread_lock_release();
        srp_service_registered = false;
        return OSAL_ERR_OK;
    }

    srp_extra_remove_by_type(instance, service_type, service_protocol);
    esp_openthread_lock_release();
    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_on_start(const osal_discovery_service_config_t *service_config, const osal_discovery_transport_config_t *transport_config)
{
    osal_err_t ret = OSAL_ERR_OK;
    bool locked = false;

    if (!service_config || !service_config->name || !service_config->type || !service_config->protocol || !transport_config) {
        ESP_LOGE(TAG, "Invalid service or transport configuration");
        return OSAL_ERR_INVALID_ARG;
    }

    if (transport_config->type != OSAL_DISCOVERY_TRANSPORT_HTTPD) {
        ESP_LOGE(TAG, "Unsupported transport type");
        return OSAL_ERR_INVALID_ARG;
    }

    // Prepare TXT entries: 3 fixed endpoints + all txt_items from service_config
    static const uint8_t text_values[3][23] = {
        {'/', 'e', 's', 'p', '_', 'l', 'o', 'c', 'a', 'l', '_', 'c', 't', 'r', 'l', '/', 'v', 'e', 'r', 's', 'i', 'o', 'n'},
        {'/', 'e', 's', 'p', '_', 'l', 'o', 'c', 'a', 'l', '_', 'c', 't', 'r', 'l', '/', 's', 'e', 's', 's', 'i', 'o', 'n'},
        {'/', 'e', 's', 'p', '_', 'l', 'o', 'c', 'a', 'l', '_', 'c', 't', 'r', 'l', '/', 'c', 'o', 'n', 't', 'r', 'o', 'l'}
    };

    // Calculate total number of TXT entries: 3 endpoints + txt_items count
    size_t total_txt_entries = 3 + service_config->txt_items.count;

    // Use a static array with reasonable max size (3 endpoints + up to 16 txt_items)
    // If txt_items.count exceeds this, we'll limit it
#define MAX_TXT_ENTRIES 19  // 3 endpoints + 16 txt_items
    static otDnsTxtEntry txt_entries[MAX_TXT_ENTRIES];
    static uint8_t txt_value_buffers[MAX_TXT_ENTRIES - 3][SRP_MAX_HOST_NAME_LEN + 1];

    if (total_txt_entries > MAX_TXT_ENTRIES) {
        ESP_LOGW(TAG, "Too many txt_items (%" PRIu64 "), limiting to %d entries",
                 (uint64_t)service_config->txt_items.count, MAX_TXT_ENTRIES - 3);
        total_txt_entries = MAX_TXT_ENTRIES;
    }

    // Set the 3 fixed endpoint entries
    txt_entries[0] = (otDnsTxtEntry) {
        .mKey = "version_endpoint",
        .mValue = text_values[0],
        .mValueLength = 23,
    };
    txt_entries[1] = (otDnsTxtEntry) {
        .mKey = "session_endpoint",
        .mValue = text_values[1],
        .mValueLength = 23,
    };
    txt_entries[2] = (otDnsTxtEntry) {
        .mKey = "control_endpoint",
        .mValue = text_values[2],
        .mValueLength = 23,
    };

    // Append all txt_items entries after the 3 endpoint entries
    size_t txt_entry_idx = 3;  // Start after the 3 endpoint entries
    for (size_t i = 0; i < service_config->txt_items.count && txt_entry_idx < total_txt_entries; i++) {
        const osal_discovery_txt_item_t *txt_item = &service_config->txt_items.list[i];
        if (!txt_item || !txt_item->var || !txt_item->val) {
            continue;  // Skip invalid entries
        }

        // Copy value to buffer (SRP expects uint8_t*)
        size_t val_len = strlen(txt_item->val);
        if (val_len > SRP_MAX_HOST_NAME_LEN) {
            val_len = SRP_MAX_HOST_NAME_LEN;
        }
        memcpy(txt_value_buffers[txt_entry_idx - 3], txt_item->val, val_len);
        txt_value_buffers[txt_entry_idx - 3][val_len] = '\0';

        txt_entries[txt_entry_idx] = (otDnsTxtEntry) {
            .mKey = txt_item->var,
            .mValue = txt_value_buffers[txt_entry_idx - 3],
            .mValueLength = (uint16_t)val_len,
        };
        txt_entry_idx++;
    }

    // Update total count to reflect actual entries added (in case some were skipped)
    total_txt_entries = txt_entry_idx;

    strncpy(srp_primary_instance_name, service_config->name, SRP_MAX_HOST_NAME_LEN);
    srp_primary_instance_name[SRP_MAX_HOST_NAME_LEN] = '\0';

    // Get port from transport config
    uint16_t service_port = transport_config->httpd.port;

    // Configure SRP service
    ret = srp_format_full_service_name(srp_cached_full_service_name, sizeof(srp_cached_full_service_name), service_config->type, service_config->protocol);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to format full service name");
        return OSAL_ERR_FAIL;
    }
    ESP_LOGI(TAG, "Full service name: %s", srp_cached_full_service_name);
    srp_client_service.mName = srp_cached_full_service_name;
    srp_client_service.mInstanceName = srp_primary_instance_name;
    srp_client_service.mTxtEntries = txt_entries;
    srp_client_service.mPort = service_port;
    srp_client_service.mNumTxtEntries = (uint8_t)total_txt_entries;
    srp_client_service.mNext = NULL;
    srp_client_service.mLease = SRP_DEFAULT_LEASE_INTERVAL;
    srp_client_service.mKeyLease = 0;

    esp_openthread_lock_acquire(portMAX_DELAY);
    locked = true;

    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance not available");
        ret = OSAL_ERR_FAIL;
        goto osal_discovery_on_start_fail;
    }

    // Ensure hostname is properly set BEFORE adding the service
    // This ensures the hostname A record is registered along with the service
    // The border router needs both hostname and service to bridge properly
    // According to ESP Thread BR docs: hostname must be set before service
    if (srp_host_name[0] != '\0') {
        // Set hostname if not already set (we track it in srp_host_name)
        ESP_LOGD(TAG, "Ensuring SRP hostname is set to: %s (before adding service)", srp_host_name);
        if (otSrpClientSetHostName(instance, srp_host_name) != OT_ERROR_NONE) {
            ESP_LOGD(TAG, "Failed to set SRP host name");
        } else {
            // Enable auto host address (equivalent to "srp client host address auto")
            // This automatically uses all preferred unicast IPv6 addresses
            if (otSrpClientEnableAutoHostAddress(instance) != OT_ERROR_NONE) {
                ESP_LOGD(TAG, "Failed to enable auto host address");
            } else {
                ESP_LOGD(TAG, "SRP hostname and auto host address configured");
            }
        }
    }

    // Try to remove the service registered before adding a new service
    // This prevents duplicate instance errors when device reboots
    (void)otSrpClientRemoveService(instance, &srp_client_service);

    // Add the service (this will trigger SRP registration with both hostname and service)
    otError error = otSrpClientAddService(instance, &srp_client_service);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to add SRP service: %d", error);
        ret = OSAL_ERR_FAIL;
        goto osal_discovery_on_start_fail;
    }

    // Enable auto-start mode (equivalent to "srp client autostart enable")
    // This automatically sends the registration to the SRP server
    otSrpClientEnableAutoStartMode(instance, NULL, NULL);
    esp_openthread_lock_release();
    locked = false;

    srp_cached_service_port = service_port;
    srp_service_registered = true;
    ESP_LOGD(TAG, "SRP service registered: %s on port %d", srp_primary_instance_name, service_port);
    return OSAL_ERR_OK;

osal_discovery_on_start_fail:
    if (locked) {
        esp_openthread_lock_release();
    }
    memset(srp_cached_full_service_name, 0, sizeof(srp_cached_full_service_name));
    memset(&srp_client_service, 0, sizeof(srp_client_service));
    srp_cached_service_port = 0;
    srp_service_registered = false;
    return ret;
}

osal_err_t osal_discovery_on_stop(void)
{
    if (!srp_service_registered) {
        return OSAL_ERR_OK;
    }

    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *instance = esp_openthread_get_instance();
    if (instance) {
        otSrpClientRemoveService(instance, &srp_client_service);
    }
    esp_openthread_lock_release();

    srp_service_registered = false;
    ESP_LOGD(TAG, "SRP service stopped");
    return OSAL_ERR_OK;
}
