/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_wifi.c
 * @brief Wi-Fi station bring-up for the test app, driven entirely by Kconfig.
 */

/* Includes *******************************************************************/

#include "test_wifi.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

/* Constants ******************************************************************/

static const char *TAG = "test_wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1

/* State **********************************************************************/

static EventGroupHandle_t s_wifi_events;
static int s_retries;
static bool s_initialized;
static bool s_started;
static bool s_connected;

/* Event handling *************************************************************/

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < CONFIG_TEST_APP_WIFI_CONNECT_RETRIES) {
            s_retries++;
            ESP_LOGW(TAG, "disconnected, retry %d/%d", s_retries, CONFIG_TEST_APP_WIFI_CONNECT_RETRIES);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&event->ip_info.ip));
        s_retries = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

/* Bring-up steps that must run once: a second esp_wifi_init() returns ESP_ERR_INVALID_STATE. */
static esp_err_t wifi_init_once(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (s_wifi_events == NULL) {
        s_wifi_events = xEventGroupCreate();
        if (s_wifi_events == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_ERROR_CHECK(esp_netif_init());

    /* Some suites create the default loop themselves; tolerate either order. */
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_TEST_APP_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_TEST_APP_WIFI_PASSWORD, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    s_initialized = true;
    return ESP_OK;
}

/* Public API *****************************************************************/

esp_err_t test_wifi_connect(void)
{
    if (s_connected) {
        return ESP_OK;
    }

    esp_err_t err = wifi_init_once();
    if (err != ESP_OK) {
        return err;
    }

    /* Restart the station after a failed attempt, so each call gets the full retry budget. */
    if (s_started) {
        err = esp_wifi_stop();
        if (err != ESP_OK) {
            return err;
        }
        s_started = false;
    }
    s_retries = 0;
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);

    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    s_started = true;

    ESP_LOGI(TAG, "connecting to '%s'", CONFIG_TEST_APP_WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_TEST_APP_WIFI_CONNECT_TIMEOUT_S * 1000));

    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGE(TAG, "failed to connect to '%s'", CONFIG_TEST_APP_WIFI_SSID);
        return (bits & WIFI_FAILED_BIT) ? ESP_FAIL : ESP_ERR_TIMEOUT;
    }

    s_connected = true;
    return ESP_OK;
}
