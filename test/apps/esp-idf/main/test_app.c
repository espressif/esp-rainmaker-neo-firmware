/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_app.c
 * @brief Single ESP-IDF test app for all RMNG SDK components (osal, esp_rmaker_neo_common, esp_rmaker_neo, esp_rmaker_neo_ota).
 */

/* Includes *******************************************************************/

#include "unity.h"
#include "unity_test_runner.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "app_network.h"

#include "test_platform_common_prototypes.h"
#include "test_nvs_common_prototypes.h"
#include "test_mqtt_common_prototypes.h"
#include "test_http_common_prototypes.h"
#include "test_json_common_prototypes.h"
#include "test_timesync_common_prototypes.h"
#include "test_rmng_common_prototypes.h"
#include "test_rmng_prototypes.h"
#include "test_rmng_ota_prototypes.h"

/* Constants ******************************************************************/

static const char *TAG = "test_app";

/* Setup and teardown *********************************************************/

/* Per-suite fixtures.
 *
 * Unity calls setUp()/tearDown() around every RUN_TEST(), but there is only one
 * pair for the whole app, while each suite here runs from its own TEST_CASE.
 * Suites that need per-test state reset (currently esp_rmaker_neo_ota, whose spy globals
 * and FSM context accumulate otherwise) register their fixtures for the duration
 * of their TEST_CASE via run_suite_with_fixtures(); the definitions below just
 * dispatch. The POSIX runners wire the same functions straight into
 * setUp()/tearDown(), so both platforms reset per test.
 */
static void (*s_suite_set_up)(void);
static void (*s_suite_tear_down)(void);

void setUp(void)
{
    if (s_suite_set_up != NULL) {
        s_suite_set_up();
    }
}

void tearDown(void)
{
    if (s_suite_tear_down != NULL) {
        s_suite_tear_down();
    }
}

/* Run one suite with its fixtures installed, then uninstall them so the next
 * TEST_CASE doesn't inherit them (Unity keeps running after a failed assert). */
static int run_suite_with_fixtures(void (*suite_set_up)(void), void (*suite_tear_down)(void), int (*suite_run)(void))
{
    s_suite_set_up = suite_set_up;
    s_suite_tear_down = suite_tear_down;
    int failures = suite_run();
    s_suite_set_up = NULL;
    s_suite_tear_down = NULL;
    return failures;
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

static bool s_wifi_setup = false;

static void wifi_setup(void)
{
    if (s_wifi_setup) {
        return;
    }
    nvs_setup();
    ESP_LOGI(TAG, "wifi_setup");
    app_network_init();
    app_network_start(POP_TYPE_MAC);
    s_wifi_setup = true;
}

/* Test cases *****************************************************************/

TEST_CASE("All platform-common tests", "[platform-common]")
{
    test_platform_common_all_tests_unity();
}

TEST_CASE("All nvs-common tests", "[nvs-common]")
{
    test_nvs_common_all_tests_unity();
}

TEST_CASE("All json-common tests", "[json-common]")
{
    test_json_common_all_tests_unity();
}

TEST_CASE("All mqtt-common tests", "[mqtt-common]")
{
    wifi_setup();
    test_mqtt_common_all_tests_unity();
}

TEST_CASE("All http-common tests", "[http-common]")
{
    wifi_setup();
    test_http_common_all_tests_unity();
}

TEST_CASE("All timesync-common tests", "[timesync-common]")
{
    wifi_setup();
    test_timesync_common_all_tests_unity();
}

TEST_CASE("All esp_rmaker_neo_common tests", "[esp_rmaker_neo_common]")
{
    test_rmng_common_all_tests_unity();
}

TEST_CASE("All esp_rmaker_neo tests", "[esp_rmaker_neo]")
{
    wifi_setup();
    test_rmng_all_tests_unity();
}

TEST_CASE("All esp_rmaker_neo_ota tests", "[esp_rmaker_neo_ota]")
{
    (void)run_suite_with_fixtures(rmng_ota_jobs_setUp, rmng_ota_jobs_tearDown, test_rmng_ota_all_tests_unity);
}

/* Main ***********************************************************************/

void app_main(void)
{
    unity_run_menu();
}
