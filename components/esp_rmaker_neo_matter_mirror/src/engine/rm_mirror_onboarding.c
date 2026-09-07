/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_onboarding.c
 * @brief Onboarding readiness gate: tracks what still has to happen before the
 *        RainMaker agent can start, and re-opens the commissioning window when
 *        the node loses its last fabric.
 *
 * The gate deals in facts, not ecosystems: whether the network is up, whether
 * commissioning still has to bring it up, and whether CHIPoBLE still holds the
 * memory the RainMaker agent needs. The port samples those facts once the Matter
 * stack is up and then feeds the transitions in from Matter events.
 */

#include <string.h>

#include "rm_mirror_engine.h"

#include "osal_log.h"
#include "osal_semaphore.h"
#include "osal_ticks.h"

static const char *TAG = "rm_mirror_onboarding";

static struct {
    const rm_mirror_port_ops_t *port;
    osal_semaphore_handle_t lock;
    osal_semaphore_handle_t ready_sem; /* given once, then the ready flag serves later waiters */
    bool initialized;
    bool ready;
    /* What is still outstanding, cleared as the events arrive */
    bool need_network;
    bool need_commissioning;
    bool need_ble_teardown;
    bool reopen_window;
    uint16_t window_timeout_s;
} __onboarding;

/**
 * @brief Mark the gate open if nothing is outstanding. Called with the lock held;
 *        returns true when this call is the one that opened it.
 */
static bool __settle_locked(void)
{
    if (__onboarding.ready || __onboarding.need_network || __onboarding.need_commissioning
            || __onboarding.need_ble_teardown) {
        return false;
    }
    __onboarding.ready = true;
    return true;
}

/* Events between the port's sample and initialized are dropped; the live facts cover them. */
static void __resample_locked(void)
{
    rm_mirror_onboarding_state_t live = { 0 };
    if (!__onboarding.port->onboarding_state_get
            || __onboarding.port->onboarding_state_get(&live) != ESP_RMAKER_OK) {
        return;
    }
    if (live.network_up) {
        __onboarding.need_network = false;
    }
    if (live.commissioned) {
        __onboarding.need_commissioning = false;
    }
    if (live.ble_reclaimed) {
        __onboarding.need_ble_teardown = false;
    }
}

static void __clear(bool *flag, const char *what)
{
    bool opened = false;
    osal_semaphore_take(__onboarding.lock, OSAL_MAX_DELAY);
    if (*flag) {
        *flag = false;
        OSAL_LOGI(TAG, "Onboarding: %s.", what);
    }
    opened = __settle_locked();
    osal_semaphore_give(__onboarding.lock);
    if (opened) {
        OSAL_LOGI(TAG, "Onboarding complete; the node is ready for the RainMaker agent.");
        osal_semaphore_give(__onboarding.ready_sem);
    }
}

esp_rmaker_error_t rm_mirror_onboarding_init(const rm_mirror_port_ops_t *port,
        const rm_mirror_onboarding_state_t *state, bool reopen_window, uint16_t window_timeout_s)
{
    if (!port || !state) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (__onboarding.initialized) {
        return ESP_RMAKER_INVALID_STATE;
    }

    memset(&__onboarding, 0, sizeof(__onboarding));
    __onboarding.lock = osal_semaphore_create_mutex();
    __onboarding.ready_sem = osal_semaphore_create_binary();
    if (!__onboarding.lock || !__onboarding.ready_sem) {
        rm_mirror_onboarding_deinit();
        return ESP_RMAKER_NO_MEM;
    }
    __onboarding.port = port;
    __onboarding.reopen_window = reopen_window;
    __onboarding.window_timeout_s = window_timeout_s;
    __onboarding.need_network = !state->network_up;
    /* An already-commissioned node has its network credentials and never re-runs commissioning. */
    __onboarding.need_commissioning = state->wait_for_commissioning && !state->commissioned;
    __onboarding.need_ble_teardown = state->ble_teardown_expected;
    __onboarding.initialized = true;

    osal_semaphore_take(__onboarding.lock, OSAL_MAX_DELAY);
    __resample_locked();
    bool opened = __settle_locked();
    osal_semaphore_give(__onboarding.lock);
    if (opened) {
        osal_semaphore_give(__onboarding.ready_sem);
    } else {
        OSAL_LOGI(TAG, "Onboarding pending: network=%d commissioning=%d ble-teardown=%d",
                  (int)__onboarding.need_network, (int)__onboarding.need_commissioning,
                  (int)__onboarding.need_ble_teardown);
    }
    return ESP_RMAKER_OK;
}

void rm_mirror_onboarding_deinit(void)
{
    if (__onboarding.lock) {
        osal_semaphore_delete(__onboarding.lock);
    }
    if (__onboarding.ready_sem) {
        osal_semaphore_delete(__onboarding.ready_sem);
    }
    memset(&__onboarding, 0, sizeof(__onboarding));
}

bool rm_mirror_onboarding_ready(void)
{
    return __onboarding.initialized && __onboarding.ready;
}

esp_rmaker_error_t rm_mirror_onboarding_wait(uint32_t timeout_ms)
{
    if (!__onboarding.initialized) {
        return ESP_RMAKER_INVALID_STATE;
    }
    if (__onboarding.ready) {
        return ESP_RMAKER_OK;
    }
    if (osal_semaphore_take(__onboarding.ready_sem, osal_ticks_from_ms(timeout_ms)) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Onboarding did not complete within %u ms: network=%d commissioning=%d "
                  "ble-teardown=%d", (unsigned)timeout_ms, (int)__onboarding.need_network,
                  (int)__onboarding.need_commissioning, (int)__onboarding.need_ble_teardown);
        return ESP_RMAKER_TIMEOUT;
    }
    /* Hand the token straight back: the gate stays open for every later caller. */
    osal_semaphore_give(__onboarding.ready_sem);
    return ESP_RMAKER_OK;
}

/* Inbound entry points (called by the port) ***********************************/

void rm_mirror_engine_handle_network_up(void)
{
    if (!__onboarding.initialized) {
        return;
    }
    __clear(&__onboarding.need_network, "the network is up");
}

void rm_mirror_engine_handle_commissioning_complete(void)
{
    if (!__onboarding.initialized) {
        return;
    }
    __clear(&__onboarding.need_commissioning, "Matter commissioning is complete");
}

void rm_mirror_engine_handle_ble_reclaimed(void)
{
    if (!__onboarding.initialized) {
        return;
    }
    __clear(&__onboarding.need_ble_teardown, "the BLE memory has been reclaimed");
}

void rm_mirror_engine_handle_fabric_removed(size_t remaining)
{
    if (!__onboarding.initialized || remaining > 0) {
        return;
    }
    if (!__onboarding.reopen_window) {
        OSAL_LOGW(TAG, "Last fabric removed; the window stays closed until a reboot or factory reset.");
        return;
    }
    if (!__onboarding.port->commissioning_window_open) {
        return;
    }
    OSAL_LOGI(TAG, "Last fabric removed; re-opening the commissioning window for %u s.",
              (unsigned)__onboarding.window_timeout_s);
    if (__onboarding.port->commissioning_window_open(__onboarding.window_timeout_s) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to re-open the commissioning window.");
    }
}
