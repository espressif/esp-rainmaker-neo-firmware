/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/* AWS IoT Credential Provider client for RainMaker Neo camera devices.
 *
 * Exchanges the node's X.509 client certificate for temporary credentials
 * scoped to the node's own KVS signaling channel, through the rmaker_aws_sign
 * component. The endpoint, role alias and region come from the deployment's
 * server config.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_rmaker_aws_sign.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How long each bring-up wait for the server config lasts before it logs and waits again. */
#define RMAKER_CAMERA_SERVER_CONFIG_WAIT_MS 10000

/**
 * @brief Take the credential-provider endpoint, video role alias and region from the server config
 *
 * Waits up to @p wait_ms for the getServerConfig response. Once taken, the values are final.
 * Do not call it with a wait from the SDK work queue.
 *
 * @return ESP_OK when the values are set
 * @return ESP_ERR_TIMEOUT when the server config has not arrived yet
 * @return ESP_ERR_NOT_FOUND when the server config does not carry one of the values
 */
esp_err_t rmaker_camera_resolve_deployment(uint32_t wait_ms);

/**
 * @brief The video role alias (NULL before rmaker_camera_resolve_deployment() succeeds)
 */
const char *rmaker_camera_get_role_alias(void);

/**
 * @brief The deployment's AWS region (NULL before rmaker_camera_resolve_deployment() succeeds)
 */
const char *rmaker_camera_get_aws_region(void);

/**
 * @brief Get current credentials for the video role alias
 *
 * Fetches them from https://<endpoint>/role-aliases/<role_alias>/credentials
 * with the node certificate and key, when they are missing or near expiry.
 * Otherwise returns the ones already fetched. Blocking. Needs a 6-8 KB stack in
 * internal RAM. Call it from one task at a time.
 *
 * @param[out] creds The credentials. Valid until the next call.
 * @return ESP_OK, ESP_ERR_INVALID_STATE before the deployment is resolved, or an error from rmaker_aws_sign
 */
esp_err_t rmaker_camera_refresh_credentials(const esp_rmaker_aws_sign_creds_t **creds);

/**
 * @brief Start the task that runs the credential fetches
 *
 * Call it once before signaling starts. The task has an 8 KB stack in internal RAM.
 *
 * @return ESP_OK, or ESP_FAIL if the task or its semaphores cannot be created
 */
esp_err_t rmaker_camera_credentials_init(void);

/**
 * @brief Credentials-fetch callback in the shape kvs_signaling expects
 *
 * Hands the fetch to the credentials task and waits for it.
 */
int rmaker_fetch_aws_credentials(uint64_t user_data,
                                 const char **pAK, uint32_t *pAKLen,
                                 const char **pSK, uint32_t *pSKLen,
                                 const char **pTok, uint32_t *pTokLen,
                                 uint32_t *pExp);

#ifdef __cplusplus
}
#endif
