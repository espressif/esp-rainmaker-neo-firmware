/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rules.h
 * @brief Decision logic for assisted claiming, separated from the transport.
 *
 * These are the parts of claiming that are pure functions of their arguments: which command
 * is legal in which state, whether an inbound fragment fits the reassembly buffer, and the
 * newline escaping the JSON payloads need. They hold the bounds checks and the state-machine
 * guards, which is where claiming's sharp edges are.
 *
 * Kept out of claim.c, and compiled on every platform, so the unit tests link the same object
 * the firmware does. claim.c itself is BLE-only and therefore ESP-only; testing through it
 * would mean either a platform-gated test target or including the source into a test
 * translation unit, both of which break as soon as claim.c becomes portable.
 */

#ifndef __CLAIM_RULES_H__
#define __CLAIM_RULES_H__

/* Includes *******************************************************/

/* Standard includes */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Protobuf includes, for the wire status and message types */
#include "claim/pb-c.h"

/* Types *******************************************************/

/**
 * @brief Claiming state.
 *
 * Advances monotonically through a successful claim, except that an abort from the phone
 * app resets it to ::RMAKER_CLAIM_STATE_PK_GENERATED so claiming can be retried. Several
 * guards compare with `<`, so the order of these values is load-bearing.
 *
 * ::RMAKER_CLAIM_STATE_VERIFY_DONE is terminal: every command is refused once it is reached,
 * so a completed claim cannot be restarted, overwritten or aborted within the same session.
 */
typedef enum {
    RMAKER_CLAIM_STATE_PK_GENERATED = 1, /**< Private key is available. */
    RMAKER_CLAIM_STATE_INIT,             /**< Claim start handled; init request sent. */
    RMAKER_CLAIM_STATE_INIT_DONE,        /**< Node ID stored and CSR generated. */
    RMAKER_CLAIM_STATE_VERIFY_PENDING,   /**< Verify request fully sent; awaiting the certificate. */
    RMAKER_CLAIM_STATE_VERIFY_DONE,      /**< Certificate received and persisted. Terminal. */
} esp_rmaker_claim_state_t;

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decide whether a command may run in the current state.
 *
 * @param[in] state The current claiming state.
 * @param[in] msg   The inbound command's message type.
 * @return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success if the command may proceed.
 * @return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidState otherwise, which is the status the
 *         caller should answer with.
 */
RmakerClaim__RMakerClaimStatus claim_rules_check_command(esp_rmaker_claim_state_t state,
        RmakerClaim__RMakerClaimMsgType msg);

/**
 * @brief Decide whether an inbound fragment fits the reassembly buffer.
 *
 * Every argument is peer-controlled, and `size_t` is 32 bits on the targets, so @p offset is
 * bounded before it is used in any sum: otherwise a large enough offset wraps every later
 * check and the caller's memcpy lands outside the buffer entirely.
 *
 * @param[in] capacity Size of the reassembly buffer. One byte is reserved for a NUL.
 * @param[in] offset   Fragment offset within the reassembled payload.
 * @param[in] len      Fragment length.
 * @param[in] totallen Stated total length of the reassembled payload.
 * @return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success if the fragment can be stored.
 * @return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__NoMemory if it does not fit.
 * @return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidParam if it contradicts @p totallen.
 */
RmakerClaim__RMakerClaimStatus claim_rules_validate_fragment(size_t capacity, uint32_t offset,
        size_t len, uint32_t totallen);

/**
 * @brief Decide whether a fragment continues the response already being reassembled.
 *
 * A response must open at offset 0 and continue exactly where the previous fragment ended.
 * Without this a peer can reassemble on top of the previous step's payload, or leave gaps and
 * overlaps that pass silently.
 *
 * @param[in] offset   The fragment's offset.
 * @param[in] expected The offset the next fragment must have, i.e. the current cursor.
 * @return true if the fragment is in sequence.
 */
bool claim_rules_fragment_in_sequence(uint32_t offset, size_t expected);

/**
 * @brief Decide whether a fragment agrees with the total its response opened with.
 *
 * `totallen` is validated per fragment, so without this a peer can open a response declaring
 * one total and finish it declaring another. Every bound is enforced either way, so this is
 * strictness rather than safety -- but it is the same posture as rejecting gaps and
 * retransmits: one response, one set of numbers.
 *
 * @param[in] offset         The fragment's offset. Offset 0 is what pins the total, so it
 *                           always agrees.
 * @param[in] totallen       The total this fragment declares.
 * @param[in] expected_total The total pinned by the fragment at offset 0.
 * @return true if the fragment may be accepted.
 */
bool claim_rules_fragment_total_pinned(uint32_t offset, uint32_t totallen, uint32_t expected_total);

/**
 * @brief Replace real newlines with the two characters '\' and 'n'.
 *
 * The CSR travels inside a JSON string, and the claiming service expects the PEM on a single
 * line with escaped newlines. A trailing newline in @p src is dropped first, so the JSON
 * string does not end with an escaped one.
 *
 * @param[out] dst      Destination buffer.
 * @param[in]  dst_size Size of @p dst.
 * @param[in,out] src   NUL-terminated source. A trailing newline is removed in place.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_NO_MEM if the escaped form does not fit @p dst, rather than truncating.
 * @return ESP_RMAKER_INVALID_ARG on a NULL argument, or a destination smaller than the three
 *         bytes one escape plus a NUL needs.
 */
esp_rmaker_error_t claim_rules_escape_new_line(char *dst, size_t dst_size, char *src);

/**
 * @brief Turn the two characters '\' and 'n' back into real newlines, in place.
 *
 * Tolerates a value ending in a lone backslash: consuming it would otherwise step past the
 * terminator and walk on through whatever follows the buffer.
 *
 * @param[in,out] str NUL-terminated string to unescape.
 */
void claim_rules_unescape_new_line(char *str);

#ifdef __cplusplus
}
#endif

#endif /* __CLAIM_RULES_H__ */
