/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file local_ctrl/sec2_cache.h
 * @brief SRP6a salt/verifier resolution for the local endpoints service (security 2)
 *
 * Internal. Declared here rather than kept static so the caching rule can be unit-tested
 * without starting the service, which would need a protocomm HTTP server and a running
 * mDNS daemon.
 */

#ifndef __LOCAL_CTRL_SEC2_CACHE_H__
#define __LOCAL_CTRL_SEC2_CACHE_H__

#include <stdbool.h>

#include "esp_rmaker_error_types.h"
#include "sdkconfig.h"

#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resolve the SRP6a salt and verifier for a PoP, using the NVS cache when it is safe.
 *
 * Generating a salt/verifier pair is expensive, so it is cached in NVS. The cache is only
 * valid for the PoP that is stored in NVS alongside it, which drives both decisions here:
 *
 * - Read it only when @p pop_from_nvs, i.e. when the PoP was not regenerated this run and
 *   therefore still matches whatever the cache was derived from.
 * - Write it only when @p pop_is_custom is false. A custom PoP is never stored in NVS
 *   (`esp_rmaker_local_ctrl_set_pop()` keeps it in RAM), so caching a pair derived from it
 *   would leave NVS holding one PoP and a verifier for another - and the next start without
 *   a custom PoP would reuse that pair against the stored PoP and fail every handshake.
 *
 * @param[in]  pop              PoP to derive from (the SRP6a password). Must not be NULL.
 * @param[in]  pop_from_nvs     True if @p pop was read from NVS rather than generated this run.
 * @param[in]  pop_is_custom    True if @p pop came from esp_rmaker_local_ctrl_set_pop().
 * @param[out] out_salt         Salt buffer, caller frees. Always LOCAL_CTRL_SEC2_SALT_LEN bytes.
 * @param[out] out_verifier     Verifier buffer, caller frees.
 * @param[out] out_verifier_len Verifier length.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code (nothing is allocated on failure).
 */
esp_rmaker_error_t esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
    const char *pop, bool pop_from_nvs, bool pop_is_custom,
    char **out_salt, char **out_verifier, int *out_verifier_len);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2 */

#endif /* __LOCAL_CTRL_SEC2_CACHE_H__ */
