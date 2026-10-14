/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file prov_console_cmd.c
 * @brief Console command that provisions the node over the configured network protocol.
 */

/* Includes **********************************************************************/

/* Configuration includes */
#include "sdkconfig.h"

/* Declarations */
#include "prov_console_cmd.h"

/* Standard includes */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ESP-IDF includes */
#include "esp_console.h"
#include "esp_event.h"
#include "esp_log.h"
#include "network_provisioning/manager.h"

#ifdef CONFIG_APP_NETWORK_OVER_WIFI
#include "esp_wifi.h"
#else /* CONFIG_APP_NETWORK_OVER_THREAD */
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "openthread/dataset.h"
#include "openthread/instance.h"
#include "openthread/ip6.h"
#include "openthread/thread.h"
#endif /* CONFIG_APP_NETWORK_OVER_THREAD */

/* Preprocessor definitions **********************************************************/

/* Console command exit codes (shell-style: 0 = success, non-zero = failure). */
#define CONSOLE_OK   0
#define CONSOLE_FAIL 1

#ifdef CONFIG_APP_NETWORK_OVER_WIFI
#define PROV_CMD_NAME          "wifi-prov"
#define PROV_CMD_HELP          "Wi-Fi provision the node. Usage: wifi-prov <ssid> [<passphrase>]"
#define PROV_CMD_SUCCESS_EVENT NETWORK_PROV_WIFI_CRED_SUCCESS
#define PROV_CMD_FAIL_EVENT    NETWORK_PROV_WIFI_CRED_FAIL
#else /* CONFIG_APP_NETWORK_OVER_THREAD */
#define PROV_CMD_NAME          "thread-prov"
#define PROV_CMD_HELP          "Thread provision the node. Usage: thread-prov <dataset_tlvs_hex>"
#define PROV_CMD_SUCCESS_EVENT NETWORK_PROV_THREAD_DATASET_SUCCESS
#define PROV_CMD_FAIL_EVENT    NETWORK_PROV_THREAD_DATASET_FAIL
#endif /* CONFIG_APP_NETWORK_OVER_THREAD */

/* Type definitions **********************************************************************/

/* Provisioning manager lifecycle, as seen through NETWORK_PROV_EVENT. */
typedef enum {
    PROV_STATE_NOT_READY = 0, /* Before NETWORK_PROV_INIT */
    PROV_STATE_MANAGER_IDLE,  /* Between INIT and START, or END and DEINIT */
    PROV_STATE_SESSION,       /* Between START and END */
    PROV_STATE_DONE,          /* After NETWORK_PROV_DEINIT */
} prov_state_t;

/* Constants **********************************************************************/

static const char *TAG = "app_network";

/* Variables **********************************************************************/

static atomic_int s_prov_state = PROV_STATE_NOT_READY;

/* Private function definitions **********************************************************/

static void prov_state_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void) arg;
    (void) base;
    (void) data;
    switch (id) {
    case NETWORK_PROV_INIT:
    case NETWORK_PROV_END:
        atomic_store(&s_prov_state, PROV_STATE_MANAGER_IDLE);
        break;
    case NETWORK_PROV_START:
        atomic_store(&s_prov_state, PROV_STATE_SESSION);
        break;
    case NETWORK_PROV_DEINIT:
        atomic_store(&s_prov_state, PROV_STATE_DONE);
        break;
    default:
        break;
    }
}

/* Disarms on the first result of the console attempt, so a later phone attempt is not stopped. */
static void prov_result_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void) arg;
    (void) base;
    (void) data;
    esp_event_handler_unregister(NETWORK_PROV_EVENT, PROV_CMD_SUCCESS_EVENT, &prov_result_handler);
    esp_event_handler_unregister(NETWORK_PROV_EVENT, PROV_CMD_FAIL_EVENT, &prov_result_handler);
    if (id == PROV_CMD_SUCCESS_EVENT) {
        network_prov_mgr_stop_provisioning();
    }
}

/* Console-supplied credentials leave no client waiting on the provisioning session, so stop it as
 * soon as they work instead of letting the manager's auto-stop timeout run down. */
static void stop_prov_on_success(void)
{
    esp_err_t err = esp_event_handler_register(NETWORK_PROV_EVENT, PROV_CMD_SUCCESS_EVENT, &prov_result_handler, NULL);
    if (err == ESP_OK) {
        err = esp_event_handler_register(NETWORK_PROV_EVENT, PROV_CMD_FAIL_EVENT, &prov_result_handler, NULL);
    }
    if (err != ESP_OK) {
        esp_event_handler_unregister(NETWORK_PROV_EVENT, PROV_CMD_SUCCESS_EVENT, &prov_result_handler);
        ESP_LOGW(TAG, "Failed to watch the provisioning result: %s", esp_err_to_name(err));
    }
}

static prov_state_t prov_state_get(void)
{
    return (prov_state_t) atomic_load(&s_prov_state);
}

#ifdef CONFIG_APP_NETWORK_OVER_WIFI
static int prov_handler(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        printf("Invalid Usage.\n");
        printf("Usage: %s <ssid> [<passphrase>]\n", PROV_CMD_NAME);
        return CONSOLE_FAIL;
    }

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));

    /* Both fields are fixed-size arrays that need no terminator, so a full-length value is valid. */
    size_t ssid_len = strlen(argv[1]);
    size_t pass_len = (argc == 3) ? strlen(argv[2]) : 0;
    if (ssid_len == 0 || ssid_len > sizeof(wifi_config.sta.ssid)) {
        printf("SSID must be 1 to %d characters\n", (int) sizeof(wifi_config.sta.ssid));
        return CONSOLE_FAIL;
    }
    if (pass_len > sizeof(wifi_config.sta.password)) {
        printf("Passphrase must be at most %d characters\n", (int) sizeof(wifi_config.sta.password));
        return CONSOLE_FAIL;
    }
    memcpy(wifi_config.sta.ssid, argv[1], ssid_len);
    if (pass_len) {
        memcpy(wifi_config.sta.password, argv[2], pass_len);
    }

    prov_state_t state = prov_state_get();
    if (state == PROV_STATE_SESSION) {
        if (network_prov_mgr_configure_wifi_sta(&wifi_config) != ESP_OK) {
            printf("Failed to pass the credentials to the provisioning manager\n");
            return CONSOLE_FAIL;
        }
        stop_prov_on_success();
        printf("Credentials passed to the provisioning manager\n");
        return CONSOLE_OK;
    }
    if (state != PROV_STATE_DONE) {
        printf("Provisioning manager is busy, retry in a few seconds\n");
        return CONSOLE_FAIL;
    }

    if (esp_wifi_stop() != ESP_OK) {
        printf("Failed to stop Wi-Fi\n");
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK) {
        printf("Failed to set the Wi-Fi configuration\n");
        return CONSOLE_FAIL;
    }
    if (esp_wifi_start() != ESP_OK) {
        printf("Failed to start Wi-Fi\n");
        return CONSOLE_FAIL;
    }
    /* ESP_ERR_WIFI_CONN: the rmaker_app_network STA_START handler connected first. */
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        printf("Failed to connect to %s\n", argv[1]);
        return CONSOLE_FAIL;
    }
    printf("Connecting to %s\n", argv[1]);
    return CONSOLE_OK;
}
#else /* CONFIG_APP_NETWORK_OVER_THREAD */
/* Parse an even-length hex string into out. Returns the byte count, or -1 on a bad string. */
static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_size)
{
    size_t hex_len = strlen(hex);
    if (hex_len == 0 || (hex_len % 2) != 0 || (hex_len / 2) > out_size) {
        return -1;
    }
    for (size_t i = 0; i < hex_len; i++) {
        char c = hex[i];
        uint8_t nibble;
        if (c >= '0' && c <= '9') {
            nibble = (uint8_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = (uint8_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            nibble = (uint8_t)(c - 'A' + 10);
        } else {
            return -1;
        }
        if ((i % 2) == 0) {
            out[i / 2] = (uint8_t)(nibble << 4);
        } else {
            out[i / 2] |= nibble;
        }
    }
    return (int)(hex_len / 2);
}

/* Apply an active dataset to a node that is no longer provisioning, restarting the Thread stack. */
static bool thread_set_active_dataset(const otOperationalDatasetTlvs *dataset)
{
    if (!esp_openthread_lock_acquire(portMAX_DELAY)) {
        return false;
    }
    otInstance *instance = esp_openthread_get_instance();
    (void) otThreadSetEnabled(instance, false);
    otError err = otDatasetSetActiveTlvs(instance, dataset);
    if (err == OT_ERROR_NONE) {
        (void) otIp6SetEnabled(instance, true);
        err = otThreadSetEnabled(instance, true);
    }
    esp_openthread_lock_release();
    return err == OT_ERROR_NONE;
}

static int prov_handler(int argc, char **argv)
{
    if (argc != 2) {
        printf("Invalid Usage.\n");
        printf("Usage: %s <dataset_tlvs_hex>\n", PROV_CMD_NAME);
        return CONSOLE_FAIL;
    }

    otOperationalDatasetTlvs dataset;
    memset(&dataset, 0, sizeof(dataset));
    int len = hex_to_bytes(argv[1], dataset.mTlvs, sizeof(dataset.mTlvs));
    if (len < 0) {
        printf("Dataset must be an even-length hex string of at most %d bytes\n", (int) sizeof(dataset.mTlvs));
        return CONSOLE_FAIL;
    }
    dataset.mLength = (uint8_t) len;

    prov_state_t state = prov_state_get();
    if (state == PROV_STATE_SESSION) {
        if (network_prov_mgr_configure_thread_dataset(&dataset) != ESP_OK) {
            printf("Failed to pass the dataset to the provisioning manager\n");
            return CONSOLE_FAIL;
        }
        stop_prov_on_success();
        printf("Dataset passed to the provisioning manager\n");
        return CONSOLE_OK;
    }
    if (state != PROV_STATE_DONE) {
        printf("Provisioning manager is busy, retry in a few seconds\n");
        return CONSOLE_FAIL;
    }

    if (!thread_set_active_dataset(&dataset)) {
        printf("Failed to set the active dataset\n");
        return CONSOLE_FAIL;
    }
    printf("Active dataset set (%d bytes), attaching\n", len);
    return CONSOLE_OK;
}
#endif /* CONFIG_APP_NETWORK_OVER_THREAD */

/* Public function definitions **********************************************************/

void app_network_register_prov_command(void)
{
    const esp_console_cmd_t cmd = {
        .command = PROV_CMD_NAME,
        .help = PROV_CMD_HELP,
        .hint = NULL,
        .func = &prov_handler,
        .argtable = NULL,
    };
    if (esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_state_handler, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to track the provisioning manager, not registering '%s'", cmd.command);
        return;
    }
    ESP_LOGI(TAG, "Registering command: %s", cmd.command);
    esp_err_t err = esp_console_cmd_register(&cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register command '%s': %s", cmd.command, esp_err_to_name(err));
    }
}
