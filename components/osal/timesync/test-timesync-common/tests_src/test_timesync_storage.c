/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_timesync_storage.c
 * @brief Test the storage functionality for timesync
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
#include "osal_storage.h"

/* Test data ******************************************************************/

static const char *test_posix_tz = "PST8PDT,M3.2.0,M11.1.0";
static const char *test_location_tz = "America/Los_Angeles";

/* Test helper functions ******************************************************/

// These tests rely on the NVS functionality being properly initialized
// by the test framework. The storage functions are tested indirectly
// through the timezone setting/getting functions.

/* Setup and teardown *********************************************************/

static void test_setup(void)
{
    // Initialize NVS for testing (if not already initialized)
    osal_storage_init(NULL);
}

static void test_teardown(void)
{
    // Clean up any test data in NVS
    osal_storage_handle_t handle = NULL;
    osal_err_t err = osal_storage_open(NULL, OSAL_TIMESYNC_NVS_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &handle);
    if (err == OSAL_ERR_OK && handle) {
        osal_storage_erase_all(handle);
        osal_storage_commit(handle);
        osal_storage_close(handle);
    }
}

/* Test functions **************************************************************/

void test_timesync_storage_basic(void)
{
    test_setup();

    // Test storing and retrieving timezone data
    int result = osal_timesync_storage_set_string(OSAL_TIMESYNC_TZ_POSIX_KEY, test_posix_tz);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Setting timezone POSIX string should succeed");

    result = osal_timesync_storage_set_string(OSAL_TIMESYNC_TZ_KEY, test_location_tz);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Setting timezone location string should succeed");

    // Retrieve and verify POSIX timezone
    char *retrieved_posix = NULL;
    result = osal_timesync_storage_get_string(OSAL_TIMESYNC_TZ_POSIX_KEY, &retrieved_posix);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Getting timezone POSIX string should succeed");
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved_posix, "Retrieved POSIX string should not be NULL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(test_posix_tz, retrieved_posix, "Retrieved POSIX string should match stored value");

    // Clean up retrieved string
    if (retrieved_posix) {
        free(retrieved_posix);
        retrieved_posix = NULL;
    }

    // Retrieve and verify location timezone
    char *retrieved_location = NULL;
    result = osal_timesync_storage_get_string(OSAL_TIMESYNC_TZ_KEY, &retrieved_location);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Getting timezone location string should succeed");
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved_location, "Retrieved location string should not be NULL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(test_location_tz, retrieved_location, "Retrieved location string should match stored value");

    // Clean up retrieved string
    if (retrieved_location) {
        free(retrieved_location);
        retrieved_location = NULL;
    }

    test_teardown();
}

void test_timesync_storage_error_cases(void)
{
    test_setup();

    // Test getting non-existent key
    char *retrieved_value = NULL;
    int result = osal_timesync_storage_get_string("non_existent_key", &retrieved_value);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, result, "Getting non-existent key should fail");
    TEST_ASSERT_NULL_MESSAGE(retrieved_value, "Retrieved value should be NULL for non-existent key");

    // Test setting with NULL key (this should be handled by NVS layer)
    result = osal_timesync_storage_set_string(NULL, test_posix_tz);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, result, "Setting with NULL key should fail");

    // Test setting with NULL value (this should be handled by NVS layer)
    result = osal_timesync_storage_set_string(OSAL_TIMESYNC_TZ_POSIX_KEY, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, result, "Setting with NULL value should fail");

    // Test getting with NULL output pointer
    result = osal_timesync_storage_get_string(OSAL_TIMESYNC_TZ_POSIX_KEY, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, result, "Getting with NULL output pointer should fail");

    test_teardown();
}

// Test the high-level timezone getter functions that use storage
void test_timesync_timezone_persistence(void)
{
    test_setup();

    // Set timezone and verify it persists
    int result = osal_timesync_set_timezone_posix(test_posix_tz);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Setting POSIX timezone should succeed");

    // Retrieve it through the public API
    char *retrieved_posix = osal_timesync_get_timezone_posix();
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved_posix, "Should be able to retrieve stored POSIX timezone");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(test_posix_tz, retrieved_posix, "Retrieved timezone should match");

    if (retrieved_posix) {
        free(retrieved_posix);
    }

    // Set location timezone
    result = osal_timesync_set_timezone(test_location_tz);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "Setting location timezone should succeed");

    // Retrieve location timezone
    char *retrieved_location = osal_timesync_get_timezone();
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved_location, "Should be able to retrieve stored location timezone");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(test_location_tz, retrieved_location, "Retrieved timezone should match");

    if (retrieved_location) {
        free(retrieved_location);
    }

    test_teardown();
}
