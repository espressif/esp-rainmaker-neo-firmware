/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_timesync_basic.c
 * @brief Test the basic timesync functionality
 */

#include "unity.h"
#include "test_timesync_common_prototypes.h"

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "osal_timesync.h"
#include "osal_timesync_internal.h"

#include "osal_time.h"

/* Test data ******************************************************************/

static time_t test_time_value = OSAL_TIMESYNC_REF_TIME + 1; /* 01-Jan-2025 00:00:01 - after reference time */
static bool test_setenv_called = false;
static char test_env_name[64] = {0};
static char test_env_value[128] = {0};

/* Test helper functions ******************************************************/

static time_t test_get_time_valid(time_t *timer)
{
    if (timer) {
        *timer = test_time_value;
    }
    return test_time_value;
}

static int test_setenv_mock(const char *name, const char *value, int rewrite)
{
    test_setenv_called = true;
    if (name) {
        strncpy(test_env_name, name, sizeof(test_env_name) - 1);
    }
    if (value) {
        strncpy(test_env_value, value, sizeof(test_env_value) - 1);
    }
    return 0;
}

/* Setup and teardown *********************************************************/

static osal_time_func original_get_time_func = NULL;
static osal_timesync_setenv_func original_setenv_func = NULL;

static void test_setup(void)
{
    // Save original function pointers
    original_get_time_func = osal_get_time;
    original_setenv_func = osal_timesync_setenv;

    // Reset test state
    test_setenv_called = false;
    memset(test_env_name, 0, sizeof(test_env_name));
    memset(test_env_value, 0, sizeof(test_env_value));
}

static void test_teardown(void)
{
    // Restore original function pointers
    if (original_get_time_func) {
        osal_get_time = original_get_time_func;
    }
    if (original_setenv_func) {
        osal_timesync_setenv = original_setenv_func;
    }
}

/* Test functions **************************************************************/

void test_timesync_init_basic(void)
{
    test_setup();

    // Test basic initialization with NULL config
    int result = osal_timesync_init(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "osal_timesync_init should succeed with NULL config");

    test_teardown();
}

void test_timesync_init_with_config(void)
{
    test_setup();

    // Test initialization with custom config
    osal_timesync_config_t config = {
        .server_name = "custom.ntp.server.com",
        .sync_time_cb = NULL  // Use default callback
    };

    int result = osal_timesync_init(&config);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "osal_timesync_init should succeed with custom config");

    test_teardown();
}

void test_timesync_sync_status(void)
{
    test_setup();

    // Initialize timesync to allow sync status checks
    int init_result = osal_timesync_init(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, init_result, "osal_timesync_init should succeed for sync status test");

    // If system clock is not after compile-time reference, we cannot assert synced deterministically
    time_t now = time(NULL);
    if (now <= OSAL_TIMESYNC_REF_TIME) {
        osal_timesync_deinit();
        TEST_IGNORE_MESSAGE("System clock is behind compile time; cannot validate sync.");
    }

    bool is_synced = osal_timesync_is_synced();
    TEST_ASSERT_TRUE_MESSAGE(is_synced, "timesync should report as synced when current time is after reference time");

    // Cleanup
    osal_timesync_deinit();
    test_teardown();
}

void test_timesync_time_validation(void)
{
    test_setup();

    // If system clock is not after compile-time reference, we cannot validate deterministically
    time_t now = time(NULL);
    if (now <= OSAL_TIMESYNC_REF_TIME) {
        TEST_IGNORE_MESSAGE("System clock is behind compile time; cannot validate time validity.");
    }

    bool is_valid = osal_timesync_time_is_valid();
    TEST_ASSERT_TRUE_MESSAGE(is_valid, "Time should be valid when after reference time");

    test_teardown();
}

void test_timesync_function_pointers(void)
{
    test_setup();

    // Test that we can inject our own time function
    osal_get_time = test_get_time_valid;

    time_t retrieved_time = osal_get_time(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(test_time_value, retrieved_time,
                                  "Function pointer injection should work");

    // Test setenv function pointer injection
    osal_timesync_setenv = test_setenv_mock;
    int result = osal_timesync_setenv("TEST_VAR", "test_value", 1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Mock setenv should succeed");
    TEST_ASSERT_TRUE_MESSAGE(test_setenv_called, "Mock setenv should have been called");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("TEST_VAR", test_env_name, "Environment variable name should match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("test_value", test_env_value, "Environment variable value should match");

    test_teardown();
}

void test_timesync_local_time_string(void)
{
    test_setup();
    osal_get_time = test_get_time_valid;

    // Test normal case
    char time_str[256];
    int result = osal_timesync_get_local_time_str(time_str, sizeof(time_str));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Getting local time string should succeed");
    TEST_ASSERT_TRUE_MESSAGE(strlen(time_str) > 0, "Time string should not be empty");
    TEST_ASSERT_TRUE_MESSAGE(strstr(time_str, "DST:") != NULL, "Time string should contain DST information");

    // Test with NULL buffer
    result = osal_timesync_get_local_time_str(NULL, sizeof(time_str));
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, result, "Should fail with NULL buffer");

    // Test with zero length buffer
    result = osal_timesync_get_local_time_str(time_str, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, result, "Should fail with zero length buffer");

    // Test with very small buffer
    char small_buf[5];
    result = osal_timesync_get_local_time_str(small_buf, sizeof(small_buf));
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, result, "Should fail with insufficient buffer size");

    test_teardown();
}

void test_timesync_set_time_rejects_invalid(void)
{
    // Non-positive epoch-ms values are invalid and must be rejected.
    TEST_ASSERT_LESS_THAN_MESSAGE(0, osal_timesync_set_time(0), "osal_timesync_set_time(0) should be rejected");
    TEST_ASSERT_LESS_THAN_MESSAGE(0, osal_timesync_set_time(-1), "osal_timesync_set_time(-1) should be rejected");
    // Positive but sub-threshold (1970-and-a-second) would step the clock to
    // garbage while still reading "not synced" - reject it too.
    TEST_ASSERT_LESS_THAN_MESSAGE(0, osal_timesync_set_time(1000), "sub-reference epoch should be rejected");
}

void test_timesync_epoch_ms_is_valid(void)
{
    // Below/at the reference floor -> invalid; comfortably after it -> valid.
    TEST_ASSERT_FALSE(osal_timesync_epoch_ms_is_valid(0));
    TEST_ASSERT_FALSE(osal_timesync_epoch_ms_is_valid(-1));
    TEST_ASSERT_FALSE(osal_timesync_epoch_ms_is_valid(1000));
    TEST_ASSERT_TRUE(osal_timesync_epoch_ms_is_valid(((int64_t)OSAL_TIMESYNC_REF_TIME + 1) * 1000));
}

void test_timesync_is_synced_not_gated_on_init(void)
{
    // is_synced() must reflect wall-clock validity even when this SDK has not
    // initialized timesync (the external-SNTP-owner case). After deinit it
    // must track the host clock, not the init flag.
    osal_timesync_deinit();

    time_t now = time(NULL);
    if (now <= OSAL_TIMESYNC_REF_TIME) {
        TEST_IGNORE_MESSAGE("System clock behind reference time; cannot validate.");
    }
    TEST_ASSERT_TRUE_MESSAGE(osal_timesync_is_synced(),
                             "is_synced must be true on a valid clock even without osal_timesync_init");
}

void test_timesync_set_time_preserves_timezone(void)
{
    test_setup();

    /* Establish a known, non-UTC timezone. */
    const char *tz = "PST8PDT,M3.2.0/2,M11.1.0/2";
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, setenv("TZ", tz, 1), "setenv TZ should succeed");
    tzset();

    /* How a fixed UTC instant renders in local time depends only on the
     * timezone, not on the current wall clock. Precondition: the offset
     * actually makes local != UTC, so the preservation check is meaningful. */
    time_t fixed = OSAL_TIMESYNC_REF_TIME + 100000;
    struct tm before, utc;
    TEST_ASSERT_NOT_NULL(localtime_r(&fixed, &before));
    TEST_ASSERT_NOT_NULL(gmtime_r(&fixed, &utc));
    TEST_ASSERT_TRUE_MESSAGE(before.tm_hour != utc.tm_hour,
                             "precondition: TZ offset should make local time differ from UTC");

    /* Coarse-set the clock. Must touch UTC only, never the timezone. The
     * return value is irrelevant here (settimeofday may be denied on the
     * host); the timezone invariant must hold either way. */
    (void)osal_timesync_set_time(((int64_t)OSAL_TIMESYNC_REF_TIME + 1) * 1000);

    /* TZ env must be intact. */
    const char *tz_after = getenv("TZ");
    TEST_ASSERT_NOT_NULL_MESSAGE(tz_after, "TZ must not be cleared by osal_timesync_set_time");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(tz, tz_after, "TZ must be unchanged by osal_timesync_set_time");

    /* Local-time rendering of the fixed instant must be unchanged (a wipe to
     * UTC would shift tm_hour by the offset). */
    struct tm after;
    TEST_ASSERT_NOT_NULL(localtime_r(&fixed, &after));
    TEST_ASSERT_EQUAL_INT_MESSAGE(before.tm_hour, after.tm_hour, "local hour changed -> timezone was wiped");
    TEST_ASSERT_EQUAL_INT_MESSAGE(before.tm_min, after.tm_min, "local minute changed -> timezone was wiped");
    TEST_ASSERT_EQUAL_INT_MESSAGE(before.tm_isdst, after.tm_isdst, "DST flag changed -> timezone was wiped");

    test_teardown();
}
