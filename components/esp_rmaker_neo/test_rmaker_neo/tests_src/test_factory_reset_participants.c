/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_factory_reset_participants.c
 * @brief Tests for factory reset participants.
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <stddef.h>
#include <string.h>

#include "esp_rmaker_system_ctrl.h"
#include "system_ctrl_internal.h"

/* Recording *******************************************************************/

#define WIPE_LOG_MAX 8

static const char *__wipe_log[WIPE_LOG_MAX];
static uint8_t __wipe_count;
static esp_rmaker_error_t __wipe_result[WIPE_LOG_MAX];

static esp_rmaker_error_t __record(const char *name)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    if (__wipe_count < WIPE_LOG_MAX) {
        __wipe_log[__wipe_count] = name;
        err = __wipe_result[__wipe_count];
        __wipe_count++;
    }
    return err;
}

static esp_rmaker_error_t __wipe_a(void *priv)
{
    (void) priv;
    return __record("a");
}

static esp_rmaker_error_t __wipe_b(void *priv)
{
    (void) priv;
    return __record("b");
}

static esp_rmaker_error_t __wipe_c(void *priv)
{
    (void) priv;
    return __record("c");
}

static esp_rmaker_error_t __wipe_d(void *priv)
{
    (void) priv;
    return __record("d");
}

static esp_rmaker_error_t __wipe_e(void *priv)
{
    (void) priv;
    return __record("e");
}

static esp_rmaker_error_t __fake_network_reset(void)
{
    return ESP_RMAKER_OK;
}

static esp_rmaker_system_ctrl_factory_reset_wipe_fn_t __all_wipes[] = { __wipe_a, __wipe_b, __wipe_c, __wipe_d, __wipe_e };

static void __setup(void)
{
    memset(__wipe_log, 0, sizeof(__wipe_log));
    memset(__wipe_result, 0, sizeof(__wipe_result));
    __wipe_count = 0;
}

static void __teardown(void)
{
    for (size_t i = 0; i < sizeof(__all_wipes) / sizeof(__all_wipes[0]); i++) {
        (void) esp_rmaker_system_ctrl_factory_reset_participant_unregister(__all_wipes[i]);
    }
}

static esp_rmaker_system_ctrl_factory_reset_participant_t __participant(const char *name, esp_rmaker_system_ctrl_factory_reset_wipe_fn_t wipe,
        bool reboots_on_wipe)
{
    esp_rmaker_system_ctrl_factory_reset_participant_t participant = {
        .name = name,
        .reboots_on_wipe = reboots_on_wipe,
        .wipe = wipe,
        .priv = NULL,
    };
    return participant;
}

/* reset_reboot_s = -1 keeps the reboot out of the test: on POSIX it would exit the process. */
static esp_rmaker_error_t __factory_reset(void)
{
    return esp_rmaker_system_ctrl_factory_reset(0, -1, __fake_network_reset);
}

/* Registration ****************************************************************/

void test_factory_reset_participant_register_arg_validation(void)
{
    __setup();

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_system_ctrl_factory_reset_participant_register(NULL));

    esp_rmaker_system_ctrl_factory_reset_participant_t no_wipe = __participant("no_wipe", NULL, false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_system_ctrl_factory_reset_participant_register(&no_wipe));

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_system_ctrl_factory_reset_participant_unregister(NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_system_ctrl_factory_reset_participant_unregister(__wipe_a));

    __teardown();
}

void test_factory_reset_participant_register_rejects_second_rebooting(void)
{
    __setup();

    esp_rmaker_system_ctrl_factory_reset_participant_t first = __participant("first", __wipe_a, true);
    esp_rmaker_system_ctrl_factory_reset_participant_t second = __participant("second", __wipe_b, true);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&first));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, esp_rmaker_system_ctrl_factory_reset_participant_register(&second));

    /* The rejected one must not have been stored. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_system_ctrl_factory_reset_participant_unregister(__wipe_b));

    __teardown();
}

/* Distinct wipe functions to fill the table at any configured size: a participant is keyed by its wipe. */
#define __TABLE_WIPE(n) \
    static esp_rmaker_error_t __table_wipe_##n(void *priv) { (void) priv; return ESP_RMAKER_OK; }
__TABLE_WIPE(0) __TABLE_WIPE(1) __TABLE_WIPE(2) __TABLE_WIPE(3) __TABLE_WIPE(4) __TABLE_WIPE(5)
__TABLE_WIPE(6) __TABLE_WIPE(7) __TABLE_WIPE(8) __TABLE_WIPE(9) __TABLE_WIPE(10) __TABLE_WIPE(11)
__TABLE_WIPE(12) __TABLE_WIPE(13) __TABLE_WIPE(14) __TABLE_WIPE(15) __TABLE_WIPE(16)

static esp_rmaker_system_ctrl_factory_reset_wipe_fn_t __table_wipes[] = {
    __table_wipe_0, __table_wipe_1, __table_wipe_2, __table_wipe_3, __table_wipe_4, __table_wipe_5,
    __table_wipe_6, __table_wipe_7, __table_wipe_8, __table_wipe_9, __table_wipe_10, __table_wipe_11,
    __table_wipe_12, __table_wipe_13, __table_wipe_14, __table_wipe_15, __table_wipe_16,
};

#define __TABLE_MAX CONFIG_ESP_RMAKER_NEO_SYSTEM_CTRL_RESET_PARTICIPANTS_MAX
_Static_assert(__TABLE_MAX < sizeof(__table_wipes) / sizeof(__table_wipes[0]),
               "one more wipe than the participant limit is needed to overflow the table");

void test_factory_reset_participant_register_table_full(void)
{
    __setup();

    esp_rmaker_system_ctrl_factory_reset_participant_t p;
    for (size_t i = 0; i < __TABLE_MAX; i++) {
        p = __participant("p", __table_wipes[i], false);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&p));
    }
    p = __participant("overflow", __table_wipes[__TABLE_MAX], false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_NO_MEM, esp_rmaker_system_ctrl_factory_reset_participant_register(&p));

    for (size_t i = 0; i < __TABLE_MAX; i++) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_unregister(__table_wipes[i]));
    }
    __teardown();
}

void test_factory_reset_participant_reregister_updates_in_place(void)
{
    __setup();

    esp_rmaker_system_ctrl_factory_reset_participant_t p = __participant("a", __wipe_a, false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&p));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&p));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __factory_reset());
    TEST_ASSERT_EQUAL_UINT8(1, __wipe_count);

    /* A single entry, so a single unregister clears it. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_unregister(__wipe_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_system_ctrl_factory_reset_participant_unregister(__wipe_a));

    __teardown();
}

/* Wipe sequencing *************************************************************/

void test_factory_reset_participants_wiped_in_registration_order(void)
{
    __setup();

    esp_rmaker_system_ctrl_factory_reset_participant_t a = __participant("a", __wipe_a, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t b = __participant("b", __wipe_b, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t c = __participant("c", __wipe_c, false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&b));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&c));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __factory_reset());

    TEST_ASSERT_EQUAL_UINT8(3, __wipe_count);
    TEST_ASSERT_EQUAL_STRING("a", __wipe_log[0]);
    TEST_ASSERT_EQUAL_STRING("b", __wipe_log[1]);
    TEST_ASSERT_EQUAL_STRING("c", __wipe_log[2]);

    __teardown();
}

void test_factory_reset_participants_rebooting_wiped_last(void)
{
    __setup();

    /* Registered first, so only the reboots_on_wipe handling can move it to the end. */
    esp_rmaker_system_ctrl_factory_reset_participant_t rebooting = __participant("rebooting", __wipe_a, true);
    esp_rmaker_system_ctrl_factory_reset_participant_t b = __participant("b", __wipe_b, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t c = __participant("c", __wipe_c, false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&rebooting));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&b));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&c));

    /* The rebooting participant's wipe returned instead of restarting: logged, not an error. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __factory_reset());

    TEST_ASSERT_EQUAL_UINT8(3, __wipe_count);
    TEST_ASSERT_EQUAL_STRING("b", __wipe_log[0]);
    TEST_ASSERT_EQUAL_STRING("c", __wipe_log[1]);
    TEST_ASSERT_EQUAL_STRING("a", __wipe_log[2]);

    __teardown();
}

void test_factory_reset_participants_wipe_failure_does_not_stop_others(void)
{
    __setup();

    esp_rmaker_system_ctrl_factory_reset_participant_t a = __participant("a", __wipe_a, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t b = __participant("b", __wipe_b, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t c = __participant("c", __wipe_c, false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&b));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&c));

    /* Two failures with different codes: the reset reports the first. */
    __wipe_result[1] = ESP_RMAKER_FAIL;
    __wipe_result[2] = ESP_RMAKER_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, __factory_reset());

    TEST_ASSERT_EQUAL_UINT8(3, __wipe_count);
    TEST_ASSERT_EQUAL_STRING("c", __wipe_log[2]);

    __teardown();
}

void test_factory_reset_from_participant_skips_only_the_originator(void)
{
    __setup();

    esp_rmaker_system_ctrl_factory_reset_participant_t a = __participant("a", __wipe_a, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t b = __participant("b", __wipe_b, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t c = __participant("c", __wipe_c, false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&b));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&c));

    /* 'b' started the reset, so it is already wiping its own state; the other two must still run. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_from_participant(__wipe_b, __fake_network_reset, -1));

    TEST_ASSERT_EQUAL_UINT8(2, __wipe_count);
    TEST_ASSERT_EQUAL_STRING("a", __wipe_log[0]);
    TEST_ASSERT_EQUAL_STRING("c", __wipe_log[1]);

    __teardown();
}

void test_factory_reset_from_participant_null_self_wipes_all(void)
{
    __setup();

    esp_rmaker_system_ctrl_factory_reset_participant_t a = __participant("a", __wipe_a, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t b = __participant("b", __wipe_b, false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&b));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_from_participant(NULL, __fake_network_reset, -1));

    TEST_ASSERT_EQUAL_UINT8(2, __wipe_count);

    __teardown();
}

/* Re-entry ********************************************************************/

/* Stands in for a participant whose own wipe restarts the external stack, which reports the reset
 * back to the SDK before this call returns. */
static esp_rmaker_error_t __wipe_reentrant(void *priv)
{
    (void) priv;
    esp_rmaker_error_t err = __record("reentrant");
    /* Ignored, and reported as such. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, esp_rmaker_system_ctrl_factory_reset_from_participant(__wipe_reentrant, __fake_network_reset, -1));
    return err;
}

void test_factory_reset_reentrant_request_is_a_noop(void)
{
    __setup();

    esp_rmaker_system_ctrl_factory_reset_participant_t reentrant = __participant("reentrant", __wipe_reentrant, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t b = __participant("b", __wipe_b, false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&reentrant));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&b));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __factory_reset());

    /* One wipe each: the nested request must not restart the sweep. */
    TEST_ASSERT_EQUAL_UINT8(2, __wipe_count);
    TEST_ASSERT_EQUAL_STRING("reentrant", __wipe_log[0]);
    TEST_ASSERT_EQUAL_STRING("b", __wipe_log[1]);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_unregister(__wipe_reentrant));
    __teardown();
}

void test_factory_reset_participants_unregister_preserves_order(void)
{
    __setup();

    esp_rmaker_system_ctrl_factory_reset_participant_t a = __participant("a", __wipe_a, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t b = __participant("b", __wipe_b, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t c = __participant("c", __wipe_c, false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&b));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&c));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_unregister(__wipe_b));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __factory_reset());

    TEST_ASSERT_EQUAL_UINT8(2, __wipe_count);
    TEST_ASSERT_EQUAL_STRING("a", __wipe_log[0]);
    TEST_ASSERT_EQUAL_STRING("c", __wipe_log[1]);

    __teardown();
}

void test_factory_reset_runs_again_after_a_run_without_reboot(void)
{
    __setup();

    esp_rmaker_system_ctrl_factory_reset_participant_t a = __participant("a", __wipe_a, false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&a));

    /* reset_reboot_s < 0: nothing restarts the node, so the in-progress flag must clear. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __factory_reset());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __factory_reset());

    TEST_ASSERT_EQUAL_UINT8(2, __wipe_count);

    __teardown();
}

/* Extra wipe ******************************************************************/

static esp_rmaker_error_t __wipe_extra(void *priv)
{
    *(int *) priv += 1;
    return __record("extra");
}

void test_factory_reset_with_wipe_runs_extra_before_participants(void)
{
    __setup();

    esp_rmaker_system_ctrl_factory_reset_participant_t a = __participant("a", __wipe_a, false);
    esp_rmaker_system_ctrl_factory_reset_participant_t b = __participant("b", __wipe_b, true);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&b));

    /* with_wipe uses the registered network reset; without one, that step's error would come first. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_register_network_reset_fn(__fake_network_reset));

    int extra_runs = 0;
    __wipe_result[0] = ESP_RMAKER_FAIL;
    esp_rmaker_error_t err = esp_rmaker_system_ctrl_factory_reset_with_wipe(__wipe_extra, &extra_runs, -1);
    (void) esp_rmaker_system_ctrl_register_network_reset_fn(NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, err);

    /* Extra first, so it still runs when the last participant restarts the node; its error surfaces. */
    TEST_ASSERT_EQUAL_INT(1, extra_runs);
    TEST_ASSERT_EQUAL_UINT8(3, __wipe_count);
    TEST_ASSERT_EQUAL_STRING("extra", __wipe_log[0]);
    TEST_ASSERT_EQUAL_STRING("a", __wipe_log[1]);
    TEST_ASSERT_EQUAL_STRING("b", __wipe_log[2]);

    __teardown();
}

static int __nested_extra_runs;

static esp_rmaker_error_t __wipe_starts_clear_claim(void *priv)
{
    (void) priv;
    (void) __record("starter");
    /* A reset requested from inside a running one is ignored, extra wipe included. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, esp_rmaker_system_ctrl_factory_reset_with_wipe(__wipe_extra, &__nested_extra_runs, -1));
    return ESP_RMAKER_OK;
}

void test_factory_reset_with_wipe_nested_skips_extra(void)
{
    __setup();
    __nested_extra_runs = 0;

    esp_rmaker_system_ctrl_factory_reset_participant_t starter = __participant("starter", __wipe_starts_clear_claim, false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_register(&starter));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __factory_reset());

    TEST_ASSERT_EQUAL_INT(0, __nested_extra_runs);
    TEST_ASSERT_EQUAL_UINT8(1, __wipe_count);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_system_ctrl_factory_reset_participant_unregister(__wipe_starts_clear_claim));
    __teardown();
}
