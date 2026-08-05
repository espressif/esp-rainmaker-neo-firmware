/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file core_internal.h
 * @brief Internal core variables and functions.
 */

#ifndef __CORE_INTERNAL_H__
#define __CORE_INTERNAL_H__

/* Standard C headers */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Error types */
#include "esp_rmaker_error_types.h"

/* Structure definitions *******************************************************/

/**
 * @brief State of the ESP RainMaker Neo SDK.
 */
typedef enum {
    /** Deinitialized */
    ESP_RMAKER_STATE_DEINIT = 0,
    /** Initialized */
    ESP_RMAKER_STATE_INIT_DONE,
    /** Starting */
    ESP_RMAKER_STATE_STARTING,
    /** Started */
    ESP_RMAKER_STATE_STARTED,
    /** Stop requested */
    ESP_RMAKER_STATE_STOP_REQUESTED,
    /** Stopped */
    ESP_RMAKER_STATE_STOPPED,
    /** Error */
    ESP_RMAKER_STATE_ERROR,
} esp_rmaker_state_t;

/**
 * @brief Subscribed flags.
 */
typedef enum {
    /** Subscribed to parameters */
    ESP_RMAKER_SUB_PARAMS = (1 << 0),
    /** Subscribed to cloud */
    ESP_RMAKER_SUB_CLOUD = (1 << 1),
    /** Subscribed to all */
    ESP_RMAKER_SUB_ALL = ESP_RMAKER_SUB_PARAMS | ESP_RMAKER_SUB_CLOUD,
} esp_rmaker_sub_flags_t;

/* Function declarations ********************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Signal a successful subscription to parameters.
 */
void esp_rmaker_core_subscribed_to_params(void);

/**
 * @brief Signal an unsubscription from parameters (either disconnect or deliberate).
 */
void esp_rmaker_core_unsubscribed_from_params(void);

/**
 * @brief Check if the device is subscribed to parameters, i.e,. if the signal is set.
 */
bool esp_rmaker_core_is_subscribed_to_params(void);

/**
 * @brief Signal a successful subscription to cloud.
 */
void esp_rmaker_core_subscribed_to_cloud(void);

/**
 * @brief Signal an unsubscription from cloud (either disconnect or deliberate).
 */
void esp_rmaker_core_unsubscribed_from_cloud(void);

/**
 * @brief Check if the device is subscribed to cloud, i.e,. if the signal is set.
 */
bool esp_rmaker_core_is_subscribed_to_cloud(void);
/**
 * @brief Kick the shared node-config retry context to drain its pending
 *        list immediately (rather than waiting for the next retry tick).
 *
 * Called by paths that want a freshly-changed node config to publish
 * promptly. Safe to call from any task.
 */
void esp_rmaker_core_kick_node_config_retry(void);

/**
 * @brief Sign a challenge using the private key.
 * @param[in] challenge The challenge to sign.
 * @param[in] challenge_len The length of the challenge.
 * @param[out] signature Pointer to the signature of the challenge.
 * @param[out] signature_len Pointer to store the actual length of the signature.
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG on invalid args, ESP_RMAKER_FAIL otherwise.
 */
esp_rmaker_error_t esp_rmaker_core_sign_challenge(const uint8_t *challenge, size_t challenge_len, uint8_t **signature, size_t *signature_len);

#ifdef __cplusplus
}
#endif

#endif /* __CORE_INTERNAL_H__ */
