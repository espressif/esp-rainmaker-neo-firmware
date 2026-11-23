/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_app.c
 * @brief Single ESP-IDF test app for all RainMaker Neo SDK components (osal, esp_rmaker_neo_common, esp_rmaker_neo, esp_rmaker_neo_ota).
 */

/* Includes *******************************************************************/

#include "unity.h"
#include "unity_test_runner.h"

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "osal_timesync.h"

#if !CONFIG_IDF_TARGET_LINUX
#include "test_wifi.h"
#endif

#include "test_osal_platform_prototypes.h"
#include "test_osal_storage_prototypes.h"
#include "test_osal_mqtt_prototypes.h"
#include "test_osal_http_prototypes.h"
#include "test_osal_json_prototypes.h"
#include "test_osal_timesync_prototypes.h"
#include "test_rmaker_neo_common_prototypes.h"
#include "test_rmaker_neo_prototypes.h"
#if !CONFIG_IDF_TARGET_LINUX
#include "test_rmaker_neo_ota_prototypes.h"
#endif

/* Constants ******************************************************************/

static const char *TAG = "test_app";

/* Setup and teardown *********************************************************/

/* Per-suite fixtures.
 *
 * Unity calls setUp()/tearDown() around every RUN_TEST(), but there is only one
 * pair for the whole app, while each suite here runs from its own TEST_CASE.
 * Suites that need per-test state reset (currently esp_rmaker_neo_ota, whose spy globals
 * and FSM context accumulate otherwise) register their fixtures for the duration
 * of their TEST_CASE via run_suite(); the definitions below just dispatch. The
 * POSIX runners wire the same functions straight into setUp()/tearDown(), so
 * both platforms reset per test.
 */
static void (*s_suite_set_up)(void);
static void (*s_suite_tear_down)(void);

/* Report a test that ends with materially less heap than it started with. Threshold keeps benign
 * allocator churn quiet. */
#define HEAP_LEAK_REPORT_THRESHOLD 1024
#define HEAP_SETTLE_DELAY_MS       10

static uint32_t s_test_heap_before;

void setUp(void)
{
    s_test_heap_before = esp_get_free_heap_size();
    if (s_suite_set_up != NULL) {
        s_suite_set_up();
    }
}

void tearDown(void)
{
    if (s_suite_tear_down != NULL) {
        s_suite_tear_down();
    }

    /* A deleted task's stack and TCB are reclaimed by the idle task, so sample only after it has
     * had a chance to run; otherwise every test that spawns one looks like a leak. */
    vTaskDelay(pdMS_TO_TICKS(HEAP_SETTLE_DELAY_MS));

    int32_t delta = (int32_t)(esp_get_free_heap_size() - s_test_heap_before);
    if (delta <= -HEAP_LEAK_REPORT_THRESHOLD) {
        ESP_LOGW(TAG, "%s: heap down %" PRId32 " bytes", Unity.CurrentTestName, -delta);
    }
}

/* Run one suite with its fixtures installed (NULL for none), restore the enclosing TEST_CASE's
 * Unity identity that the nested UNITY_BEGIN() overwrote, and fail that case on the suite's
 * failure count. */
static void run_suite(void (*suite_set_up)(void), void (*suite_tear_down)(void), int (*suite_run)(void))
{
    const char *outer_file = Unity.TestFile;
    const char *outer_name = Unity.CurrentTestName;
    UNITY_LINE_TYPE outer_line = Unity.CurrentTestLineNumber;

    uint32_t heap_before = esp_get_free_heap_size();
    uint32_t outer_test_heap_before = s_test_heap_before;

    s_suite_set_up = suite_set_up;
    s_suite_tear_down = suite_tear_down;
    int failures = suite_run();
    s_suite_set_up = NULL;
    s_suite_tear_down = NULL;

    /* The nested tests' setUp() overwrote the enclosing case's own baseline. */
    s_test_heap_before = outer_test_heap_before;

    uint32_t heap_after = esp_get_free_heap_size();
    ESP_LOGI(TAG, "heap: %" PRIu32 " -> %" PRIu32 " (%+" PRId32 "), low water %" PRIu32,
             heap_before, heap_after, (int32_t)(heap_after - heap_before), esp_get_minimum_free_heap_size());

    Unity.TestFile = outer_file;
    Unity.CurrentTestName = outer_name;
    Unity.CurrentTestLineNumber = outer_line;

    /* Re-arm Unity.AbortFrame: the nested suite left it on a frame that has since returned, and a
     * failing assert longjmps into it. */
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, failures, "suite reported failures; see the per-test output above");
    }
}

static void nvs_setup(void)
{
    ESP_LOGI(TAG, "nvs_setup");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void wifi_setup(void)
{
    nvs_setup();
#if !CONFIG_IDF_TARGET_LINUX
    /* The linux target runs on a host that is already on a network. */
    TEST_ASSERT_EQUAL(ESP_OK, test_wifi_connect());
#endif
}

static void time_setup(void)
{
    wifi_setup();

    static bool s_time_synced = false;
    if (s_time_synced) {
        return;
    }
    TEST_ASSERT_EQUAL(0, osal_timesync_init(NULL));
    int err = osal_timesync_wait_for_sync(CONFIG_TEST_APP_TIME_SYNC_TIMEOUT_S * 1000);
    TEST_ASSERT_EQUAL(0, osal_timesync_deinit());
    TEST_ASSERT_EQUAL_MESSAGE(0, err, "SNTP did not sync; date-dependent tests would run on a 1970 clock");
    s_time_synced = true;
}

/* Test cases *****************************************************************/

TEST_CASE("All platform-common tests", "[platform-common]")
{
    run_suite(NULL, NULL, test_osal_platform_all_tests_unity);
}

TEST_CASE("All nvs-common tests", "[nvs-common]")
{
    run_suite(NULL, NULL, test_osal_storage_all_tests_unity);
}

TEST_CASE("All json-common tests", "[json-common]")
{
    run_suite(NULL, NULL, test_osal_json_all_tests_unity);
}

TEST_CASE("All mqtt-common tests", "[mqtt-common]")
{
    wifi_setup();
    run_suite(NULL, NULL, test_osal_mqtt_all_tests_unity);
}

TEST_CASE("All http-common tests", "[http-common]")
{
    wifi_setup();
    run_suite(NULL, test_osal_http_tear_down, test_osal_http_all_tests_unity);
}

TEST_CASE("All timesync-common tests", "[timesync-common]")
{
    time_setup();
    run_suite(NULL, NULL, test_osal_timesync_all_tests_unity);
}

TEST_CASE("All esp_rmaker_neo_common tests", "[esp_rmaker_neo_common]")
{
    run_suite(NULL, NULL, test_rmaker_neo_common_all_tests_unity);
}

TEST_CASE("All esp_rmaker_neo tests", "[esp_rmaker_neo]")
{
    time_setup();
    run_suite(NULL, NULL, test_rmaker_neo_all_tests_unity);
}


#if !CONFIG_IDF_TARGET_LINUX /* no OTA on the linux target */
TEST_CASE("All esp_rmaker_neo_ota tests", "[esp_rmaker_neo_ota]")
{
    run_suite(test_rmaker_neo_ota_jobs_setUp, test_rmaker_neo_ota_jobs_tearDown,
              test_rmaker_neo_ota_all_tests_unity);
}
#endif

/* Main ***********************************************************************/

void app_main(void)
{
    unity_run_menu();
}
