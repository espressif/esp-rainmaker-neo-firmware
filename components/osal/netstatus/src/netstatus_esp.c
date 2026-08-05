/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file netstatus_esp.c
 * @brief ESP-IDF implementation of the one-shot network-connectivity trapdoor.
 *
 * One-shot network wait: one connectivity bit per enabled network
 * type (Wi-Fi STA, Thread, Ethernet), each guarded by the underlying ESP-IDF option
 * (CONFIG_ESP_WIFI_ENABLED / CONFIG_OPENTHREAD_ENABLED / CONFIG_ETH_ENABLED) and served
 * by its own static, Kconfig-guarded event handler. arm() registers the relevant
 * handlers and latches any interface already connected; trap() blocks until any one
 * network is connected (whichever first), then disarms.
 */

#include "osal_netstatus.h"

#include <stdbool.h>
#include <stddef.h>

#include "osal_event_group.h"
#include "osal_ticks.h"
#include "osal_log.h"

#include <esp_event.h>
#include <esp_bit_defs.h>

#include "sdkconfig.h"
#define WIFI_ENABLED (CONFIG_ESP_WIFI_ENABLED || CONFIG_ESP32_WIFI_ENABLED || CONFIG_ESP_WIFI_REMOTE_ENABLED)
#define ETH_ENABLED (CONFIG_ETH_ENABLED)
#define THREAD_ENABLED (CONFIG_OPENTHREAD_ENABLED)

#if WIFI_ENABLED || ETH_ENABLED
#include <esp_netif.h>
#endif
#if WIFI_ENABLED
#include <esp_wifi.h>
#endif
#if THREAD_ENABLED
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <esp_openthread_dns64.h>
#endif

static const char *TAG = "osal_netstatus";

/* One connectivity bit per network type; set independently so multiple networks are
 * supported simultaneously. */
#if WIFI_ENABLED
static const osal_event_group_bits_t WIFI_CONNECTED_BIT = BIT0;
#endif
#if THREAD_ENABLED
static const osal_event_group_bits_t THREAD_CONNECTED_BIT = BIT1;
#endif
#if ETH_ENABLED
static const osal_event_group_bits_t ETH_CONNECTED_BIT = BIT2;
#endif

static osal_event_group_handle_t s_group;
static osal_event_group_bits_t   s_wait_bits;        /* pending (not yet connected) */
static bool                                  s_already_connected;

#if WIFI_ENABLED || ETH_ENABLED
static bool rmaker_has_ipv4(const char *ifkey)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(ifkey);
    if (!netif) {
        return false;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        return false;
    }
    return ip_info.ip.addr != 0;
}
#endif

/* Static, Kconfig-guarded event handlers - one per network type. */
#if WIFI_ENABLED
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && s_group) {
        osal_event_group_set_bits(s_group, WIFI_CONNECTED_BIT);
    }
}
#endif
#if THREAD_ENABLED
static void thread_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == OPENTHREAD_EVENT && id == OPENTHREAD_EVENT_SET_DNS_SERVER && s_group) {
        osal_event_group_set_bits(s_group, THREAD_CONNECTED_BIT);
    }
}
#endif
#if ETH_ENABLED
static void eth_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP && s_group) {
        osal_event_group_set_bits(s_group, ETH_CONNECTED_BIT);
    }
}
#endif

/* Unregister whatever was registered and release the event group. Safe to call after a
 * partial arm (unregistering an unregistered handler is a no-op). */
static void disarm(void)
{
#if WIFI_ENABLED
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
#endif
#if THREAD_ENABLED
    esp_event_handler_unregister(OPENTHREAD_EVENT, OPENTHREAD_EVENT_SET_DNS_SERVER, &thread_event_handler);
#endif
#if ETH_ENABLED
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler);
#endif
    if (s_group) {
        osal_event_group_delete(s_group);
        s_group = NULL;
    }
}

int osal_netstatus_arm(void)
{
    if (s_group) {
        return 0; /* already armed; idempotent */
    }

    s_group = osal_event_group_create();
    if (!s_group) {
        OSAL_LOGE(TAG, "Failed to create event group");
        return -1;
    }
    s_wait_bits = 0;
    s_already_connected = false;

#if WIFI_ENABLED
    if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL) != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to register Wi-Fi event handler");
        goto fail;
    }
    {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK && rmaker_has_ipv4("WIFI_STA_DEF")) {
            s_already_connected = true;
            osal_event_group_set_bits(s_group, WIFI_CONNECTED_BIT);
        } else {
            s_wait_bits |= WIFI_CONNECTED_BIT;
        }
    }
#endif
#if THREAD_ENABLED
    if (esp_event_handler_register(OPENTHREAD_EVENT, OPENTHREAD_EVENT_SET_DNS_SERVER, &thread_event_handler, NULL) != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to register Thread event handler");
        goto fail;
    }
    {
        ip6_addr_t nat64_prefix;
        esp_openthread_lock_acquire(portMAX_DELAY);
        esp_err_t ot_err = esp_openthread_get_nat64_prefix(&nat64_prefix);
        esp_openthread_lock_release();
        if (ot_err == ESP_OK) {
            s_already_connected = true;
            osal_event_group_set_bits(s_group, THREAD_CONNECTED_BIT);
        } else {
            s_wait_bits |= THREAD_CONNECTED_BIT;
        }
    }
#endif
#if ETH_ENABLED
    if (esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler, NULL) != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to register Ethernet event handler");
        goto fail;
    }
    if (rmaker_has_ipv4("ETH_DEF")) {
        s_already_connected = true;
        osal_event_group_set_bits(s_group, ETH_CONNECTED_BIT);
    } else {
        s_wait_bits |= ETH_CONNECTED_BIT;
    }
#endif

    OSAL_LOGI(TAG, "Armed network-connectivity trapdoor");
    return 0;

fail:
    disarm();
    return -1;
}

int osal_netstatus_trap(void)
{
    if (!s_group) {
        OSAL_LOGW(TAG, "osal_netstatus_trap() called without osal_netstatus_arm()");
        return -1;
    }

    /* Wait for any one enabled network to connect (whichever first). Skip if some
     * interface was already connected at arm time. */
    if (!s_already_connected && s_wait_bits != 0) {
        OSAL_LOGI(TAG, "Waiting for network connectivity...");
        osal_event_group_wait_bits(s_group, s_wait_bits,
                                   false /* clear_on_exit */,
                                   false /* wait_for_all_bits: any network */,
                                   OSAL_MAX_DELAY);
    }
    OSAL_LOGI(TAG, "Network connected");

    disarm();
    return 0;
}
