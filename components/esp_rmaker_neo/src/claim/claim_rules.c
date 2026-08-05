/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file claim_rules.c
 * @brief Decision logic for assisted claiming. See claim/rules.h.
 */

/* Includes *******************************************************/

/* Declarations */
#include "claim/rules.h"

/* Standard includes */
#include <string.h>
#include <stdbool.h>

/* Public function definitions *******************************************************/

RmakerClaim__RMakerClaimStatus claim_rules_check_command(esp_rmaker_claim_state_t state,
        RmakerClaim__RMakerClaimMsgType msg)
{
    /* VERIFY_DONE is terminal for every command. A restart would drop back to INIT and make a
     * second TypeCmdClaimInit legal, storing a node ID the issued certificate was not signed
     * for; a second verify would overwrite the stored certificate; an abort would discard a
     * claim that already succeeded, failing startup for credentials that are on disk. */
    if (state == RMAKER_CLAIM_STATE_VERIFY_DONE) {
        return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidState;
    }

    switch (msg) {
    case RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimStart:
        /* A restart is legitimate at any point before completion. */
        return state >= RMAKER_CLAIM_STATE_PK_GENERATED
               ? RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success
               : RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidState;

    case RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimInit:
        /* INIT consumes the service's response; INIT_DONE pumps the remaining CSR fragments.
         * Anything else means init has already been handled and the flow has moved on. */
        return (state == RMAKER_CLAIM_STATE_INIT || state == RMAKER_CLAIM_STATE_INIT_DONE)
               ? RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success
               : RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidState;

    case RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimVerify:
        /* Only while a certificate is actually outstanding. */
        return state == RMAKER_CLAIM_STATE_VERIFY_PENDING
               ? RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success
               : RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidState;

    case RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimAbort:
        /* Abandoning an attempt is always allowed before it completes. */
        return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success;

    default:
        return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidState;
    }
}

RmakerClaim__RMakerClaimStatus claim_rules_validate_fragment(size_t capacity, uint32_t offset,
        size_t len, uint32_t totallen)
{
    if (capacity == 0) {
        return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__NoMemory;
    }

    /* Bound the offset on its own first. Everything after this is wrap-free. */
    if (offset >= capacity) {
        return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__NoMemory;
    }
    /* Derive the length limit from the bounded offset, so the subtraction cannot underflow.
     * The -1 keeps room for the NUL terminator the JSON parsing downstream relies on. */
    if (len > capacity - 1 - offset) {
        return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__NoMemory;
    }
    /* A zero total would otherwise sail through as a "complete" empty response and fail the
     * claim for good on a JSON parse error, rather than being rejected as the malformed frame
     * it is. */
    if (totallen == 0 || totallen >= capacity) {
        return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__NoMemory;
    }
    if (len + offset > totallen) {
        return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidParam;
    }
    return RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success;
}

bool claim_rules_fragment_in_sequence(uint32_t offset, size_t expected)
{
    return offset == 0 || (size_t)offset == expected;
}

bool claim_rules_fragment_total_pinned(uint32_t offset, uint32_t totallen, uint32_t expected_total)
{
    return offset == 0 || totallen == expected_total;
}

esp_rmaker_error_t claim_rules_escape_new_line(char *dst, size_t dst_size, char *src)
{
    /* Under three bytes there is no room for even one escape plus the NUL, and the `limit`
     * below would be computed before the start of the buffer, which is undefined behaviour
     * however the comparison then happens to turn out. */
    if (!dst || !src || dst_size < 3) {
        return ESP_RMAKER_INVALID_ARG;
    }

    memset(dst, 0, dst_size);

    /* Drop a trailing newline so the JSON string does not end with an escaped one. Testing the
     * length first keeps this off src[-1] for an empty string. */
    size_t src_len = strlen(src);
    if (src_len > 0 && src[src_len - 1] == '\n') {
        src[src_len - 1] = '\0';
    }

    char *target = dst;
    /* Leave room for a two-character escape plus the NUL. */
    const char *limit = dst + dst_size - 3;
    while (*src) {
        if (target >= limit) {
            return ESP_RMAKER_NO_MEM;
        }
        if (*src == '\n') {
            *target++ = '\\';
            *target++ = 'n';
            src++;
            continue;
        }
        *target++ = *src++;
    }
    *target = '\0';
    return ESP_RMAKER_OK;
}

void claim_rules_unescape_new_line(char *str)
{
    if (!str) {
        return;
    }
    char *target = str;
    while (*str) {
        if (*str == '\\') {
            str++;
            if (*str == '\0') {
                /* A lone trailing backslash. Consuming it and falling through would copy the
                 * NUL and step past it, and the loop would walk on through whatever follows
                 * the buffer. */
                break;
            }
            if (*str == 'n') {
                *target++ = '\n';
                str++;
                continue;
            }
        }
        *target++ = *str++;
    }
    *target = '\0';
}
