/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_network_neo.h
 * @brief Network bring-up for examples, on both platforms.
 *
 * One surface the example body can call unconditionally:
 *  - ESP-IDF: Wi-Fi/Thread provisioning, via the espressif/rmaker_app_network
 *    component, wrapped around the RMNG pre-provisioning flow.
 *  - POSIX:   there is no provisioning and no credentials, so the calls are
 *             successful no-ops.
 *
 * ::app_network_provision collapses the ESP four-step sequence
 * (pre_prov_init -> set_custom_mfg_data -> start_neo -> pre_prov_deinit) into a
 * single call.
 */

#pragma once

/* Standard includes */
#include <stdint.h>

/* Platform includes */
#include "osal_err.h"

#ifdef ESP_PLATFORM
/* espressif/rmaker_app_network: brings app_network_init() and the
 * MFG_DATA_DEVICE_TYPE_* / MFG_DATA_DEVICE_SUBTYPE_* macros reused below. */
#include "app_network.h"
#endif /* ESP_PLATFORM */

/* --- Supported device types and subtypes --- */
/* ESP RainMaker types and subtypes are reused whenever possible. On POSIX there is no
 * BLE advertisement to carry them, so they exist only to keep the example source
 * portable. */

#ifdef ESP_PLATFORM
/* Light */
#define NEO_MFG_DATA_DEVICE_TYPE_LIGHT               (MFG_DATA_DEVICE_TYPE_LIGHT)
#define NEO_MFG_DATA_DEVICE_SUBTYPE_LIGHT            (MFG_DATA_DEVICE_SUBTYPE_LIGHT)
/* Switch */
#define NEO_MFG_DATA_DEVICE_TYPE_SWITCH              (MFG_DATA_DEVICE_TYPE_SWITCH)
#define NEO_MFG_DATA_DEVICE_SUBTYPE_SWITCH           (MFG_DATA_DEVICE_SUBTYPE_SWITCH)
#else
#define NEO_MFG_DATA_DEVICE_TYPE_LIGHT               (0)
#define NEO_MFG_DATA_DEVICE_SUBTYPE_LIGHT            (0)
#define NEO_MFG_DATA_DEVICE_TYPE_SWITCH              (0)
#define NEO_MFG_DATA_DEVICE_SUBTYPE_SWITCH           (0)
#endif /* ESP_PLATFORM */

/* Sentinel device type: pass this to app_network_provision() to skip setting custom
 * manufacturer data entirely (no BLE advertisement filter), matching the bring-up of
 * examples that never advertised a device type/subtype. */
#define NEO_MFG_DATA_DEVICE_TYPE_NONE    (0xFFFF)
#define NEO_MFG_DATA_DEVICE_SUBTYPE_NONE (0xFF)

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ESP_PLATFORM
/**
 * @brief Initialise the network stack.
 *
 * No-op on POSIX. On ESP-IDF this comes from espressif/rmaker_app_network.
 */
void app_network_init(void);
#endif /* !ESP_PLATFORM */

#ifdef ESP_PLATFORM
/**
 * @brief Set the custom manufacturer data for RMNG.
 *
 * Adds custom manufacturing data to the BLE advertisements sent during provisioning, so
 * a phone app can filter the scanned devices down to the relevant one.
 *
 * ESP-IDF only: there is no BLE advertisement on POSIX. Prefer ::app_network_provision,
 * which calls this as part of the bring-up sequence.
 *
 * @param[in] device_type The device type.
 * @param[in] device_subtype The device subtype.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t app_network_set_custom_mfg_data_neo(uint16_t device_type, uint8_t device_subtype);

/**
 * @brief Start Wi-Fi/Thread provisioning for RMNG, using the configured PoP setting.
 *
 * ESP-IDF only. Prefer ::app_network_provision, which wraps this together with the
 * RMNG pre-provisioning lifecycle.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t app_network_start_neo(void);
#endif /* ESP_PLATFORM */

/**
 * @brief Reset the network credentials.
 *
 * No-op returning OSAL_ERR_OK on POSIX, where there are no credentials to clear.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t app_network_reset_credentials(void);

/**
 * @brief Run the full network provisioning bring-up.
 *
 * ESP-IDF: esp_rmaker_pre_prov_init() -> app_network_set_custom_mfg_data_neo() ->
 * app_network_start_neo() -> esp_rmaker_pre_prov_deinit(). Passing
 * ::NEO_MFG_DATA_DEVICE_TYPE_NONE skips the manufacturer-data step.
 * POSIX: no-op returning OSAL_ERR_OK.
 *
 * @param[in] device_type    RMNG manufacturer-data device type, or
 *                           ::NEO_MFG_DATA_DEVICE_TYPE_NONE to not set any.
 * @param[in] device_subtype RMNG manufacturer-data device subtype.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t app_network_provision(uint16_t device_type, uint8_t device_subtype);

/**
 * @brief Optional provisioning-lifecycle callbacks.
 *
 * An example may register these to drive user feedback (e.g. an LED animation) around
 * the otherwise-opaque ::app_network_provision call, without having to touch any
 * ESP-only provisioning event. All fields are optional (NULL to ignore). The callbacks
 * fire only on ESP; POSIX has no provisioning, so the setter is a no-op there and
 * nothing is ever invoked.
 */
typedef struct {
    void (*on_begin)(void);          /*!< Bring-up started, before the network starts. */
    void (*on_session_start)(void);  /*!< Provisioning session became active (app connected). */
    void (*on_end)(void);            /*!< Provisioning finished (success or failure). */
} app_network_prov_hooks_t;

/**
 * @brief Register provisioning-lifecycle callbacks (see ::app_network_prov_hooks_t).
 *
 * @param[in] hooks Callbacks to copy, or NULL to clear. The struct is copied; the
 *                  caller need not keep it alive.
 */
void app_network_set_prov_hooks(const app_network_prov_hooks_t *hooks);

#ifdef __cplusplus
}
#endif
