/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_migration.c
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_rmaker_common_events.h"
#include "esp_rmaker_credentials.h"
#include "esp_rmaker_event_loop.h"
#include "credentials/migration.h"
#include "event_loop.h"
#include "util/esp_rmaker_crypto.h"

#include "osal_event_loop.h"
#include "osal_ota.h"
#include "osal_scheduler.h"
#include "osal_task.h"
#include "osal_ticks.h"

#ifndef ESP_PLATFORM
#include "osal_ota_posix_config.h"
#include "osal_ota_posix_shared.h"
#endif /* ESP_PLATFORM */

#define TEST_MIGRATION_TARGET_HOST "neo-target.example.com"
#define TEST_MIGRATION_OVERRIDE_HOST "app-override.example.com"
#define TEST_MIGRATION_TIMEOUT_S 1
#define TEST_MIGRATION_WAIT_MS 3000
#define TEST_MIGRATION_POLL_MS 10
#define TEST_MIGRATION_EVENT_FLUSH 0

/* Private functions **************************************************/

static esp_rmaker_error_t __test_override_mqtt_host(char **p_str)
{
    *p_str = strdup(TEST_MIGRATION_OVERRIDE_HOST);
    return *p_str ? ESP_RMAKER_OK : ESP_RMAKER_NO_MEM;
}

/**
 * @brief Clear the migration and restore the default credentials providers.
 */
static void __test_migration_cleanup(void)
{
    esp_rmaker_migration_reset();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_reset_to_default());
}

/**
 * @brief Get the current MQTT host through the credentials providers.
 * @note The caller must free the result.
 */
static char *__test_get_mqtt_host(void)
{
    char *host = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_get_mqtt_host(&host));
    return host;
}

/**
 * @brief Remove the trailing NUL that the getters add to a PEM credential.
 */
static size_t __test_stored_len(const esp_rmaker_credential_t *p_credential)
{
    size_t len = p_credential->len;
    if (len > 0 && p_credential->credential[len - 1] == '\0') {
        len--;
    }
    return len;
}

/**
 * @brief The claim credentials that a test erases and restores.
 */
typedef struct {
    char *client_id;
    char *mqtt_host;
    esp_rmaker_credential_t cert;
    esp_rmaker_credential_t key;
} __test_saved_credentials_t;

static void __test_save_and_erase(__test_saved_credentials_t *p_saved)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_get_thing_name(&p_saved->client_id));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_get_mqtt_host(&p_saved->mqtt_host));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_get_client_cert(&p_saved->cert));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_get_private_key(&p_saved->key));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_erase_claim_data());
}

/**
 * @brief Restore the saved claim credentials, clear the migration, and restore the default providers.
 * @note Has no asserts, so it can run after a failed assert. Returns true on success.
 */
static bool __test_restore(__test_saved_credentials_t *p_saved)
{
    bool ok = esp_rmaker_credentials_set_client_id(p_saved->client_id) == ESP_RMAKER_OK;
    ok &= esp_rmaker_credentials_set_mqtt_host(p_saved->mqtt_host) == ESP_RMAKER_OK;
    ok &= esp_rmaker_credentials_set_client_cert(p_saved->cert.credential, __test_stored_len(&p_saved->cert)) == ESP_RMAKER_OK;
    ok &= esp_rmaker_credentials_set_client_key(p_saved->key.credential, __test_stored_len(&p_saved->key)) == ESP_RMAKER_OK;
    free(p_saved->client_id);
    free(p_saved->mqtt_host);
    esp_rmaker_credentials_free_credential(&p_saved->cert);
    esp_rmaker_credentials_free_credential(&p_saved->key);
    esp_rmaker_migration_reset();
    ok &= esp_rmaker_credentials_reset_to_default() == ESP_RMAKER_OK;
    return ok;
}

/* Public function definitions ****************************************************/

void test_migration_rejects_empty_target(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_migration_check(NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_migration_check(""));
    TEST_ASSERT_FALSE((esp_rmaker_migration_get_state() & RMAKER_MIGRATION_WRITE_MQTT_HOST));
}

void test_migration_same_host_does_nothing(void)
{
    char *stored_host = __test_get_mqtt_host();

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(stored_host));
    TEST_ASSERT_FALSE((esp_rmaker_migration_get_state() & RMAKER_MIGRATION_WRITE_MQTT_HOST));

    __test_migration_cleanup();
    free(stored_host);
}

void test_migration_new_host_in_memory_only(void)
{
    char *stored_host = __test_get_mqtt_host();
    TEST_ASSERT_NOT_EQUAL(0, strcmp(stored_host, TEST_MIGRATION_TARGET_HOST));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
    TEST_ASSERT_TRUE((esp_rmaker_migration_get_state() & RMAKER_MIGRATION_WRITE_MQTT_HOST));
    char *host = __test_get_mqtt_host();
    TEST_ASSERT_EQUAL_STRING(TEST_MIGRATION_TARGET_HOST, host);
    free(host);

    /* A second check after a node deinit keeps the first result. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
    TEST_ASSERT_TRUE((esp_rmaker_migration_get_state() & RMAKER_MIGRATION_WRITE_MQTT_HOST));

    /* The default provider still reads the old host, so nothing was written. */
    __test_migration_cleanup();
    host = __test_get_mqtt_host();
    TEST_ASSERT_EQUAL_STRING(stored_host, host);
    free(host);
    free(stored_host);
}

void test_migration_app_override_is_stored_host(void)
{
    esp_rmaker_credentials_providers_t providers = { .mqtt_host = __test_override_mqtt_host };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_override(&providers));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_OVERRIDE_HOST));
    TEST_ASSERT_FALSE((esp_rmaker_migration_get_state() & RMAKER_MIGRATION_WRITE_MQTT_HOST));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
    TEST_ASSERT_TRUE((esp_rmaker_migration_get_state() & RMAKER_MIGRATION_WRITE_MQTT_HOST));
    char *host = __test_get_mqtt_host();
    TEST_ASSERT_EQUAL_STRING(TEST_MIGRATION_TARGET_HOST, host);
    free(host);

    __test_migration_cleanup();
}

void test_migration_stored_node_id_needs_no_write(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
    esp_rmaker_migration_state_t state = esp_rmaker_migration_get_state();
    TEST_ASSERT_FALSE((state & RMAKER_MIGRATION_WRITE_CLIENT_ID));
    TEST_ASSERT_TRUE((state & RMAKER_MIGRATION_WRITE_MQTT_HOST));

    __test_migration_cleanup();
}

void test_migration_cert_cn_needs_write(void)
{
    __test_saved_credentials_t saved = { 0 };
    __test_save_and_erase(&saved);
    char *cn = NULL;
    esp_rmaker_error_t cn_err = esp_rmaker_crypto_get_cert_cn(saved.cert.credential, saved.cert.len, &cn);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_set_client_cert(saved.cert.credential, __test_stored_len(&saved.cert)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_set_mqtt_host(saved.mqtt_host));

    esp_rmaker_error_t err = esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST);
    esp_rmaker_migration_state_t state = esp_rmaker_migration_get_state();
    char *client_id = NULL;
    esp_rmaker_error_t id_err = esp_rmaker_credentials_get_thing_name(&client_id);

    TEST_ASSERT_TRUE(__test_restore(&saved));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, cn_err);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_TRUE((state & RMAKER_MIGRATION_WRITE_CLIENT_ID));
    TEST_ASSERT_TRUE((state & RMAKER_MIGRATION_WRITE_MQTT_HOST));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, id_err);
    TEST_ASSERT_EQUAL_STRING(cn, client_id);
    free(client_id);
    free(cn);
}

void test_migration_no_node_id_no_cert_fails(void)
{
    __test_saved_credentials_t saved = { 0 };
    __test_save_and_erase(&saved);

    esp_rmaker_error_t err = esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST);
    esp_rmaker_migration_state_t state = esp_rmaker_migration_get_state();

    TEST_ASSERT_TRUE(__test_restore(&saved));
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, err);
    TEST_ASSERT_FALSE((state & RMAKER_MIGRATION_WRITE_CLIENT_ID));
    TEST_ASSERT_FALSE((state & RMAKER_MIGRATION_WRITE_MQTT_HOST));
}
/* Write-back and timeout flows *****************************************************/

OSAL_EVENT_DEFINE_BASE(TEST_MIGRATION_EVENT);

static volatile int s_flush_count = 0;
static volatile int s_internal_timeout_count = 0;
static volatile int s_public_timeout_count = 0;

static void __test_flow_handler(void *arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == TEST_MIGRATION_EVENT) {
        s_flush_count++;
    } else if (event_base == RMAKER_INTERNAL_EVENT && event_id == RMAKER_INTERNAL_EVENT_MIGRATION_TIMEOUT) {
        s_internal_timeout_count++;
    } else if (event_base == RMAKER_EVENT && event_id == RMAKER_EVENT_MQTT_HOST_MIGRATION_TIMEOUT) {
        s_public_timeout_count++;
    }
}

static bool __test_wait_for_count(volatile int *p_count, int target)
{
    for (int waited_ms = 0; *p_count < target && waited_ms < TEST_MIGRATION_WAIT_MS; waited_ms += TEST_MIGRATION_POLL_MS) {
        osal_task_delay(osal_ticks_from_ms(TEST_MIGRATION_POLL_MS));
    }
    return *p_count >= target;
}

/**
 * @brief Wait until the event loop handled all the events posted before this call.
 */
static void __test_flush_events(void)
{
    int target = s_flush_count + 1;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_event_post(TEST_MIGRATION_EVENT, TEST_MIGRATION_EVENT_FLUSH, NULL, 0, OSAL_MAX_DELAY));
    TEST_ASSERT_TRUE(__test_wait_for_count(&s_flush_count, target));
}

static void __test_post_mqtt_connected(void)
{
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_event_post(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, NULL, 0, OSAL_MAX_DELAY));
    __test_flush_events();
}

/**
 * @brief Start the target host timeout, and wait until the migration handled its expiry.
 */
static void __test_expire_timeout(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_on_network_up(TEST_MIGRATION_TIMEOUT_S));
    TEST_ASSERT_TRUE(__test_wait_for_count(&s_internal_timeout_count, 1));
    __test_flush_events();
}

/**
 * @brief Start the event loop and the scheduler, and register the event counters.
 */
static void __test_begin_boot(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_loop_init(__test_flow_handler));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_init());
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_event_handler_register(TEST_MIGRATION_EVENT, TEST_MIGRATION_EVENT_FLUSH, __test_flow_handler, NULL));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_event_handler_register(RMAKER_EVENT, RMAKER_EVENT_MQTT_HOST_MIGRATION_TIMEOUT, __test_flow_handler, NULL));
    s_flush_count = 0;
    s_internal_timeout_count = 0;
    s_public_timeout_count = 0;
}

/**
 * @brief Start the write-back. Call after ::__test_begin_boot and a successful check.
 * @note Registers the timeout counter after the migration handler, so the counter runs last.
 */
static void __test_start(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_start());
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_event_handler_register(RMAKER_INTERNAL_EVENT, RMAKER_INTERNAL_EVENT_MIGRATION_TIMEOUT, __test_flow_handler, NULL));
}

#ifndef ESP_PLATFORM
/* The rollback stubs count here, and do not reboot. */
volatile int g_test_migration_rollback_count = 0;

#ifndef __APPLE__
osal_err_t __wrap_osal_ota_mark_app_invalid_rollback_and_reboot(void);

osal_err_t __wrap_osal_ota_mark_app_invalid_rollback_and_reboot(void)
{
    g_test_migration_rollback_count++;
    return OSAL_ERR_OK;
}
#endif /* __APPLE__ */

static bool s_slots_staged = false;
static uint8_t s_saved_boot_slot = 0;
static uint8_t s_saved_last_valid_slot = 0;

static uint8_t __test_boot_slot(void)
{
    uint8_t boot_idx = 0;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_get_boot_partition(&boot_idx));
    return boot_idx;
}

static void __test_set_all_slots_valid(void)
{
    for (uint8_t i = 0; i < OSAL_OTA_POSIX_PART_COUNT; i++) {
        osal_ota_posix_config_set_partition_state(i, OSAL_OTA_IMG_VALID);
    }
}

/**
 * @brief Boot a new image in the next slot that waits for verification.
 */
static void __test_stage_pending_verify(void)
{
    s_saved_boot_slot = __test_boot_slot();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_get_last_valid_partition(&s_saved_last_valid_slot));
    s_slots_staged = true;
    __test_set_all_slots_valid();
    uint8_t new_slot = (uint8_t)((s_saved_boot_slot + 1) % OSAL_OTA_POSIX_PART_COUNT);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_last_valid_partition(s_saved_boot_slot));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_boot_partition(new_slot));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_partition_state(new_slot, OSAL_OTA_IMG_PENDING_VERIFY));
}
#endif /* ESP_PLATFORM */

/**
 * @brief Undo ::__test_begin_boot and the migration, and store the MQTT host if not NULL.
 * @note Has no asserts, so it can run after a failed assert. Returns true on success.
 */
static bool __test_end_boot(const char *stored_host)
{
    esp_rmaker_migration_reset();
    bool ok = esp_rmaker_credentials_reset_to_default() == ESP_RMAKER_OK;
    osal_event_handler_unregister(TEST_MIGRATION_EVENT, TEST_MIGRATION_EVENT_FLUSH, __test_flow_handler);
    osal_event_handler_unregister(RMAKER_EVENT, RMAKER_EVENT_MQTT_HOST_MIGRATION_TIMEOUT, __test_flow_handler);
    osal_event_handler_unregister(RMAKER_INTERNAL_EVENT, RMAKER_INTERNAL_EVENT_MIGRATION_TIMEOUT, __test_flow_handler);
    event_loop_deinit();
#ifndef ESP_PLATFORM
    if (s_slots_staged) {
        __test_set_all_slots_valid();
        osal_ota_posix_config_set_last_valid_partition(s_saved_last_valid_slot);
        osal_ota_posix_config_set_boot_partition(s_saved_boot_slot);
        s_slots_staged = false;
    }
#endif /* ESP_PLATFORM */
    if (stored_host) {
        ok &= esp_rmaker_credentials_set_mqtt_host(stored_host) == ESP_RMAKER_OK;
    }
    return ok;
}

/**
 * @brief Clear the in-memory migration, as a reboot does, and read the stored MQTT host.
 * @note The caller must free the result.
 */
static char *__test_reboot_and_get_mqtt_host(void)
{
    __test_migration_cleanup();
    return __test_get_mqtt_host();
}

void test_migration_flow_valid_connect_writes_back(void)
{
    __test_saved_credentials_t saved = { 0 };
    __test_save_and_erase(&saved);
    char *cn = NULL;
    char *host = NULL;
    char *client_id = NULL;
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_crypto_get_cert_cn(saved.cert.credential, saved.cert.len, &cn));
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_set_client_cert(saved.cert.credential, __test_stored_len(&saved.cert)));
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_set_mqtt_host(saved.mqtt_host));
        __test_begin_boot();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        __test_start();
        TEST_ASSERT_EQUAL(RMAKER_MIGRATION_WRITE_ANY, esp_rmaker_migration_get_state());

        __test_post_mqtt_connected();
        TEST_ASSERT_EQUAL(0, esp_rmaker_migration_get_state());
        host = __test_reboot_and_get_mqtt_host();
        TEST_ASSERT_EQUAL_STRING(TEST_MIGRATION_TARGET_HOST, host);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_credentials_get_thing_name(&client_id));
        TEST_ASSERT_EQUAL_STRING(cn, client_id);

        /* The next boot finds the node migrated. */
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        TEST_ASSERT_EQUAL(0, esp_rmaker_migration_get_state());
    }
    bool restored = __test_end_boot(NULL);
    restored &= __test_restore(&saved);
    /* A new frame: after a failure, every assert aborts, and the old frame runs the cleanup again. */
    if (TEST_PROTECT()) {
        TEST_ASSERT_TRUE(restored);
    }
    free(cn);
    free(host);
    free(client_id);
}

void test_migration_flow_valid_no_connect_times_out(void)
{
    char *stored_host = __test_get_mqtt_host();
    char *host = NULL;
    if (TEST_PROTECT()) {
        __test_begin_boot();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        __test_start();

        __test_expire_timeout();
        TEST_ASSERT_EQUAL(1, s_public_timeout_count);
        TEST_ASSERT_EQUAL(RMAKER_MIGRATION_WRITE_MQTT_HOST, esp_rmaker_migration_get_state());

        /* The node keeps the target host, so a late connection still writes back. */
        host = __test_get_mqtt_host();
        TEST_ASSERT_EQUAL_STRING(TEST_MIGRATION_TARGET_HOST, host);
        free(host);
        host = NULL;
        __test_post_mqtt_connected();
        TEST_ASSERT_EQUAL(0, esp_rmaker_migration_get_state());
        host = __test_reboot_and_get_mqtt_host();
        TEST_ASSERT_EQUAL_STRING(TEST_MIGRATION_TARGET_HOST, host);
    }
    bool restored = __test_end_boot(stored_host);
    if (TEST_PROTECT()) {
        TEST_ASSERT_TRUE(restored);
    }
    free(host);
    free(stored_host);
}

void test_migration_flow_connect_cancels_timeout(void)
{
    char *stored_host = __test_get_mqtt_host();
    if (TEST_PROTECT()) {
        __test_begin_boot();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        __test_start();
        /* A longer timeout, so the connection comes first. */
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_on_network_up(TEST_MIGRATION_TIMEOUT_S * 2));
        __test_post_mqtt_connected();
        TEST_ASSERT_EQUAL(0, esp_rmaker_migration_get_state());

        TEST_ASSERT_FALSE(__test_wait_for_count(&s_internal_timeout_count, 1));
        TEST_ASSERT_EQUAL(0, s_public_timeout_count);
    }
    bool restored = __test_end_boot(stored_host);
    if (TEST_PROTECT()) {
        TEST_ASSERT_TRUE(restored);
    }
    free(stored_host);
}

void test_migration_flow_reboot_before_connect_retries(void)
{
    char *stored_host = __test_get_mqtt_host();
    char *host = NULL;
    if (TEST_PROTECT()) {
        __test_begin_boot();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        __test_start();

        /* A reboot before the first connection: nothing is stored. */
        host = __test_reboot_and_get_mqtt_host();
        TEST_ASSERT_EQUAL_STRING(stored_host, host);
        free(host);
        host = NULL;

        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        TEST_ASSERT_EQUAL(RMAKER_MIGRATION_WRITE_MQTT_HOST, esp_rmaker_migration_get_state());
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_start());
        __test_post_mqtt_connected();
        TEST_ASSERT_EQUAL(0, esp_rmaker_migration_get_state());
        host = __test_reboot_and_get_mqtt_host();
        TEST_ASSERT_EQUAL_STRING(TEST_MIGRATION_TARGET_HOST, host);
    }
    bool restored = __test_end_boot(stored_host);
    if (TEST_PROTECT()) {
        TEST_ASSERT_TRUE(restored);
    }
    free(host);
    free(stored_host);
}

#ifndef ESP_PLATFORM
void test_migration_flow_pending_connect_writes_back(void)
{
    char *stored_host = __test_get_mqtt_host();
    char *host = NULL;
    if (TEST_PROTECT()) {
        __test_begin_boot();
        __test_stage_pending_verify();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        __test_start();

        /* The image is still pending verify, so the connection alone does not write. */
        __test_post_mqtt_connected();
        TEST_ASSERT_EQUAL(RMAKER_MIGRATION_WRITE_MQTT_HOST, esp_rmaker_migration_get_state());

        TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_mark_app_valid_cancel_rollback());
        __test_flush_events();
        TEST_ASSERT_EQUAL(0, esp_rmaker_migration_get_state());
        host = __test_reboot_and_get_mqtt_host();
        TEST_ASSERT_EQUAL_STRING(TEST_MIGRATION_TARGET_HOST, host);
    }
    bool restored = __test_end_boot(stored_host);
    if (TEST_PROTECT()) {
        TEST_ASSERT_TRUE(restored);
    }
    free(host);
    free(stored_host);
}

void test_migration_flow_pending_no_connect_rolls_back(void)
{
    char *stored_host = __test_get_mqtt_host();
    char *host = NULL;
    if (TEST_PROTECT()) {
        __test_begin_boot();
        __test_stage_pending_verify();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        __test_start();

        g_test_migration_rollback_count = 0;

        /* The migration rolls back a pending image, and posts no timeout event. */
        __test_expire_timeout();
        TEST_ASSERT_EQUAL(1, g_test_migration_rollback_count);
        TEST_ASSERT_EQUAL(0, s_public_timeout_count);
        TEST_ASSERT_EQUAL(RMAKER_MIGRATION_WRITE_MQTT_HOST, esp_rmaker_migration_get_state());

        host = __test_reboot_and_get_mqtt_host();
        TEST_ASSERT_EQUAL_STRING(stored_host, host);
    }
    bool restored = __test_end_boot(stored_host);
    if (TEST_PROTECT()) {
        TEST_ASSERT_TRUE(restored);
    }
    free(host);
    free(stored_host);
}

void test_migration_flow_valid_after_start_no_connect_times_out(void)
{
    char *stored_host = __test_get_mqtt_host();
    if (TEST_PROTECT()) {
        __test_begin_boot();
        __test_stage_pending_verify();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        __test_start();
        g_test_migration_rollback_count = 0;

        /* The app marks the image valid before the timeout, so the migration does not roll back. */
        TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_mark_app_valid_cancel_rollback());
        __test_expire_timeout();
        TEST_ASSERT_EQUAL(0, g_test_migration_rollback_count);
        TEST_ASSERT_EQUAL(1, s_public_timeout_count);
        TEST_ASSERT_EQUAL(RMAKER_MIGRATION_WRITE_MQTT_HOST, esp_rmaker_migration_get_state());
    }
    bool restored = __test_end_boot(stored_host);
    if (TEST_PROTECT()) {
        TEST_ASSERT_TRUE(restored);
    }
    free(stored_host);
}

void test_migration_flow_raw_mark_valid_connect_writes_back(void)
{
    char *stored_host = __test_get_mqtt_host();
    char *host = NULL;
    if (TEST_PROTECT()) {
        __test_begin_boot();
        __test_stage_pending_verify();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        __test_start();

        /* Mark the image valid without the OSAL call, so no event is posted. */
        TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_posix_config_set_partition_state(__test_boot_slot(), OSAL_OTA_IMG_VALID));
        __test_flush_events();
        TEST_ASSERT_EQUAL(RMAKER_MIGRATION_WRITE_MQTT_HOST, esp_rmaker_migration_get_state());

        __test_post_mqtt_connected();
        TEST_ASSERT_EQUAL(0, esp_rmaker_migration_get_state());
        host = __test_reboot_and_get_mqtt_host();
        TEST_ASSERT_EQUAL_STRING(TEST_MIGRATION_TARGET_HOST, host);
    }
    bool restored = __test_end_boot(stored_host);
    if (TEST_PROTECT()) {
        TEST_ASSERT_TRUE(restored);
    }
    free(host);
    free(stored_host);
}

void test_migration_flow_power_loss_after_mark_valid_retries(void)
{
    char *stored_host = __test_get_mqtt_host();
    char *host = NULL;
    if (TEST_PROTECT()) {
        __test_begin_boot();
        __test_stage_pending_verify();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        __test_start();
        TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_ota_mark_app_valid_cancel_rollback());
        __test_flush_events();
        TEST_ASSERT_EQUAL(RMAKER_MIGRATION_WRITE_MQTT_HOST, esp_rmaker_migration_get_state());

        /* Power loss before the first connection: nothing is stored. */
        host = __test_reboot_and_get_mqtt_host();
        TEST_ASSERT_EQUAL_STRING(stored_host, host);
        free(host);
        host = NULL;

        /* The next boot runs a valid image and migrates again. */
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_check(TEST_MIGRATION_TARGET_HOST));
        TEST_ASSERT_EQUAL(RMAKER_MIGRATION_WRITE_MQTT_HOST, esp_rmaker_migration_get_state());
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_migration_start());
        __test_post_mqtt_connected();
        TEST_ASSERT_EQUAL(0, esp_rmaker_migration_get_state());
        host = __test_reboot_and_get_mqtt_host();
        TEST_ASSERT_EQUAL_STRING(TEST_MIGRATION_TARGET_HOST, host);
    }
    bool restored = __test_end_boot(stored_host);
    if (TEST_PROTECT()) {
        TEST_ASSERT_TRUE(restored);
    }
    free(host);
    free(stored_host);
}
#endif /* ESP_PLATFORM */
