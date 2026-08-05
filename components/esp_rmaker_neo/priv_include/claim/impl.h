/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file impl.h
 * @brief Assisted claiming.
 *
 * Assisted claiming lets a node without pre-flashed cloud credentials obtain them at
 * first setup. The node generates a private key and a Certificate Signing Request, and
 * the phone app relays them to the ESP RainMaker claiming service over the BLE
 * provisioning session, returning a certificate and a cloud-assigned node ID. Those are
 * written to the factory partition under the same NVS keys the host-side factory tooling
 * would have used, so the rest of the SDK reads credentials exactly as before.
 *
 * The device is a pure responder: the phone app drives the exchange over the
 * `rmaker_claim` provisioning endpoint. See docs/en/specs/provisioning.md for the protocol.
 */

#ifndef __CLAIM_IMPL_H__
#define __CLAIM_IMPL_H__

/* Includes *******************************************************/

/* Standard includes */
#include <stdint.h>
#include <stddef.h>

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Claiming decision logic, which owns ::esp_rmaker_claim_state_t */
#include "claim/rules.h"

/* Crypto includes, for the key type and its CSR buffer bound */
#include "util/esp_rmaker_crypto.h"

/* Preprocessor definitions *******************************************************/

/** The key type claiming generates, and the CSR buffer that key needs.
 *
 * ECDSA P-256 is not configurable: the ESP RainMaker claiming service only accepts ECDSA CSRs.
 *
 * ::MAX_CSR_SIZE covers the escaped form too, since the buffer holds the CSR before and after
 * newline escaping. Neither value is a protocol constant; nothing on the wire depends on them.
 */
#define CLAIM_KEY_TYPE   RMAKER_CRYPTO_KEY_TYPE_ECDSA_P256
#define MAX_CSR_SIZE     RMAKER_CRYPTO_CSR_PEM_LEN_ECDSA_P256

/** Maximum size of a claiming payload.
 *
 * One buffer is time-multiplexed across the whole exchange: the private-key PEM, then
 * the claim-init request, the init response, the verify request and finally the verify
 * response. This is therefore also the hard cap on the certificate size.
 */
#define MAX_PAYLOAD_SIZE 3072

/* Types *******************************************************/


/**
 * @brief Claiming context.
 *
 * Allocated in external RAM when available. Unlike esp-rainmaker, the private key is
 * held as PEM rather than an mbedtls_pk_context, so this module needs no mbedTLS types:
 * all crypto goes through the esp_rmaker_crypto_* helpers in rmng-common.
 */
typedef struct {
    esp_rmaker_claim_state_t state;
    unsigned char csr[MAX_CSR_SIZE];   /**< PEM CSR, newlines escaped for JSON. */
    char payload[MAX_PAYLOAD_SIZE];    /**< Time-multiplexed request/response buffer. */
    size_t payload_offset;             /**< Fragmentation cursor into `payload`. */
    size_t payload_len;                /**< Valid length of `payload`. */
    uint32_t payload_total;            /**< Total an inbound response declared at offset 0. */
    uint8_t *key_pem;                  /**< PEM private key. Length includes the NUL. */
    size_t key_pem_len;
} esp_rmaker_claim_data_t;

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize assisted claiming.
 *
 * Loads or generates the private key (in a dedicated low-priority task, since key
 * generation is slow enough to trip the task watchdog otherwise), ensures the
 * general-purpose random bytes exist, and registers the `rmaker_claim` provisioning
 * endpoint so the phone app can drive claiming once provisioning starts.
 *
 * Must be called before provisioning is started, and only when the node is not already
 * claimed. Blocks until key generation completes.
 *
 * @return The claiming context on success, NULL on failure. Pass it to
 *         ::esp_rmaker_assisted_claim_perform, which takes ownership.
 */
esp_rmaker_claim_data_t *esp_rmaker_assisted_claim_init(void);

/**
 * @brief Wait for assisted claiming to finish, then clean up.
 *
 * Blocks until the phone app completes or aborts claiming, or until the provisioning session
 * ends. There is no timeout: the session end is the bound. It has to be, because this runs
 * after provisioning, where the `rmaker_claim` endpoint is already gone -- an app that
 * started claiming and never finished it can send neither a certificate nor an abort, so
 * without that signal the calling task would block for good.
 *
 * Frees @p claim_data and unregisters the endpoint handlers in all cases.
 *
 * @param[in] claim_data The context returned by ::esp_rmaker_assisted_claim_init.
 * @return ESP_RMAKER_OK if claiming succeeded and credentials were persisted.
 * @return ESP_RMAKER_INVALID_STATE if claiming never started, e.g. the node was
 *         provisioned over a transport the phone app cannot claim over.
 * @return ESP_RMAKER_FAIL if claiming was attempted but did not complete, including on
 *         timeout, in which case ::RMAKER_EVENT_CLAIM_FAILED is posted.
 */
esp_rmaker_error_t esp_rmaker_assisted_claim_perform(esp_rmaker_claim_data_t *claim_data);

/**
 * @brief Free a claiming context.
 *
 * @param[in] claim_data The context to free. NULL is tolerated.
 */
void esp_rmaker_claim_data_free(esp_rmaker_claim_data_t *claim_data);

#ifdef __cplusplus
}
#endif

#endif /* __CLAIM_IMPL_H__ */
