/*
* SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#include <stdio.h>
#include <esp_err.h>
#include <esp_console.h>
#include <esp_log.h>
#include "app_webrtc.h"

#define SLAVE_HOOKS_FOR_HOST_DEEP_SLEEP (1)

static const char *TAG = "power_save";

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_pm.h"
#include "esp_event.h"

/* The Wi-Fi power-save helpers apply on the co-processor targets alone. On any
 * other target they would be dead code. */
#if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32C5)
#define PS_WIFI_KNOBS_ENABLED 1
#else
#define PS_WIFI_KNOBS_ENABLED 0
#endif

#if PS_WIFI_KNOBS_ENABLED

static esp_err_t configure_wifi_power_save(void)
{
#if CONFIG_EXAMPLE_WIFI_PS_NONE
    const wifi_ps_type_t mode = WIFI_PS_NONE;
    const char *mode_name = "NONE";
#elif CONFIG_EXAMPLE_WIFI_PS_MAX_MODEM
    const wifi_ps_type_t mode = WIFI_PS_MAX_MODEM;
    const char *mode_name = "MAX_MODEM";
#else
    const wifi_ps_type_t mode = WIFI_PS_MIN_MODEM;
    const char *mode_name = "MIN_MODEM";
#endif

    /* Read back rather than trust the write. Until this function was wired to
     * an event it never ran, and the radio kept the driver default silently.
     * Any power figure is only meaningful beside this line. */
    wifi_ps_type_t before = WIFI_PS_NONE;
    esp_wifi_get_ps(&before);

    esp_err_t ret = esp_wifi_set_ps(mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_ps(%s) failed: %s", mode_name, esp_err_to_name(ret));
        return ret;
    }

    wifi_ps_type_t after = WIFI_PS_NONE;
    esp_wifi_get_ps(&after);
    ESP_LOGI(TAG, "Wi-Fi power save mode: %s (was %d, now %d)",
             mode_name, (int) before, (int) after);

#if CONFIG_EXAMPLE_WIFI_PS_MAX_MODEM
    /* Only MAX_MODEM honours listen_interval. Under MIN_MODEM the radio wakes
     * at every DTIM, so the field is inert.
     *
     * `wifi-set` rebuilds wifi_config_t from zero and restarts the station, so
     * a one-shot write at boot is lost at the first provisioning. Rewriting it
     * on every WIFI_EVENT_STA_START keeps the value in the driver. The driver
     * reads the field when the station associates, and esp_wifi_start() only
     * posts the event, so a write may land after the association it was meant
     * for. The value then applies from the next association. */
    wifi_config_t wifi_config = {};
    ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
    if (ret == ESP_OK) {
        wifi_config.sta.listen_interval = CONFIG_EXAMPLE_WIFI_LISTEN_INTERVAL;
        ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "listen_interval not applied: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Wi-Fi listen interval: %d beacons", CONFIG_EXAMPLE_WIFI_LISTEN_INTERVAL);
#endif

    return ESP_OK;
}

static void on_sta_start(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    configure_wifi_power_save();
}

#endif /* PS_WIFI_KNOBS_ENABLED */

/* The host-sleep integration is a co-processor feature, not a chip property:
 * eh_cp_feat_host_ps is what provides the callbacks and the wake path. */
#if CONFIG_ESP_HOSTED_CP_FEAT_HOST_PS
#define HOST_PS_INTEGRATION_ENABLED 1
#include "host_ps_integration.h"
#include "eh_cp_feat_host_ps_apis.h"
#else
#define HOST_PS_INTEGRATION_ENABLED 0
#endif

/* Milliseconds to wait for the host to come back up. eh_cp_feat_host_ps caps
 * this internally; the value only bounds how long the caller blocks. */
#define HOST_WAKEUP_TIMEOUT_MS 2000

#if SLAVE_HOOKS_FOR_HOST_DEEP_SLEEP && HOST_PS_INTEGRATION_ENABLED

/* The host-sleep callbacks live in host_ps_integration.c, vendored from
 * esp_hosted's power_save example: they stop the CLI and enter CP light sleep
 * when the host sleeps, and reverse it when the host wakes. */

static void app_webrtc_event_handler(app_webrtc_event_data_t *event_data, void *user_ctx)
{
    if (event_data == NULL) {
        return;
    }

    switch (event_data->event_id) {
    /* An offer already wakes the host: the signaling bridge adapter calls
     * eh_cp_feat_host_ps_wakeup_host() before it forwards one. Media start
     * wakes it again, in case it slept in between; on an awake host this
     * returns at once. */
    case APP_WEBRTC_EVENT_STREAMING_STARTED:
        ESP_LOGI(TAG, "Streaming started, waking up host");
        eh_cp_feat_host_ps_wakeup_host(HOST_WAKEUP_TIMEOUT_MS);
        break;
    default:
        break;
    }
}
#endif /* SLAVE_HOOKS_FOR_HOST_DEEP_SLEEP && HOST_PS_INTEGRATION_ENABLED */

esp_err_t power_save_init(void)
{
    esp_err_t ret = ESP_OK;

#if PS_WIFI_KNOBS_ENABLED
    /* app_main() calls this after app_network_init() and before provisioning
     * starts Wi-Fi. Register for the start event and apply the power-save
     * mode there, at every start. A failure
     * costs power, not function, so it must not bring the co-processor down:
     * a reboot here takes the host's wake path with it. */
    esp_err_t evt_ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START,
                        &on_sta_start, NULL);
    if (evt_ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi power save will stay at the driver default: %s",
                 esp_err_to_name(evt_ret));
    }
#endif

#if SLAVE_HOOKS_FOR_HOST_DEEP_SLEEP && HOST_PS_INTEGRATION_ENABLED
    ESP_LOGI(TAG, "Initializing power save API");

    ret = host_ps_integration_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Host power save integration failed: %s", esp_err_to_name(ret));
    }

#else /* SLAVE_HOOKS_FOR_HOST_DEEP_SLEEP && HOST_PS_INTEGRATION_ENABLED */
    /* esp_hosted's host_power_save API is C5/C6-only (it's the slave-side
     * mechanism that lets the co-processor sleep until the host wakes it
     * over SDIO). On other targets there's no host to coordinate with,
     * so skip silently. */
    ESP_LOGI(TAG, "Slave-side host power save hooks not applicable on this target");
#endif

    return ret;
}

esp_err_t power_save_register_webrtc_events(void)
{
#if SLAVE_HOOKS_FOR_HOST_DEEP_SLEEP && HOST_PS_INTEGRATION_ENABLED
    if (app_webrtc_register_event_callback(app_webrtc_event_handler, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to register WebRTC event callback for power save");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Registered WebRTC event callback for host wakeup");
#endif
    return ESP_OK;
}
