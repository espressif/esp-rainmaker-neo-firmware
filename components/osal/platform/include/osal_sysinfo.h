/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_sysinfo.h
 * @brief Platform common system information.
 */

#ifndef __OSAL_SYSINFO_H__
#define __OSAL_SYSINFO_H__

/* Includes **************************************************************/

/* Standard C headers */
#include <stdint.h>
#include <stddef.h>

/* Error codes */
#include "osal_err.h"

/* Preprocessor definitions **********************************************/

/** Length of a MAC address, in bytes. */
#define OSAL_MAC_ADDR_LEN 6

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the firmware version string.
 *
 * @return The firmware version.
 */
const char *osal_sysinfo_get_fw_version(void);

/**
 * @brief Get the project name string.
 *
 * @return The project name.
 */
const char *osal_sysinfo_get_project_name(void);

/**
 * @brief Get the platform the firmware is running on.
 *
 * The chip target on ESP-IDF (`CONFIG_IDF_TARGET`, e.g. "esp32c3"), and "posix" on a host.
 * Reported to the cloud, so treat the values as part of that contract rather than free text.
 *
 * @return The platform name. Never NULL.
 */
const char *osal_sysinfo_get_platform_name(void);

/**
 * @brief Get the device's base MAC address.
 *
 * The base address the platform derives its per-interface addresses from, so it identifies
 * the device regardless of which radio it ends up using. On ESP-IDF this is `ESP_MAC_BASE`,
 * covering both Wi-Fi and Thread nodes.
 *
 * A host has no such address, so the POSIX implementation substitutes the lowest-numbered
 * non-loopback interface reporting a non-zero link-layer address. That is stable across runs,
 * but on a machine with bridges, tunnels or container interfaces it need not be the primary
 * NIC - treat it as a host identity for tooling and simulation, not as an authoritative
 * device identifier.
 *
 * @param[out] mac     Buffer to receive the address, most significant byte first.
 * @param[in]  mac_len Size of @p mac. Must be at least ::OSAL_MAC_ADDR_LEN.
 * @return OSAL_ERR_OK on success.
 * @return OSAL_ERR_INVALID_ARG if @p mac is NULL or @p mac_len is too small.
 * @return OSAL_ERR_NOT_FOUND if the platform has no MAC address to report.
 * @return OSAL_ERR_FAIL if the address could not be read.
 */
osal_err_t osal_sysinfo_get_base_mac(uint8_t *mac, size_t mac_len);

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_SYSINFO_H__ */
