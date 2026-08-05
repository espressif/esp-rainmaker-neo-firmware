/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_timesync_timezone.c
 * @brief Test the timezone functionality
 */

#include "unity.h"
#include "test_timesync_common_prototypes.h"

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "osal_timesync.h"
#include "osal_timesync_internal.h"
#include "osal_mem_alloc.h"

/* Test data ******************************************************************/

static bool test_setenv_called = false;
static char test_env_name[64] = {0};
static char test_env_value[128] = {0};

/* Test helper functions ******************************************************/

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

static osal_timesync_setenv_func original_setenv_func = NULL;

static void test_setup(void)
{
    // Save original function pointers
    original_setenv_func = osal_timesync_setenv;

    // Reset test state
    test_setenv_called = false;
    memset(test_env_name, 0, sizeof(test_env_name));
    memset(test_env_value, 0, sizeof(test_env_value));
}

static void test_teardown(void)
{
    // Restore original function pointers
    if (original_setenv_func) {
        osal_timesync_setenv = original_setenv_func;
    }
}

/* Test functions **************************************************************/

void test_timesync_timezone_posix(void)
{
    test_setup();
    osal_timesync_setenv = test_setenv_mock;

    // Test setting a valid POSIX timezone
    const char *test_tz = "PST8PDT,M3.2.0,M11.1.0";
    int result = osal_timesync_set_timezone_posix(test_tz);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Setting POSIX timezone should succeed");
    TEST_ASSERT_TRUE_MESSAGE(test_setenv_called, "setenv should have been called");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("TZ", test_env_name, "Should set TZ environment variable");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(test_tz, test_env_value, "Should set correct timezone value");

    // Test setting with NULL timezone
    result = osal_timesync_set_timezone_posix(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, result, "Setting NULL POSIX timezone should fail");

    test_teardown();
}

void test_timesync_timezone_location(void)
{
    test_setup();
    osal_timesync_setenv = test_setenv_mock;

    // Test setting a valid location-based timezone
    const char *test_tz = "America/Los_Angeles";
    int result = osal_timesync_set_timezone(test_tz);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Setting location timezone should succeed");
    TEST_ASSERT_TRUE_MESSAGE(test_setenv_called, "setenv should have been called");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("TZ", test_env_name, "Should set TZ environment variable");
    // The exact value will be the POSIX string from the database
    TEST_ASSERT_TRUE_MESSAGE(strlen(test_env_value) > 0, "Should set a non-empty timezone value");

    // Test setting with NULL timezone
    result = osal_timesync_set_timezone(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, result, "Setting NULL timezone should fail");

    test_teardown();
}

void test_timesync_timezone_database(void)
{
    test_setup();

    // Test looking up valid timezone entries
    const char *posix_str = osal_timesync_tz_db_get_posix_str("America/Los_Angeles");
    TEST_ASSERT_NOT_NULL_MESSAGE(posix_str, "Should find timezone in database");
    TEST_ASSERT_TRUE_MESSAGE(strlen(posix_str) > 0, "POSIX string should not be empty");

    posix_str = osal_timesync_tz_db_get_posix_str("Asia/Shanghai");
    TEST_ASSERT_NOT_NULL_MESSAGE(posix_str, "Should find Asia/Shanghai in database");

    posix_str = osal_timesync_tz_db_get_posix_str("Europe/London");
    TEST_ASSERT_NOT_NULL_MESSAGE(posix_str, "Should find Europe/London in database");

    // Test looking up invalid timezone
    posix_str = osal_timesync_tz_db_get_posix_str("Invalid/Timezone");
    TEST_ASSERT_NULL_MESSAGE(posix_str, "Should not find invalid timezone");

    // Test with NULL input
    posix_str = osal_timesync_tz_db_get_posix_str(NULL);
    TEST_ASSERT_NULL_MESSAGE(posix_str, "Should return NULL for NULL input");

    test_teardown();
}

void test_timesync_timezone_error_cases(void)
{
    test_setup();
    osal_timesync_setenv = test_setenv_mock;

    // Test setting invalid location timezone (not in database)
    int result = osal_timesync_set_timezone("Invalid/Timezone");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, result, "Setting invalid timezone should fail");
    TEST_ASSERT_FALSE_MESSAGE(test_setenv_called, "setenv should not have been called for invalid timezone");

    // Test setting empty string timezone
    result = osal_timesync_set_timezone("");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, result, "Setting empty timezone should fail");

    // Test setting empty POSIX timezone
    result = osal_timesync_set_timezone_posix("");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Setting empty POSIX timezone should succeed");

    test_teardown();
}
