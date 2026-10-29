/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_ota_rollback.c
 * @brief Unit tests for the first boot after an OTA (esp_rmaker_ota.c).
 *
 * POSIX only: the fixtures drive the partition states of the POSIX OTA config.
 */

#include "unity.h"
#include "test_rmaker_neo_ota_prototypes.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "constants/esp_rmaker_nvs_common.h"
#include "osal_storage.h"
#include "osal_ota_posix_config.h"
#include "osal_ota_posix_shared.h"
#include "posix_exit_codes.h"

#include "ota_timeout_handler.h"

static bool g_timer_init_fail = false;
static esp_rmaker_error_t __timer_init(const rmaker_ota_timeout_handler_config_t *config, rmaker_ota_timeout_handler_handle_t *p_handle);

/* Include source to access the static reboot and diagnostics handlers (same TU) */
#define rmaker_ota_timeout_handler_init __timer_init
#include "esp_rmaker_ota.c"
#undef rmaker_ota_timeout_handler_init

extern bool TEST_OTA_JOBS_DO_NOT_WRAP;
extern osal_event_base_t g_test_last_posted_event_base;
extern int32_t g_test_last_posted_event_id;

OSAL_EVENT_DEFINE_BASE(TEST_OSAL_OTA_EVENT);
#define TEST_OSAL_OTA_EVENT_APP_VALID 7

static esp_rmaker_error_t __timer_init(const rmaker_ota_timeout_handler_config_t *config, rmaker_ota_timeout_handler_handle_t *p_handle)
{
    if (g_timer_init_fail) {
        return ESP_RMAKER_NO_MEM;
    }
    return rmaker_ota_timeout_handler_init(config, p_handle);
}

static int g_diag_calls = 0;
static esp_rmaker_ota_diag_state_t g_diag_last_state;
static void *g_diag_last_priv = NULL;
static int g_diag_priv_marker = 0;

static esp_rmaker_ota_diag_status_t __diag_success(esp_rmaker_ota_diag_priv_t *ota_diag_priv, void *priv)
{
    g_diag_calls++;
    g_diag_last_state = ota_diag_priv->state;
    g_diag_last_priv = priv;
    return OTA_DIAG_STATUS_SUCCESS;
}

static uint8_t __boot_slot(void)
{
    uint8_t boot_idx = 0;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_get_boot_partition(&boot_idx));
    return boot_idx;
}

static uint8_t __last_valid_slot(void)
{
    uint8_t last_valid_idx = 0;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_get_last_valid_partition(&last_valid_idx));
    return last_valid_idx;
}

static void __set_all_slots_valid(void)
{
    for (uint8_t i = 0; i < OSAL_OTA_POSIX_PART_COUNT; i++) {
        TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_partition_state(i, OSAL_OTA_IMG_VALID));
    }
}

/* Stage the first boot of a new image that waits for verification */
static void __begin_pending_verify_boot(void)
{
    TEST_OTA_JOBS_DO_NOT_WRAP = true;
    osal_err_t err = osal_event_loop_create_default();
    TEST_ASSERT_TRUE(err == OSAL_ERR_OK || err == OSAL_ERR_INVALID_STATE);
    osal_storage_reset((char *)RMAKER_NVS_PART_NAME);
    osal_storage_init((char *)RMAKER_NVS_PART_NAME);
    __set_all_slots_valid();
    uint8_t old_slot = __boot_slot();
    uint8_t new_slot = (uint8_t)((old_slot + 1) % OSAL_OTA_POSIX_PART_COUNT);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_last_valid_partition(old_slot));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_boot_partition(new_slot));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_partition_state(new_slot, OSAL_OTA_IMG_PENDING_VERIFY));
    osal_mqtt_event_notify_client_disconnected();
    osal_ota_event_loop_registration_info_t ota_registration_info = {
        .event_base = TEST_OSAL_OTA_EVENT,
        .event_ids = {
            .app_valid = TEST_OSAL_OTA_EVENT_APP_VALID,
        },
    };
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_event_loop_init(&ota_registration_info));
    g_test_last_posted_event_base = NULL;
    g_test_last_posted_event_id = 0;
    g_diag_calls = 0;
    g_diag_last_priv = NULL;
}

static void __end_pending_verify_boot(void)
{
    osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, esp_rmaker_ota_post_mqtt_diag_handler);
    if (g_rollback_timeout_handler != NULL) {
        rmaker_ota_timeout_handler_deinit(g_rollback_timeout_handler);
        g_rollback_timeout_handler = NULL;
    }
    g_post_mqtt_diag = NULL;
    g_post_mqtt_diag_priv = NULL;
    g_timer_init_fail = false;
    osal_ota_event_loop_init(NULL);
    osal_storage_reset((char *)RMAKER_NVS_PART_NAME);
    __set_all_slots_valid();
}

static void __post_mqtt_connected(void)
{
    esp_rmaker_ota_post_mqtt_diag_handler(NULL, RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, NULL);
}

void test_rollback_null_diag_waits_for_mqtt_before_valid(void)
{
    __begin_pending_verify_boot();
    esp_rmaker_ota_config_t config = { 0 };

    esp_rmaker_ota_handle_reboot(&config);

    TEST_ASSERT_TRUE(esp_rmaker_ota_partition_running_is_pending_verify());
    TEST_ASSERT_NOT_NULL(g_rollback_timeout_handler);
    TEST_ASSERT_NOT_EQUAL(__boot_slot(), __last_valid_slot());
    TEST_ASSERT_FALSE(g_test_last_posted_event_base == TEST_OSAL_OTA_EVENT);
    __end_pending_verify_boot();
}

void test_rollback_null_diag_without_mqtt_rolls_back(void)
{
    __begin_pending_verify_boot();
    uint8_t old_slot = 0;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_get_last_valid_partition(&old_slot));
    fflush(stdout);
    pid_t pid = fork();
    TEST_ASSERT_NOT_EQUAL(-1, pid);
    if (pid == 0) {
        esp_rmaker_ota_config_t config = { 0 };
        esp_rmaker_ota_handle_reboot(&config);
        esp_rmaker_ota_rollback_timeout_callback(NULL);
        _exit(0);
    }

    int status = 0;
    TEST_ASSERT_EQUAL(pid, waitpid(pid, &status, 0));
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL(POSIX_EXIT_REBOOT, WEXITSTATUS(status));
    TEST_ASSERT_EQUAL(old_slot, __boot_slot());
    TEST_ASSERT_TRUE(esp_rmaker_ota_partition_rollback_detected());
    __end_pending_verify_boot();
}

void test_rollback_nvs_failure_still_rolls_back(void)
{
    __begin_pending_verify_boot();
    uint8_t old_slot = 0;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_get_last_valid_partition(&old_slot));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_deinit((char *)RMAKER_NVS_PART_NAME));
    fflush(stdout);
    pid_t pid = fork();
    TEST_ASSERT_NOT_EQUAL(-1, pid);
    if (pid == 0) {
        esp_rmaker_ota_mark_invalid();
        _exit(0);
    }

    int status = 0;
    TEST_ASSERT_EQUAL(pid, waitpid(pid, &status, 0));
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL(POSIX_EXIT_REBOOT, WEXITSTATUS(status));
    TEST_ASSERT_EQUAL(old_slot, __boot_slot());
    __end_pending_verify_boot();
}

void test_rollback_null_diag_mqtt_connect_marks_valid(void)
{
    __begin_pending_verify_boot();
    esp_rmaker_ota_config_t config = { 0 };

    esp_rmaker_ota_handle_reboot(&config);
    __post_mqtt_connected();

    TEST_ASSERT_NULL(g_rollback_timeout_handler);
    TEST_ASSERT_EQUAL(__boot_slot(), __last_valid_slot());
    TEST_ASSERT_EQUAL_PTR(TEST_OSAL_OTA_EVENT, g_test_last_posted_event_base);
    TEST_ASSERT_EQUAL(TEST_OSAL_OTA_EVENT_APP_VALID, g_test_last_posted_event_id);
    __end_pending_verify_boot();
}

void test_rollback_timer_failure_still_marks_valid_on_mqtt(void)
{
    __begin_pending_verify_boot();
    g_timer_init_fail = true;
    esp_rmaker_ota_config_t config = { 0 };

    esp_rmaker_ota_handle_reboot(&config);
    TEST_ASSERT_NULL(g_rollback_timeout_handler);
    TEST_ASSERT_TRUE(esp_rmaker_ota_partition_running_is_pending_verify());

    __post_mqtt_connected();
    TEST_ASSERT_EQUAL(__boot_slot(), __last_valid_slot());
    __end_pending_verify_boot();
}

void test_rollback_post_mqtt_diag_receives_config_priv(void)
{
    __begin_pending_verify_boot();
    esp_rmaker_ota_config_t config = {
        .ota_diag = __diag_success,
        .priv = &g_diag_priv_marker,
    };

    esp_rmaker_ota_handle_reboot(&config);
    TEST_ASSERT_EQUAL(1, g_diag_calls);
    TEST_ASSERT_EQUAL(OTA_DIAG_STATE_INIT, g_diag_last_state);

    __post_mqtt_connected();
    TEST_ASSERT_EQUAL(2, g_diag_calls);
    TEST_ASSERT_EQUAL(OTA_DIAG_STATE_POST_MQTT, g_diag_last_state);
    TEST_ASSERT_EQUAL_PTR(&g_diag_priv_marker, g_diag_last_priv);
    TEST_ASSERT_NULL(g_post_mqtt_diag);
    TEST_ASSERT_NULL(g_post_mqtt_diag_priv);
    TEST_ASSERT_EQUAL(__boot_slot(), __last_valid_slot());
    TEST_ASSERT_EQUAL_PTR(TEST_OSAL_OTA_EVENT, g_test_last_posted_event_base);
    TEST_ASSERT_EQUAL(TEST_OSAL_OTA_EVENT_APP_VALID, g_test_last_posted_event_id);
    __end_pending_verify_boot();
}

void test_rollback_mqtt_already_connected_marks_valid(void)
{
    __begin_pending_verify_boot();
    osal_mqtt_event_notify_client_connected();
    esp_rmaker_ota_config_t config = { 0 };

    esp_rmaker_ota_handle_reboot(&config);

    TEST_ASSERT_NULL(g_rollback_timeout_handler);
    TEST_ASSERT_EQUAL(__boot_slot(), __last_valid_slot());
    __end_pending_verify_boot();
}

void test_rollback_post_mqtt_diag_runs_once_if_already_connected(void)
{
    __begin_pending_verify_boot();
    osal_mqtt_event_notify_client_connected();
    esp_rmaker_ota_config_t config = {
        .ota_diag = __diag_success,
        .priv = &g_diag_priv_marker,
    };

    esp_rmaker_ota_handle_reboot(&config);
    TEST_ASSERT_EQUAL(2, g_diag_calls);
    TEST_ASSERT_EQUAL(OTA_DIAG_STATE_POST_MQTT, g_diag_last_state);

    __post_mqtt_connected();
    TEST_ASSERT_EQUAL(2, g_diag_calls);
    __end_pending_verify_boot();
}
