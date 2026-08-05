/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_claim.c
 * @brief Host tests for the claiming decision logic in claim/rules.h.
 *
 * These cover the state-machine guards, the reassembly bounds and the newline escaping. They
 * link claim_rules.c the same way the firmware does, so they run on every platform and stay
 * valid whether or not claim.c itself is built.
 *
 * Note what the bounds tests are really protecting: the reassembly arithmetic only overflows
 * where `size_t` is 32 bits, so on a 64-bit host it cannot wrap and the memory error is
 * invisible. The assertions are therefore on the *rejection*, never on the memory outcome - a
 * regression shows up as an accepted fragment, not as a crash.
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <string.h>

#include "claim/rules.h"

/* How much room the real claiming context gives the reassembly buffer. */
#define TEST_CAPACITY 3072

/* Shorthands, because the generated protobuf enum names are unwieldy in assertions. */
#define ST_OK      RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success
#define ST_PARAM   RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidParam
#define ST_STATE   RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidState
#define ST_NOMEM   RMAKER_CLAIM__RMAKER_CLAIM_STATUS__NoMemory

#define CMD_START  RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimStart
#define CMD_INIT   RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimInit
#define CMD_VERIFY RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimVerify
#define CMD_ABORT  RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimAbort

/* Fragment validation *********************************************/

void test_claim_validate_data_bounds(void)
{
    /* --- Accepted: a plain fragment, one that fills the buffer, and a tail fragment --- */
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_validate_fragment(TEST_CAPACITY, 0, 100, 100));
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_validate_fragment(TEST_CAPACITY, 0,
                          TEST_CAPACITY - 1, TEST_CAPACITY - 1));
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_validate_fragment(TEST_CAPACITY, TEST_CAPACITY - 2,
                          1, TEST_CAPACITY - 1));

    /* --- The 32-bit wrap. With offset = -len every bound in the original implementation
     *     evaluated to 0, and the caller's memcpy landed ~2 kB before the buffer. --- */
    TEST_ASSERT_EQUAL_INT(ST_NOMEM, claim_rules_validate_fragment(TEST_CAPACITY, 0xFFFFF401u,
                          TEST_CAPACITY - 1, 0));
    TEST_ASSERT_EQUAL_INT(ST_NOMEM, claim_rules_validate_fragment(TEST_CAPACITY, UINT32_MAX, 1, 100));
    TEST_ASSERT_EQUAL_INT(ST_NOMEM, claim_rules_validate_fragment(TEST_CAPACITY, 0xFFFFFF00u, 256, 3000));

    /* --- Offset inside the address space but outside the buffer --- */
    TEST_ASSERT_EQUAL_INT(ST_NOMEM, claim_rules_validate_fragment(TEST_CAPACITY, TEST_CAPACITY,
                          1, TEST_CAPACITY - 1));

    /* --- Fragments that would run past the end, leaving no room for the NUL --- */
    TEST_ASSERT_EQUAL_INT(ST_NOMEM, claim_rules_validate_fragment(TEST_CAPACITY, 0,
                          TEST_CAPACITY, TEST_CAPACITY - 1));
    TEST_ASSERT_EQUAL_INT(ST_NOMEM, claim_rules_validate_fragment(TEST_CAPACITY, TEST_CAPACITY - 1,
                          1, TEST_CAPACITY - 1));

    /* --- Stated totals that cannot fit, or are absent --- */
    TEST_ASSERT_EQUAL_INT(ST_NOMEM, claim_rules_validate_fragment(TEST_CAPACITY, 0, 10, TEST_CAPACITY));
    TEST_ASSERT_EQUAL_INT(ST_NOMEM, claim_rules_validate_fragment(TEST_CAPACITY, 0, 10, 0));

    /* --- Fragments inconsistent with the stated total --- */
    TEST_ASSERT_EQUAL_INT(ST_PARAM, claim_rules_validate_fragment(TEST_CAPACITY, 0, 101, 100));
    TEST_ASSERT_EQUAL_INT(ST_PARAM, claim_rules_validate_fragment(TEST_CAPACITY, 60, 50, 100));

    /* --- A zero-capacity buffer can hold nothing --- */
    TEST_ASSERT_EQUAL_INT(ST_NOMEM, claim_rules_validate_fragment(0, 0, 1, 1));
}

void test_claim_fragment_sequencing(void)
{
    /* --- A response opens at offset 0, whatever the cursor was --- */
    TEST_ASSERT_TRUE(claim_rules_fragment_in_sequence(0, 0));
    TEST_ASSERT_TRUE(claim_rules_fragment_in_sequence(0, 512));

    /* --- Otherwise it must continue exactly where the last fragment ended --- */
    TEST_ASSERT_TRUE(claim_rules_fragment_in_sequence(4, 4));

    /* --- Gaps, overlaps and retransmits are all out of sequence --- */
    TEST_ASSERT_FALSE(claim_rules_fragment_in_sequence(8, 4));
    TEST_ASSERT_FALSE(claim_rules_fragment_in_sequence(2, 4));
    TEST_ASSERT_FALSE(claim_rules_fragment_in_sequence(4, 8));

    /* --- A fresh response cannot open partway through --- */
    TEST_ASSERT_FALSE(claim_rules_fragment_in_sequence(4, 0));

    /* --- The total is pinned by the opening fragment --- */
    /* Offset 0 is what pins it, so it agrees with whatever was there before. */
    TEST_ASSERT_TRUE(claim_rules_fragment_total_pinned(0, 3000, 0));
    TEST_ASSERT_TRUE(claim_rules_fragment_total_pinned(0, 200, 3000));
    /* Later fragments must repeat it. */
    TEST_ASSERT_TRUE(claim_rules_fragment_total_pinned(200, 3000, 3000));
    /* Shrinking or growing the total mid-response is rejected. */
    TEST_ASSERT_FALSE(claim_rules_fragment_total_pinned(200, 200, 3000));
    TEST_ASSERT_FALSE(claim_rules_fragment_total_pinned(200, 4000, 3000));
}

/* Newline escaping ************************************************/

void test_claim_escape_new_line(void)
{
    char dst[512];

    /* --- Round trip: real newlines become '\' 'n' and back --- */
    char pem[] = "-----BEGIN CERTIFICATE REQUEST-----\nQUJD\nREVG\n-----END CERTIFICATE REQUEST-----\n";
    char expected[sizeof(pem)];
    strcpy(expected, pem);
    expected[strlen(expected) - 1] = '\0';   /* the trailing newline is dropped, not escaped */

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, claim_rules_escape_new_line(dst, sizeof(dst), pem));
    TEST_ASSERT_NULL(strchr(dst, '\n'));
    TEST_ASSERT_NOT_NULL(strstr(dst, "\\n"));

    claim_rules_unescape_new_line(dst);
    TEST_ASSERT_EQUAL_STRING(expected, dst);

    /* --- An empty source must not read src[-1] --- */
    char empty[] = "";
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, claim_rules_escape_new_line(dst, sizeof(dst), empty));
    TEST_ASSERT_EQUAL_STRING("", dst);

    /* --- A source whose escaped form does not fit is refused, not truncated --- */
    char newlines[64];
    memset(newlines, '\n', sizeof(newlines) - 1);
    newlines[sizeof(newlines) - 1] = '\0';
    char small[32];
    TEST_ASSERT_EQUAL(ESP_RMAKER_NO_MEM, claim_rules_escape_new_line(small, sizeof(small), newlines));

    /* --- Bad arguments --- */
    char src[] = "x";
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, claim_rules_escape_new_line(NULL, sizeof(dst), src));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, claim_rules_escape_new_line(dst, sizeof(dst), NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, claim_rules_escape_new_line(dst, 0, src));
    /* Under three bytes there is no room for an escape plus the NUL, and computing the write
     * limit would land before the buffer. Rejected on the size alone, before any arithmetic. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, claim_rules_escape_new_line(dst, 1, src));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, claim_rules_escape_new_line(dst, 2, src));
    /* Three bytes is the smallest accepted destination: room for the escape it reserves and the
     * NUL, so an empty source fits and any character is refused rather than truncated. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, claim_rules_escape_new_line(dst, 3, empty));
    TEST_ASSERT_EQUAL_STRING("", dst);
    TEST_ASSERT_EQUAL(ESP_RMAKER_NO_MEM, claim_rules_escape_new_line(dst, 3, src));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, claim_rules_escape_new_line(dst, 4, src));
    TEST_ASSERT_EQUAL_STRING("x", dst);
}

void test_claim_unescape_new_line(void)
{
    char buf[64];

    /* --- The ordinary case --- */
    strcpy(buf, "line1\\nline2");
    claim_rules_unescape_new_line(buf);
    TEST_ASSERT_EQUAL_STRING("line1\nline2", buf);

    /* --- A value ending in an escaped newline: the common upstream walk-off trigger --- */
    strcpy(buf, "line1\\n");
    claim_rules_unescape_new_line(buf);
    TEST_ASSERT_EQUAL_STRING("line1\n", buf);

    /* --- A lone trailing backslash. Without the guard the terminator is copied and the walk
     *     continues, so the sentinel planted after the NUL must survive. --- */
    memset(buf, 0, sizeof(buf));
    strcpy(buf, "abc\\");
    strcpy(buf + 5, "SENTINEL");
    claim_rules_unescape_new_line(buf);
    TEST_ASSERT_EQUAL_STRING("abc", buf);
    TEST_ASSERT_EQUAL_STRING("SENTINEL", buf + 5);

    /* --- A backslash before something other than 'n' is dropped, matching the JSON the
     *     claiming service sends --- */
    strcpy(buf, "a\\tb");
    claim_rules_unescape_new_line(buf);
    TEST_ASSERT_EQUAL_STRING("atb", buf);

    /* --- Nothing to do, and NULL tolerated --- */
    strcpy(buf, "plain");
    claim_rules_unescape_new_line(buf);
    TEST_ASSERT_EQUAL_STRING("plain", buf);
    claim_rules_unescape_new_line(NULL);
}

/* Command guards **************************************************/

void test_claim_command_guards(void)
{
    /* --- Start: a restart is legitimate right up to completion --- */
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_check_command(RMAKER_CLAIM_STATE_PK_GENERATED, CMD_START));
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_check_command(RMAKER_CLAIM_STATE_INIT, CMD_START));
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_check_command(RMAKER_CLAIM_STATE_INIT_DONE, CMD_START));
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_check_command(RMAKER_CLAIM_STATE_VERIFY_PENDING, CMD_START));

    /* --- Init: only the command that consumes the response, and the ones that pump
     *     fragments. Past that, re-running it would store a second node ID. --- */
    TEST_ASSERT_EQUAL_INT(ST_STATE, claim_rules_check_command(RMAKER_CLAIM_STATE_PK_GENERATED, CMD_INIT));
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_check_command(RMAKER_CLAIM_STATE_INIT, CMD_INIT));
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_check_command(RMAKER_CLAIM_STATE_INIT_DONE, CMD_INIT));
    TEST_ASSERT_EQUAL_INT(ST_STATE, claim_rules_check_command(RMAKER_CLAIM_STATE_VERIFY_PENDING, CMD_INIT));

    /* --- Verify: only while a certificate is outstanding --- */
    TEST_ASSERT_EQUAL_INT(ST_STATE, claim_rules_check_command(RMAKER_CLAIM_STATE_PK_GENERATED, CMD_VERIFY));
    TEST_ASSERT_EQUAL_INT(ST_STATE, claim_rules_check_command(RMAKER_CLAIM_STATE_INIT, CMD_VERIFY));
    TEST_ASSERT_EQUAL_INT(ST_STATE, claim_rules_check_command(RMAKER_CLAIM_STATE_INIT_DONE, CMD_VERIFY));
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_check_command(RMAKER_CLAIM_STATE_VERIFY_PENDING, CMD_VERIFY));

    /* --- Abort: allowed at every state before completion --- */
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_check_command(RMAKER_CLAIM_STATE_PK_GENERATED, CMD_ABORT));
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_check_command(RMAKER_CLAIM_STATE_INIT, CMD_ABORT));
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_check_command(RMAKER_CLAIM_STATE_INIT_DONE, CMD_ABORT));
    TEST_ASSERT_EQUAL_INT(ST_OK, claim_rules_check_command(RMAKER_CLAIM_STATE_VERIFY_PENDING, CMD_ABORT));

    /* --- VERIFY_DONE is terminal: a completed claim cannot be restarted, re-initialised,
     *     overwritten or cancelled within the same session --- */
    TEST_ASSERT_EQUAL_INT(ST_STATE, claim_rules_check_command(RMAKER_CLAIM_STATE_VERIFY_DONE, CMD_START));
    TEST_ASSERT_EQUAL_INT(ST_STATE, claim_rules_check_command(RMAKER_CLAIM_STATE_VERIFY_DONE, CMD_INIT));
    TEST_ASSERT_EQUAL_INT(ST_STATE, claim_rules_check_command(RMAKER_CLAIM_STATE_VERIFY_DONE, CMD_VERIFY));
    TEST_ASSERT_EQUAL_INT(ST_STATE, claim_rules_check_command(RMAKER_CLAIM_STATE_VERIFY_DONE, CMD_ABORT));

    /* --- An unknown command type is refused in every state --- */
    TEST_ASSERT_EQUAL_INT(ST_STATE, claim_rules_check_command(RMAKER_CLAIM_STATE_INIT, (int)0x7FFF));
}
