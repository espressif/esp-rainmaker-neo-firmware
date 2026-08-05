/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_schedules.c
 * @brief Unit tests for schedule JSON parsing functions
 *
 * Includes schedules.c directly to test static parsing functions.
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <stdbool.h>

#include "json_parser.h"
#include "esp_rmaker_error_types.h"
#include "esp_schedule.h"
#include "esp_schedule_port_osal.h"
#include "osal_semaphore.h"

/* Include schedules.c to access static parsing functions */
#include "services/schedules.c"

/* --- Per-node schedule test fixture --------------------------------------
 *
 * ``details_*`` tests drive ``__build_schedule_details_for_node_locked``
 * directly against a bare in-place self node.
 *
 * ``esp_schedule_init(false, ...)`` is idempotent and runs the component
 * with NVS disabled, so handles created here never touch the test
 * partition. Persistence is the schedule service's own concern (the
 * details JSON), and the local_config NVS handle is not brought up by this
 * fixture -- its getters are NULL-safe, so a reload is a clean no-op. */

static _esp_rmaker_node_t s_test_node;

static esp_rmaker_node_t *schedule_test_node_setup(void)
{
    memset(&s_test_node, 0, sizeof(s_test_node));
    _esp_rmaker_node_init(&s_test_node);
    TEST_ASSERT_NOT_NULL(s_test_node.lock);
    /* esp_schedule runs with NVS disabled: this service owns persistence, so
     * handles created here never touch the test partition. */
    (void)esp_schedule_init_with_config(esp_schedule_port_osal_get(), false /* enable_nvs */, NULL, NULL);
    return (esp_rmaker_node_t *)&s_test_node;
}

static void schedule_test_node_teardown(esp_rmaker_node_t *node)
{
    _esp_rmaker_node_t *n = (_esp_rmaker_node_t *)node;
    /* Reset path runs through ::esp_rmaker_schedule_service_unload_node, which calls
     * ``esp_schedule_delete`` on every handle the parse produced. */
    _esp_rmaker_node_reset(n);
    if (n->lock) {
        osal_semaphore_delete(n->lock);
        n->lock = NULL;
    }
}

static node_schedule_state_t *schedule_test_sched(const esp_rmaker_node_t *node)
{
    return &((_esp_rmaker_node_t *)node)->schedule;
}

/* Helper used by the bogus-id table-driven tests below. Builds a node,
 * pushes the JSON, asserts zero schedules ended up installed. */
static void __assert_id_rejected(const char *json)
{
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(0, schedule_test_sched(node)->count,
                                  "Schedule with bogus id must be rejected");
        TEST_ASSERT_NULL_MESSAGE(schedule_test_sched(node)->handles,
                                 "No handles array should be allocated for an empty install");
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

/* ========================================================================== */
/* Parse action tests                                                          */
/* ========================================================================== */

void test_schedules_parse_action_valid_light_power(void)
{
    const char *json = "{\"action\":{\"light\":{\"Power\":true}}}";
    jparse_ctx_t jctx;
    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));

    __schedule_action_t action = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_action(&jctx, &action));
    TEST_ASSERT_NOT_NULL_MESSAGE(action.data, "Action data should not be NULL");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, action.data_len, "Action data_len should be > 0");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("{\"light\":{\"Power\":true}}", action.data, "Action data mismatch");

    __schedule_action_free(&action);
    json_parse_end(&jctx);
}

void test_schedules_parse_action_valid_nested_object(void)
{
    const char *json = "{\"action\":{\"device1\":{\"param1\":42,\"param2\":\"value\"}}}";
    jparse_ctx_t jctx;
    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));

    __schedule_action_t action = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_action(&jctx, &action));
    TEST_ASSERT_TRUE_MESSAGE(strstr(action.data, "device1") != NULL, "Should contain device1");
    TEST_ASSERT_TRUE_MESSAGE(strstr(action.data, "param1") != NULL, "Should contain param1");

    __schedule_action_free(&action);
    json_parse_end(&jctx);
}

void test_schedules_parse_action_missing_returns_null(void)
{
    const char *json = "{\"other_key\":\"value\"}";
    jparse_ctx_t jctx;
    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));

    __schedule_action_t action = {0};
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_RMAKER_OK, __parse_action(&jctx, &action),
                                  "Expected error when action key is missing");
    TEST_ASSERT_NULL(action.data);

    json_parse_end(&jctx);
}

void test_schedules_parse_action_empty_object_returns_empty(void)
{
    const char *json = "{\"action\":{}}";
    jparse_ctx_t jctx;
    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));

    __schedule_action_t action = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_action(&jctx, &action));
    TEST_ASSERT_EQUAL_MESSAGE(3, action.data_len, "Action data_len should be 3 for {} with NUL termination");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("{}", action.data, "Action data should be empty object");
    __schedule_action_free(&action);
    json_parse_end(&jctx);
}

void test_schedules_parse_action_free_null_safe(void)
{
    __schedule_action_free(NULL);
    /* Should not crash */
}

/* ========================================================================== */
/* Parse enabled flag tests                                                    */
/* ========================================================================== */

void test_schedules_details_disabled_schedule_skipped(void)
{
    /* __parse_schedule_details_json_locked skips schedules with enabled: false */
    const char *json = "[{\"enabled\":false,\"id\":\"Skip\",\"triggers\":[{\"rsec\":3600}],"
                       "\"validity\":{},\"action\":{\"light\":{\"Power\":true}}}]";

    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(0, schedule_test_sched(node)->count,
                                  "Disabled schedule must not produce a handle");
        TEST_ASSERT_NULL_MESSAGE(schedule_test_sched(node)->handles,
                                 "No handles array should be allocated for an empty install");
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_details_mixed_enabled_disabled(void)
{
    /* First enabled, second disabled - only the first ends up installed.
     * ``count`` must reflect created entries only, and the surviving handle
     * lives at slot 0 (per-node array grows by realloc-append). */
    const char *json = "["
                       "{\"enabled\":true,\"id\":\"On\",\"triggers\":[{\"rsec\":3600}],\"validity\":{},\"action\":{\"x\":1}},"
                       "{\"enabled\":false,\"id\":\"Off\",\"triggers\":[{\"rsec\":7200}],\"validity\":{},\"action\":{\"y\":2}}"
                       "]";

    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(1, schedule_test_sched(node)->count,
                                  "count should reflect created only");
        TEST_ASSERT_NOT_NULL_MESSAGE(schedule_test_sched(node)->handles,
                                     "handles array should be allocated");
        TEST_ASSERT_NOT_NULL_MESSAGE(schedule_test_sched(node)->handles[0],
                                     "Created handle should be at slot 0");

        /* Clear must wipe count + free the array. */
        __node_release_locked(node);
        TEST_ASSERT_EQUAL(0, schedule_test_sched(node)->count);
        TEST_ASSERT_NULL(schedule_test_sched(node)->handles);
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

/* ========================================================================== */
/* Regression: skipped/failed entries must not corrupt clear path             */
/* (heap_caps_free assert from release/v0.0.2)                                */
/* ========================================================================== */

void test_schedules_details_all_no_id_count_zero(void)
{
    /* All entries lack an id - parse should skip them all; count must be 0. */
    const char *json = "["
                       "{\"enabled\":true,\"triggers\":[{\"rsec\":3600}],\"validity\":{},\"action\":{\"x\":1}},"
                       "{\"enabled\":true,\"triggers\":[{\"rsec\":7200}],\"validity\":{},\"action\":{\"y\":2}},"
                       "{\"enabled\":true,\"triggers\":[{\"rsec\":9000}],\"validity\":{},\"action\":{\"z\":3}}"
                       "]";

    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(0, schedule_test_sched(node)->count,
                                  "Entries without id must not increment count");
        TEST_ASSERT_NULL_MESSAGE(schedule_test_sched(node)->handles,
                                 "No handles array should be allocated for an empty install");

        /* Clear over an already-empty slice must be a no-op (was the
         * regression - old global path crashed iterating NULL slots). */
        __node_release_locked(node);
        TEST_ASSERT_NULL(schedule_test_sched(node)->handles);
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_details_clear_after_skip_no_crash(void)
{
    /* Mix valid and skipped (no-id) entries. Clear must NOT dereference
     * skipped (NULL) slots - that was the production crash:
     *   assert failed: heap_caps_free ... free() target pointer is outside heap areas */
    const char *json = "["
                       "{\"enabled\":true,\"triggers\":[{\"rsec\":3600}],\"validity\":{},\"action\":{\"a\":1}},"   /* no id */
                       "{\"enabled\":true,\"id\":\"Valid\",\"triggers\":[{\"rsec\":7200}],\"validity\":{},\"action\":{\"b\":2}},"
                       "{\"enabled\":true,\"triggers\":[{\"rsec\":9000}],\"validity\":{},\"action\":{\"c\":3}}"     /* no id */
                       "]";

    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(1, schedule_test_sched(node)->count,
                                  "Only the entry with id should count");
        TEST_ASSERT_NOT_NULL_MESSAGE(schedule_test_sched(node)->handles[0],
                                     "Valid handle should be packed at slot 0");

        /* Regression: previously crashed inside the clear loop. */
        __node_release_locked(node);
        TEST_ASSERT_EQUAL(0, schedule_test_sched(node)->count);
        TEST_ASSERT_NULL(schedule_test_sched(node)->handles);
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_details_reparse_after_skip_no_crash(void)
{
    /* Reproduces the original production sequence:
     *   1. First update with entries lacking id -> all skipped.
     *   2. Second update -> build path internally clears first, then installs.
     * The clear step inside ``__build_schedule_details_for_node_locked``
     * previously crashed iterating NULL slots; per-node ``__node_release_locked(node)``
     * is now NULL-safe (no array allocated when count is 0). */
    const char *first_json = "["
                             "{\"enabled\":true,\"triggers\":[{\"rsec\":3600}],\"validity\":{},\"action\":{\"a\":1}},"
                             "{\"enabled\":true,\"triggers\":[{\"rsec\":7200}],\"validity\":{},\"action\":{\"b\":2}},"
                             "{\"enabled\":true,\"triggers\":[{\"rsec\":9000}],\"validity\":{},\"action\":{\"c\":3}}"
                             "]";
    const char *second_json = "["
                              "{\"enabled\":true,\"id\":\"Real\",\"triggers\":[{\"rsec\":3600}],\"validity\":{},\"action\":{\"x\":1}}"
                              "]";

    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, first_json, strlen(first_json)));
        TEST_ASSERT_EQUAL(0, schedule_test_sched(node)->count);

        /* Second update: ``__build_..._locked`` clears the (empty) slice
         * internally before installing the new entry. */
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, second_json, strlen(second_json)));
        TEST_ASSERT_EQUAL(1, schedule_test_sched(node)->count);
        TEST_ASSERT_NOT_NULL(schedule_test_sched(node)->handles[0]);
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

/* ========================================================================== */
/* Parse trigger tests                                                        */
/*                                                                            */
/* esp_schedule carries exactly one trigger per schedule, so __parse_trigger  */
/* honours only index 0 of the cloud "triggers" array and reports             */
/* INVALID_ARG when no usable trigger is there. The field mapping has to land */
/* on a shape esp_schedule's validator accepts (its docs/trigger_rules.md).   */
/* ========================================================================== */

void test_schedules_parse_trigger_relative(void)
{
    const char *json = "{\"triggers\":[{\"rsec\":3600,\"ts\":1726387200}]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_trigger(&jctx, &trigger, NULL));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_RELATIVE, trigger.type, "Should be relative type");
    TEST_ASSERT_EQUAL_MESSAGE(3600, trigger.relative_seconds, "rsec should be 3600");
    TEST_ASSERT_EQUAL_MESSAGE(1726387200, (int)trigger.next_scheduled_time_utc, "ts mismatch");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_days_of_week(void)
{
    const char *json = "{\"triggers\":[{\"d\":127,\"m\":480}]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_trigger(&jctx, &trigger, NULL));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_DAYS_OF_WEEK, trigger.type, "Should be days of week");
    TEST_ASSERT_EQUAL_MESSAGE(127, trigger.day.repeat_days, "repeat_days mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(8, trigger.hours, "Should be 8:00");
    TEST_ASSERT_EQUAL_MESSAGE(0, trigger.minutes, "Should be 8:00");
    TEST_ASSERT_EQUAL_MESSAGE(0, trigger.date.day, "Weekday arm must leave the date arm clear");
    TEST_ASSERT_EQUAL_MESSAGE(0, trigger.date.repeat_months, "Weekday arm must leave the date arm clear");
    TEST_ASSERT_FALSE_MESSAGE(trigger.date.repeat_every_year, "Weekday arm must leave the date arm clear");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_date(void)
{
    /* Year-bounded date arm: esp_schedule shape DATE-5. */
    const char *json = "{\"triggers\":[{\"dd\":15,\"mm\":256,\"yy\":2025,\"m\":1260}]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_trigger(&jctx, &trigger, NULL));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_DATE, trigger.type, "Should be date type");
    TEST_ASSERT_EQUAL_MESSAGE(15, trigger.date.day, "day mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(256, trigger.date.repeat_months, "month mask mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(2025, trigger.date.year, "year mismatch");
    TEST_ASSERT_FALSE_MESSAGE(trigger.date.repeat_every_year, "A bounded year excludes repeat_every_year");
    TEST_ASSERT_EQUAL_MESSAGE(0, trigger.day.repeat_days, "Date arm must leave the weekday arm clear");
    TEST_ASSERT_EQUAL_MESSAGE(21, trigger.hours, "Should be 21:00");
    TEST_ASSERT_EQUAL_MESSAGE(0, trigger.minutes, "Should be 21:00");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_date_no_month_mask_recurs_monthly(void)
{
    /* "19:30 on the 20th of every month": no 'mm'/'yy' means every month of
     * every year, which esp_schedule spells as a full month mask plus
     * repeat_every_year (shape DATE-4). A bare day with no mask would be a
     * one-shot, and repeat_every_year without a mask is rejected outright. */
    const char *json = "{\"triggers\":[{\"dd\":20,\"m\":1170}]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_trigger(&jctx, &trigger, NULL));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_DATE, trigger.type, "Should be date type");
    TEST_ASSERT_EQUAL_MESSAGE(20, trigger.date.day, "day mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_MONTH_ALL, trigger.date.repeat_months,
                              "Absent 'mm' must become a full month mask");
    TEST_ASSERT_EQUAL_MESSAGE(0, trigger.date.year, "No 'yy' means no year bound");
    TEST_ASSERT_TRUE_MESSAGE(trigger.date.repeat_every_year, "Absent 'yy' must recur every year");
    TEST_ASSERT_EQUAL_MESSAGE(19, trigger.hours, "Should be 19:30");
    TEST_ASSERT_EQUAL_MESSAGE(30, trigger.minutes, "Should be 19:30");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_date_arm_beats_weekday_arm(void)
{
    /* The cloud can express "(weekdays OR the Nth) of a month set"; the two
     * arms are exclusive in esp_schedule and a trigger carrying both is
     * rejected, so the date arm wins and 'd' is dropped. */
    const char *json = "{\"triggers\":[{\"m\":843,\"d\":62,\"dd\":14,\"mm\":15}]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_trigger(&jctx, &trigger, NULL));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_DATE, trigger.type, "Date arm must win");
    TEST_ASSERT_EQUAL_MESSAGE(0, trigger.day.repeat_days, "Weekday arm must be cleared");
    TEST_ASSERT_EQUAL_MESSAGE(14, trigger.date.day, "day mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(15, trigger.date.repeat_months, "month mask mismatch");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_months_without_day_ignored(void)
{
    /* 'mm'/'yy' need a 'dd' to apply to; without one they are ignored and the
     * weekday arm stands on its own (a month mask with no day-of-month is
     * rejected by esp_schedule). */
    const char *json = "{\"triggers\":[{\"m\":300,\"d\":5,\"mm\":15,\"yy\":2030}]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_trigger(&jctx, &trigger, NULL));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_DAYS_OF_WEEK, trigger.type, "Should stay days of week");
    TEST_ASSERT_EQUAL_MESSAGE(5, trigger.day.repeat_days, "repeat_days mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(0, trigger.date.day, "date arm must stay clear");
    TEST_ASSERT_EQUAL_MESSAGE(0, trigger.date.repeat_months, "date arm must stay clear");
    TEST_ASSERT_EQUAL_MESSAGE(0, trigger.date.year, "date arm must stay clear");
    TEST_ASSERT_FALSE_MESSAGE(trigger.date.repeat_every_year, "date arm must stay clear");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_minutes_out_of_range_fails(void)
{
    /* 'm' beyond a day would yield hours > 23, which esp_schedule rejects. */
    const char *json = "{\"triggers\":[{\"d\":127,\"m\":1440}]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, __parse_trigger(&jctx, &trigger, NULL));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_INVALID, trigger.type, "Trigger must be reset");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_empty_array_fails(void)
{
    const char *json = "{\"triggers\":[]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, __parse_trigger(&jctx, &trigger, NULL),
                              "Empty array yields no usable trigger");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_INVALID, trigger.type, "Type must stay INVALID");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_missing_fails(void)
{
    const char *json = "{\"other\":\"value\"}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, __parse_trigger(&jctx, &trigger, NULL),
                              "Absent triggers array yields no usable trigger");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_extra_entries_dropped(void)
{
    /* Only index 0 is honoured; the rest are dropped with a warning. */
    const char *json = "{\"triggers\":[{\"rsec\":3600},{\"d\":127,\"m\":480}]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_trigger(&jctx, &trigger, NULL));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_RELATIVE, trigger.type, "Must take index 0");
    TEST_ASSERT_EQUAL_MESSAGE(3600, trigger.relative_seconds, "rsec of index 0 expected");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_invalid_first_entry_fails(void)
{
    /* Index 0 is unusable (days-of-week with no 'm'). A valid later entry
     * must NOT rescue the schedule now that only index 0 is honoured. */
    const char *json = "{\"triggers\":[{\"d\":127},{\"rsec\":7200}]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_INVALID_ARG, __parse_trigger(&jctx, &trigger, NULL),
                              "Later valid entries must not rescue an unusable index 0");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_INVALID, trigger.type, "Trigger must be reset");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_days_missing_minutes_fails(void)
{
    /* days-of-week trigger missing 'm' is unusable. */
    const char *json = "{\"triggers\":[{\"d\":127}]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, __parse_trigger(&jctx, &trigger, NULL));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_INVALID, trigger.type, "Trigger must be reset");

    json_parse_end(&jctx);
}

void test_schedules_parse_trigger_invalid_type_fails(void)
{
    /* Only entry has no recognized field -> type stays INVALID. */
    const char *json = "{\"triggers\":[{\"foo\":1}]}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, __parse_trigger(&jctx, &trigger, NULL));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_SCHEDULE_TYPE_INVALID, trigger.type, "Type must stay INVALID");

    json_parse_end(&jctx);
}

/* ========================================================================== */
/* Parse validity tests                                                       */
/* ========================================================================== */

void test_schedules_parse_validity_full(void)
{
    const char *json = "{\"validity\":{\"start\":1000,\"end\":2000}}";
    jparse_ctx_t jctx;
    esp_schedule_validity_t validity = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_validity(&jctx, &validity));

    TEST_ASSERT_EQUAL_MESSAGE(1000, (int)validity.start_time, "start time mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(2000, (int)validity.end_time, "end time mismatch");

    json_parse_end(&jctx);
}

void test_schedules_parse_validity_missing_returns_ok(void)
{
    const char *json = "{\"other\":\"value\"}";
    jparse_ctx_t jctx;
    esp_schedule_validity_t validity = { .start_time = 99, .end_time = 99 };

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_validity(&jctx, &validity));

    /* When validity key is missing, struct is unchanged */
    TEST_ASSERT_EQUAL_MESSAGE(99, (int)validity.start_time, "start unchanged when validity missing");
    TEST_ASSERT_EQUAL_MESSAGE(99, (int)validity.end_time, "end unchanged when validity missing");

    json_parse_end(&jctx);
}

void test_schedules_parse_validity_empty_object(void)
{
    const char *json = "{\"validity\":{}}";
    jparse_ctx_t jctx;
    esp_schedule_validity_t validity = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_validity(&jctx, &validity));

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)validity.start_time, "start defaults to 0");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)validity.end_time, "end defaults to 0");

    json_parse_end(&jctx);
}

void test_schedules_parse_validity_large_timestamps(void)
{
    const char *json = "{\"validity\":{\"start\":1704067200,\"end\":1735689600}}";
    jparse_ctx_t jctx;
    esp_schedule_validity_t validity = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_validity(&jctx, &validity));

    TEST_ASSERT_EQUAL_MESSAGE(1704067200, (int)validity.start_time, "start mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(1735689600, (int)validity.end_time, "end mismatch");

    json_parse_end(&jctx);
}

void test_schedules_parse_validity_bogus_keys_only(void)
{
    /* Validity object present but without start/end - must zero, not leak garbage. */
    const char *json = "{\"validity\":{\"bogus\":342}}";
    jparse_ctx_t jctx;
    esp_schedule_validity_t validity = { .start_time = 0xDEAD, .end_time = 0xBEEF };

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_validity(&jctx, &validity));

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)validity.start_time, "start must be 0 when key missing in validity object");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)validity.end_time, "end must be 0 when key missing in validity object");

    json_parse_end(&jctx);
}

void test_schedules_parse_validity_only_start(void)
{
    const char *json = "{\"validity\":{\"start\":1234}}";
    jparse_ctx_t jctx;
    esp_schedule_validity_t validity = { .start_time = 99, .end_time = 99 };

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_validity(&jctx, &validity));

    TEST_ASSERT_EQUAL_MESSAGE(1234, (int)validity.start_time, "start parsed");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)validity.end_time, "end must be 0 when missing");

    json_parse_end(&jctx);
}

void test_schedules_parse_validity_only_end(void)
{
    const char *json = "{\"validity\":{\"end\":5678}}";
    jparse_ctx_t jctx;
    esp_schedule_validity_t validity = { .start_time = 99, .end_time = 99 };

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_validity(&jctx, &validity));

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)validity.start_time, "start must be 0 when missing");
    TEST_ASSERT_EQUAL_MESSAGE(5678, (int)validity.end_time, "end parsed");

    json_parse_end(&jctx);
}

void test_schedules_parse_validity_wrong_types(void)
{
    /* start/end with non-int values - both must fall back to 0. */
    const char *json = "{\"validity\":{\"start\":\"oops\",\"end\":true}}";
    jparse_ctx_t jctx;
    esp_schedule_validity_t validity = { .start_time = 77, .end_time = 88 };

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_validity(&jctx, &validity));

    TEST_ASSERT_EQUAL_MESSAGE(0, (int)validity.start_time, "start must be 0 on type mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)validity.end_time, "end must be 0 on type mismatch");

    json_parse_end(&jctx);
}

void test_schedules_parse_validity_negative_values(void)
{
    /* Negative timestamps are syntactically valid ints - parser must pass them through. */
    const char *json = "{\"validity\":{\"start\":-1,\"end\":-100}}";
    jparse_ctx_t jctx;
    esp_schedule_validity_t validity = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_validity(&jctx, &validity));

    TEST_ASSERT_EQUAL_MESSAGE(-1, (int)validity.start_time, "negative start preserved");
    TEST_ASSERT_EQUAL_MESSAGE(-100, (int)validity.end_time, "negative end preserved");

    json_parse_end(&jctx);
}

void test_schedules_parse_validity_not_an_object(void)
{
    /* "validity" present but not an object - must not crash, struct unchanged. */
    const char *json = "{\"validity\":42}";
    jparse_ctx_t jctx;
    esp_schedule_validity_t validity = { .start_time = 11, .end_time = 22 };

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_validity(&jctx, &validity));

    /* json_obj_get_object should fail; struct untouched. */
    TEST_ASSERT_EQUAL_MESSAGE(11, (int)validity.start_time, "start unchanged when validity not an object");
    TEST_ASSERT_EQUAL_MESSAGE(22, (int)validity.end_time, "end unchanged when validity not an object");

    json_parse_end(&jctx);
}

/* ========================================================================== */
/* JSON format variation tests                                                */
/* ========================================================================== */

void test_schedules_parse_action_key_order_independent(void)
{
    const char *json = "{\"enabled\":true,\"id\":\"Test\",\"action\":{\"x\":1}}";
    jparse_ctx_t jctx;
    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));

    __schedule_action_t action = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_action(&jctx, &action));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("{\"x\":1}", action.data, "Action content");

    __schedule_action_free(&action);
    json_parse_end(&jctx);
}

void test_schedules_parse_action_whitespace_tolerant(void)
{
    const char *json = "  {  \"action\"  :  { \"light\" : { \"Power\" : true } } }  ";
    jparse_ctx_t jctx;
    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));

    __schedule_action_t action = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_action(&jctx, &action));
    TEST_ASSERT_TRUE_MESSAGE(strstr(action.data, "light") != NULL, "Should contain light");

    __schedule_action_free(&action);
    json_parse_end(&jctx);
}

void test_schedules_parse_full_schedule_object(void)
{
    const char *json = "{\"enabled\":true,\"id\":\"Wake Up\",\"triggers\":[{\"rsec\":3600}],"
                       "\"validity\":{\"start\":0,\"end\":999999},\"action\":{\"light\":{\"Power\":true}}}";
    jparse_ctx_t jctx;
    esp_schedule_trigger_t trigger = {0};
    esp_schedule_validity_t validity = {0};

    TEST_ASSERT_EQUAL(0, json_parse_start(&jctx, json, (int)strlen(json)));

    bool enabled = false;
    json_obj_get_bool(&jctx, "enabled", &enabled);
    TEST_ASSERT_TRUE_MESSAGE(enabled, "enabled should be true");

    char id[32] = {0};
    json_obj_get_string(&jctx, "id", id, sizeof(id));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Wake Up", id, "id mismatch");

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_trigger(&jctx, &trigger, NULL));
    TEST_ASSERT_EQUAL(ESP_SCHEDULE_TYPE_RELATIVE, trigger.type);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_validity(&jctx, &validity));
    TEST_ASSERT_EQUAL(0, (int)validity.start_time);
    TEST_ASSERT_EQUAL(999999, (int)validity.end_time);

    __schedule_action_t action = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __parse_action(&jctx, &action));
    TEST_ASSERT_TRUE_MESSAGE(strstr(action.data, "Power") != NULL, "action should contain Power");

    __schedule_action_free(&action);
    json_parse_end(&jctx);
}

/* ========================================================================== */
/* Regression: schedule count must be clamped to MAX_SCHEDULES                */
/* (uint8_t truncation -> OOB write into handles array)                       */
/* ========================================================================== */

void test_schedules_details_count_clamped_to_max(void)
{
    /* Build a JSON array with MAX_SCHEDULES_PER_NODE + 1 minimal valid
     * entries. Each entry shares the same cloud id ``"N"``, but
     * ``__derive_schedule_name`` hashes (local_id, cloud_id) - with
     * local_id NULL for every entry, every entry derives the same NVS
     * name. The first install wins; the rest collide on ``esp_schedule_create``
     * and would be re-added to ``handles`` regardless. The interesting
     * invariant is the parse-loop clamp: it must never iterate past
     * MAX_SCHEDULES_PER_NODE, even when the cloud sends more. */
    const char *entry_fmt = "{\"enabled\":true,\"id\":\"N%d\",\"triggers\":[{\"rsec\":1}],\"validity\":{},\"action\":{\"x\":1}}";
    const int n_entries = MAX_SCHEDULES_PER_NODE + 1;

    /* Compose entries with distinct ids so each survives create. */
    size_t cap = 2 + n_entries * (strlen(entry_fmt) + 12);
    char *json = (char *)calloc(1, cap);
    TEST_ASSERT_NOT_NULL(json);
    size_t off = 0;
    json[off++] = '[';
    for (int i = 0; i < n_entries; i++) {
        off += (size_t)snprintf(json + off, cap - off, entry_fmt, i);
        if (i < n_entries - 1) {
            json[off++] = ',';
        }
    }
    json[off++] = ']';
    json[off] = '\0';

    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, off));
        TEST_ASSERT_TRUE_MESSAGE(schedule_test_sched(node)->count <= MAX_SCHEDULES_PER_NODE,
                                 "count must be clamped to MAX_SCHEDULES_PER_NODE");
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
    free(json);
}

/* ========================================================================== */
/* Bogus id handling: missing / empty / oversized / non-string                */
/* ========================================================================== */

void test_schedules_details_empty_id_rejected(void)
{
    /* id present but empty string -> skipped. */
    __assert_id_rejected(
        "[{\"enabled\":true,\"id\":\"\",\"triggers\":[{\"rsec\":3600}],\"validity\":{},\"action\":{\"x\":1}}]");
}

void test_schedules_details_oversized_id_rejected(void)
{
    /* id exceeds MAX_SCHEDULE_NAME_LEN (16). json_obj_get_string must not
     * overflow the buffer; parser must reject the entry rather than truncate
     * to a colliding NVS key. */
    __assert_id_rejected(
        "[{\"enabled\":true,\"id\":\"this_id_is_way_too_long_for_buffer\","
        "\"triggers\":[{\"rsec\":3600}],\"validity\":{},\"action\":{\"x\":1}}]");
}

void test_schedules_details_numeric_id_rejected(void)
{
    /* id is a number, not a string -> skipped. */
    __assert_id_rejected(
        "[{\"enabled\":true,\"id\":12345,\"triggers\":[{\"rsec\":3600}],\"validity\":{},\"action\":{\"x\":1}}]");
}

void test_schedules_details_null_id_rejected(void)
{
    /* id explicitly null -> skipped. */
    __assert_id_rejected(
        "[{\"enabled\":true,\"id\":null,\"triggers\":[{\"rsec\":3600}],\"validity\":{},\"action\":{\"x\":1}}]");
}

void test_schedules_details_object_id_rejected(void)
{
    /* id is an object -> skipped. */
    __assert_id_rejected(
        "[{\"enabled\":true,\"id\":{\"x\":1},\"triggers\":[{\"rsec\":3600}],\"validity\":{},\"action\":{\"x\":1}}]");
}

void test_schedules_details_max_length_id_accepted(void)
{
    /* Exactly MAX_SCHEDULE_NAME_LEN (16) chars of cloud id -> must be accepted.
     * The derived NVS schedule name is hashed to 14 chars, so a long cloud
     * id is fine; only the parse path's id buffer (17 bytes incl. NUL)
     * could reject it. */
    const char *json =
        "[{\"enabled\":true,\"id\":\"abcdefghijklmnop\","
        "\"triggers\":[{\"rsec\":3600}],\"validity\":{},\"action\":{\"x\":1}}]";

    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(1, schedule_test_sched(node)->count,
                                  "Max-length id must be accepted");
        TEST_ASSERT_NOT_NULL(schedule_test_sched(node)->handles[0]);
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_details_name_field_ignored(void)
{
    /* Regression: per spec, "name" is ignored and "id" is mandatory.
     * An entry with only "name" (no id) must be rejected. */
    __assert_id_rejected(
        "[{\"enabled\":true,\"name\":\"Schedule_1778728172\","
        "\"triggers\":[{\"d\":8,\"m\":671}],\"validity\":null,\"action\":{\"Light\":{\"Power\":true}}}]");
}

/* ========================================================================== */
/* Regression: update_details(NULL) must not crash                            */
/* ========================================================================== */

void test_schedules_update_details_null_input_safe(void)
{
    /* Before the fix, strdup(NULL) is undefined behavior.
     * After the fix, the public API should reject NULL with INVALID_ARG. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG,
                      esp_rmaker_schedule_service_update_details(NULL));
}

/* ==========================================================================
 * Serialization round-trip tests
 *
 * ``__serialize_node_schedules_locked`` is the inverse of the build path and
 * the form we persist. Its contract is that re-parsing its output reproduces
 * the same live configs -- otherwise a reboot would silently change a
 * schedule's meaning. Each case installs from a cloud payload, serializes,
 * rebuilds from that output, and compares the resulting trigger.
 * ========================================================================== */

/* Grab a copy of slot ``i``'s trigger config for comparison. */
static esp_schedule_trigger_t schedule_test_trigger_at(const esp_rmaker_node_t *node, uint8_t i)
{
    esp_schedule_config_t cfg = {0};
    TEST_ASSERT_TRUE_MESSAGE(i < schedule_test_sched(node)->count, "slot out of range");
    TEST_ASSERT_EQUAL(ESP_OK,
                      esp_schedule_get(schedule_test_sched(node)->handles[i], &cfg));
    return cfg.trigger;
}

/* Install ``json``, serialize, reinstall from the serialized form, and assert
 * the trigger of slot 0 survives unchanged. Returns the serialized string,
 * which the caller frees. */
static char *schedule_test_round_trip(esp_rmaker_node_t *node, const char *json)
{
    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
    esp_rmaker_node_unlock(node);
    TEST_ASSERT_EQUAL_MESSAGE(1, schedule_test_sched(node)->count, "setup must install one schedule");
    esp_schedule_trigger_t before = schedule_test_trigger_at(node, 0);

    esp_rmaker_node_lock(node);
    char *serialized = __serialize_node_schedules_locked(node);
    esp_rmaker_node_unlock(node);
    TEST_ASSERT_NOT_NULL_MESSAGE(serialized, "serialization must succeed");

    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK,
                              __build_schedule_details_for_node_locked(node, NULL, serialized,
                                      strlen(serialized)),
                              "serialized output must be re-parseable");
    esp_rmaker_node_unlock(node);
    TEST_ASSERT_EQUAL_MESSAGE(1, schedule_test_sched(node)->count,
                              "round trip must install exactly one schedule");
    esp_schedule_trigger_t after = schedule_test_trigger_at(node, 0);

    TEST_ASSERT_EQUAL_MESSAGE(before.type, after.type, "type must survive the round trip");
    TEST_ASSERT_EQUAL_MESSAGE(before.hours, after.hours, "hours must survive");
    TEST_ASSERT_EQUAL_MESSAGE(before.minutes, after.minutes, "minutes must survive");
    TEST_ASSERT_EQUAL_MESSAGE(before.day.repeat_days, after.day.repeat_days, "repeat_days must survive");
    TEST_ASSERT_EQUAL_MESSAGE(before.date.day, after.date.day, "date.day must survive");
    TEST_ASSERT_EQUAL_MESSAGE(before.date.repeat_months, after.date.repeat_months,
                              "repeat_months must survive");
    TEST_ASSERT_EQUAL_MESSAGE(before.date.year, after.date.year, "year must survive");
    TEST_ASSERT_EQUAL_MESSAGE(before.date.repeat_every_year, after.date.repeat_every_year,
                              "repeat_every_year must survive");
    TEST_ASSERT_EQUAL_MESSAGE(before.relative_seconds, after.relative_seconds,
                              "relative_seconds must survive");
    return serialized;
}

void test_schedules_serialize_round_trip_days_of_week(void)
{
    esp_rmaker_node_t *node = schedule_test_node_setup();
    if (TEST_PROTECT()) {
        char *out = schedule_test_round_trip(
                        node,
                        "[{\"enabled\":true,\"id\":\"dow\",\"triggers\":[{\"d\":127,\"m\":480}],"
                        "\"action\":{\"light\":{\"Power\":true}}}]");
        /* The weekday arm must be emitted, and no date arm alongside it. */
        TEST_ASSERT_NOT_NULL(strstr(out, "\"d\":127"));
        TEST_ASSERT_NULL_MESSAGE(strstr(out, "\"dd\""), "weekday arm must not emit a date arm");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"id\":\"dow\""), "cloud id must be preserved");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "Power"), "action must be preserved");
        free(out);
    }
    schedule_test_node_teardown(node);
}

void test_schedules_serialize_round_trip_date_recurring(void)
{
    /* Shape DATE-4: no 'mm'/'yy' from the cloud becomes a full month mask plus
     * repeat_every_year, and that has to come back the same way. */
    esp_rmaker_node_t *node = schedule_test_node_setup();
    if (TEST_PROTECT()) {
        char *out = schedule_test_round_trip(
                        node,
                        "[{\"enabled\":true,\"id\":\"date4\",\"triggers\":[{\"dd\":20,\"m\":1170}],"
                        "\"action\":{\"x\":1}}]");
        TEST_ASSERT_NULL_MESSAGE(strstr(out, "\"yy\""),
                                 "repeat_every_year is spelled by omitting 'yy'");
        free(out);
    }
    schedule_test_node_teardown(node);
}

void test_schedules_serialize_round_trip_date_year_bounded(void)
{
    /* Shape DATE-5: an explicit year must be emitted and must not turn into
     * repeat_every_year (the two are mutually exclusive). */
    esp_rmaker_node_t *node = schedule_test_node_setup();
    if (TEST_PROTECT()) {
        char *out = schedule_test_round_trip(
                        node,
                        "[{\"enabled\":true,\"id\":\"date5\","
                        "\"triggers\":[{\"dd\":15,\"mm\":256,\"yy\":2035,\"m\":1260}],"
                        "\"action\":{\"x\":1}}]");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"yy\":2035"), "year bound must be emitted");
        free(out);
    }
    schedule_test_node_teardown(node);
}

void test_schedules_serialize_relative_emits_computed_ts(void)
{
    /* The whole point of re-serializing rather than storing the cloud payload:
     * a relative trigger must persist its computed absolute target, or a replay
     * would recompute "now + rsec" and fire again on every boot. */
    esp_rmaker_node_t *node = schedule_test_node_setup();
    if (TEST_PROTECT()) {
        const char *json = "[{\"enabled\":true,\"id\":\"rel\","
                           "\"triggers\":[{\"rsec\":3600}],\"action\":{\"x\":1}}]";
        esp_rmaker_node_lock(node);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        char *out = __serialize_node_schedules_locked(node);
        esp_rmaker_node_unlock(node);
        TEST_ASSERT_NOT_NULL(out);

        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"rsec\":3600"), "rsec must be emitted");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"ts\":"), "computed ts must be emitted");
        /* ts must be a real timestamp, not the 0 placeholder. */
        TEST_ASSERT_NULL_MESSAGE(strstr(out, "\"ts\":0"), "ts must carry the computed target");
        free(out);
    }
    schedule_test_node_teardown(node);
}

void test_schedules_serialize_empty_node_is_empty_array(void)
{
    esp_rmaker_node_t *node = schedule_test_node_setup();
    if (TEST_PROTECT()) {
        esp_rmaker_node_lock(node);
        char *out = __serialize_node_schedules_locked(node);
        esp_rmaker_node_unlock(node);
        TEST_ASSERT_NOT_NULL(out);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("[]", out,
                                         "a node with no schedules must serialize to an empty array");
        free(out);
    }
    schedule_test_node_teardown(node);
}

void test_schedules_serialize_round_trip_preserves_count(void)
{
    /* Multiple schedules, mixed trigger types, must all survive. */
    const char *json = "["
                       "{\"enabled\":true,\"id\":\"a\",\"triggers\":[{\"d\":5,\"m\":300}],\"action\":{\"x\":1}},"
                       "{\"enabled\":true,\"id\":\"b\",\"triggers\":[{\"dd\":9,\"mm\":128,\"m\":0}],\"action\":{\"y\":2}},"
                       "{\"enabled\":true,\"id\":\"c\",\"triggers\":[{\"rsec\":60}],\"action\":{\"z\":3}}"
                       "]";
    esp_rmaker_node_t *node = schedule_test_node_setup();
    if (TEST_PROTECT()) {
        esp_rmaker_node_lock(node);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        char *out = __serialize_node_schedules_locked(node);
        esp_rmaker_node_unlock(node);
        TEST_ASSERT_NOT_NULL(out);
        TEST_ASSERT_EQUAL_MESSAGE(3, schedule_test_sched(node)->count, "setup installs three");

        esp_rmaker_node_lock(node);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, out, strlen(out)));
        esp_rmaker_node_unlock(node);
        TEST_ASSERT_EQUAL_MESSAGE(3, schedule_test_sched(node)->count,
                                  "round trip must preserve every schedule");
        free(out);
    }
    schedule_test_node_teardown(node);
}

/* ==========================================================================
 * Arm-time detection tests (timestamp callback)
 *
 * ``esp_schedule_get`` fills next_scheduled_time_utc for RELATIVE triggers
 * only, so the arm result is taken from the timestamp callback instead: it is
 * invoked for every trigger type and only when the schedule armed with a real
 * future occurrence. These cover the cases that a config read-back could not
 * distinguish.
 * ========================================================================== */

void test_schedules_arm_records_next_fire_for_date_trigger(void)
{
    /* The case the accessor cannot report: a DATE trigger that armed fine. Its
     * next-fire must still be captured, via the callback. */
    const char *json = "[{\"enabled\":true,\"id\":\"d1\",\"triggers\":[{\"dd\":15,\"mm\":0,\"m\":600}],"
                       "\"action\":{\"x\":1}}]";
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(1, schedule_test_sched(node)->count,
                                  "a date one-shot with a future occurrence must survive");

        esp_schedule_config_t cfg = {0};
        TEST_ASSERT_EQUAL(ESP_OK,
                          esp_schedule_get(schedule_test_sched(node)->handles[0], &cfg));
        const __schedule_priv_data_t *priv = (const __schedule_priv_data_t *)cfg.priv_data;
        TEST_ASSERT_NOT_NULL(priv);
        TEST_ASSERT_TRUE_MESSAGE(priv->one_shot, "a date arm with no month mask is one-shot");
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, (int)priv->next_fire_ts,
                                      "the timestamp callback must have recorded the armed instant");
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_arm_drops_date_trigger_with_past_year(void)
{
    /* Bounded to a year that has passed, so there is no occurrence: the arm
     * reports nothing and the schedule is dropped rather than kept as a
     * permanently inert entry. This is the non-relative arm-time prune. */
    const char *json = "[{\"enabled\":true,\"id\":\"old\","
                       "\"triggers\":[{\"dd\":15,\"mm\":0,\"yy\":2020,\"m\":600}],"
                       "\"action\":{\"x\":1}}]";
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(0, schedule_test_sched(node)->count,
                                  "a date arm bounded to a past year has no occurrence and must be dropped");
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_serialize_emits_ts_for_date_one_shot(void)
{
    /* "ts" used to be emitted for relative triggers only, because that was the
     * only type the config read-back populated. It must now cover any one-shot. */
    const char *json = "[{\"enabled\":true,\"id\":\"d2\",\"triggers\":[{\"dd\":15,\"mm\":0,\"m\":600}],"
                       "\"action\":{\"x\":1}}]";
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        char *out = __serialize_node_schedules_locked(node);
        TEST_ASSERT_NOT_NULL(out);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"ts\":"),
                                     "a date one-shot must persist its armed instant");
        TEST_ASSERT_NULL_MESSAGE(strstr(out, "\"ts\":0"), "ts must be the real computed instant");
        free(out);
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_serialize_omits_ts_for_repeating(void)
{
    /* Repeating triggers must NOT carry ts: their next-fire moves on every arm,
     * so emitting it would make the serialized form differ every boot and cost a
     * flash write each time. */
    const char *json = "[{\"enabled\":true,\"id\":\"rep\",\"triggers\":[{\"d\":127,\"m\":480}],"
                       "\"action\":{\"x\":1}}]";
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        char *first = __serialize_node_schedules_locked(node);
        TEST_ASSERT_NOT_NULL(first);
        TEST_ASSERT_NULL_MESSAGE(strstr(first, "\"ts\""), "a repeating trigger must not persist ts");

        /* Byte-stability is the property the replay's change check relies on. */
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, first, strlen(first)));
        char *second = __serialize_node_schedules_locked(node);
        TEST_ASSERT_NOT_NULL(second);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(first, second,
                                         "a repeating schedule must serialize identically across re-arms");
        free(first);
        free(second);
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_replay_skips_one_shot_already_due(void)
{
    /* Replaying our own payload: a one-shot whose armed instant is behind us
     * either fired (and the write removing it was lost) or came due while the
     * device was off. It must not be recreated. */
    char json[256];
    snprintf(json, sizeof(json),
             "[{\"enabled\":true,\"id\":\"past\",\"triggers\":[{\"rsec\":3600,\"ts\":%lld}],"
             "\"action\":{\"x\":1}}]", (long long)(time(NULL) - 60));

    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(0, schedule_test_sched(node)->count,
                                  "a one-shot already past its armed instant must not be restored");
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_replay_keeps_one_shot_still_pending(void)
{
    /* The converse: rebooting before a one-shot was due must keep it, armed for
     * the same instant rather than recomputed from the reboot time. */
    time_t target = time(NULL) + 3600;
    char json[256];
    snprintf(json, sizeof(json),
             "[{\"enabled\":true,\"id\":\"future\",\"triggers\":[{\"rsec\":60,\"ts\":%lld}],"
             "\"action\":{\"x\":1}}]", (long long)target);

    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(1, schedule_test_sched(node)->count,
                                  "a one-shot still ahead of us must be restored");

        esp_schedule_config_t cfg = {0};
        TEST_ASSERT_EQUAL(ESP_OK,
                          esp_schedule_get(schedule_test_sched(node)->handles[0], &cfg));
        TEST_ASSERT_EQUAL_MESSAGE((int)target, (int)cfg.trigger.next_scheduled_time_utc,
                                  "the persisted instant must be honoured, not recomputed from rsec");
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_arm_drops_year_bounded_repeating_after_expiry(void)
{
    /* A *repeating* schedule bounded to a year that has passed (shape DATE-5:
     * the 15th of each masked month in 2020). It is not one-shot, so neither the
     * fire-time prune nor the ts replay check applies to it -- the arm-time rule
     * is what has to catch it. */
    const char *json = "[{\"enabled\":true,\"id\":\"y2020\","
                       "\"triggers\":[{\"dd\":15,\"mm\":256,\"yy\":2020,\"m\":600}],"
                       "\"action\":{\"x\":1}}]";
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(0, schedule_test_sched(node)->count,
                                  "a year-bounded repeating schedule past its year must be dropped");
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_arm_keeps_year_bounded_repeating_still_valid(void)
{
    /* The converse: bounded to a year still ahead of us, so it must survive and
     * must not be mistaken for expired. */
    const char *json = "[{\"enabled\":true,\"id\":\"y2035\","
                       "\"triggers\":[{\"dd\":15,\"mm\":256,\"yy\":2035,\"m\":600}],"
                       "\"action\":{\"x\":1}}]";
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL_MESSAGE(1, schedule_test_sched(node)->count,
                                  "a year-bounded repeating schedule still in range must survive");

        esp_schedule_config_t cfg = {0};
        TEST_ASSERT_EQUAL(ESP_OK,
                          esp_schedule_get(schedule_test_sched(node)->handles[0], &cfg));
        const __schedule_priv_data_t *priv = (const __schedule_priv_data_t *)cfg.priv_data;
        TEST_ASSERT_FALSE_MESSAGE(priv->one_shot, "a month mask makes it repeating, not one-shot");
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, (int)priv->next_fire_ts, "it must have armed");
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

/* ==========================================================================
 * Spent-schedule removal tests
 *
 * A one-shot that has fired is dropped from the node and from the persisted
 * details. Lookup is by cloud id, not by handle pointer, because the fire path
 * is asynchronous and the set may have been replaced meanwhile.
 * ========================================================================== */

/* Three schedules, distinct ids, all repeating so nothing is auto-dropped. */
static const char *SCHEDULE_TEST_THREE =
    "["
    "{\"enabled\":true,\"id\":\"aa\",\"triggers\":[{\"d\":127,\"m\":300}],\"action\":{\"x\":1}},"
    "{\"enabled\":true,\"id\":\"bb\",\"triggers\":[{\"d\":127,\"m\":420}],\"action\":{\"y\":2}},"
    "{\"enabled\":true,\"id\":\"cc\",\"triggers\":[{\"d\":127,\"m\":540}],\"action\":{\"z\":3}}"
    "]";

void test_schedules_fired_one_shot_removed_from_node_and_payload(void)
{
    /* What the fire path does to a spent one-shot: the handle goes and the entry
     * disappears from what we persist, so a reboot cannot restore it. Deleting
     * here crosses tasks, which is safe only because the port's timer cancel
     * barriers against a running callback
     * (overrides/esp_schedule/port/esp_schedule_port_osal.c). */
    const char *json = "[{\"enabled\":true,\"id\":\"os\",\"triggers\":[{\"rsec\":3600}],"
                       "\"action\":{\"x\":1}}]";
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_EQUAL(1, schedule_test_sched(node)->count);

        char *before = __serialize_node_schedules_locked(node);
        TEST_ASSERT_NOT_NULL(before);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(before, "\"os\""), "present before firing");
        free(before);

        TEST_ASSERT_TRUE_MESSAGE(__node_remove_by_cloud_id_locked(node, "os"),
                                 "the fired schedule must be found and removed");
        TEST_ASSERT_EQUAL_MESSAGE(0, schedule_test_sched(node)->count, "the handle must be released");

        char *after = __serialize_node_schedules_locked(node);
        TEST_ASSERT_NOT_NULL(after);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("[]", after,
                                         "a fired one-shot must be gone from what we persist");
        free(after);
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_remove_by_cloud_id_removes_one(void)
{
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, SCHEDULE_TEST_THREE,
                                  strlen(SCHEDULE_TEST_THREE)));
        TEST_ASSERT_EQUAL(3, schedule_test_sched(node)->count);

        /* Remove the middle one; the other two must survive. */
        TEST_ASSERT_TRUE_MESSAGE(__node_remove_by_cloud_id_locked(node, "bb"),
                                 "removal must report success");
        TEST_ASSERT_EQUAL_MESSAGE(2, schedule_test_sched(node)->count, "count must drop by one");

        char *out = __serialize_node_schedules_locked(node);
        TEST_ASSERT_NOT_NULL(out);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"aa\""), "'aa' must survive");
        TEST_ASSERT_NULL_MESSAGE(strstr(out, "\"bb\""), "'bb' must be gone from the payload");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"cc\""), "'cc' must survive");
        free(out);
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_remove_by_cloud_id_unknown_is_noop(void)
{
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, SCHEDULE_TEST_THREE,
                                  strlen(SCHEDULE_TEST_THREE)));
        /* This is the concurrent-replacement case: the id the fire path carries
         * is no longer present, which must simply miss. */
        TEST_ASSERT_FALSE_MESSAGE(__node_remove_by_cloud_id_locked(node, "zz"),
                                  "unknown id must not report a removal");
        TEST_ASSERT_EQUAL_MESSAGE(3, schedule_test_sched(node)->count, "nothing may be removed");
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_remove_by_cloud_id_last_one_frees_array(void)
{
    const char *json = "[{\"enabled\":true,\"id\":\"only\",\"triggers\":[{\"d\":127,\"m\":300}],"
                       "\"action\":{\"x\":1}}]";
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        TEST_ASSERT_TRUE(__node_remove_by_cloud_id_locked(node, "only"));
        TEST_ASSERT_EQUAL(0, schedule_test_sched(node)->count);
        TEST_ASSERT_NULL_MESSAGE(schedule_test_sched(node)->handles,
                                 "the handle array must be freed once empty");

        char *out = __serialize_node_schedules_locked(node);
        TEST_ASSERT_NOT_NULL(out);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("[]", out, "an emptied node serializes to []");
        free(out);
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_remove_by_cloud_id_null_safe(void)
{
    esp_rmaker_node_t *node = schedule_test_node_setup();
    esp_rmaker_node_lock(node);
    if (TEST_PROTECT()) {
        TEST_ASSERT_FALSE(__node_remove_by_cloud_id_locked(node, NULL));
        TEST_ASSERT_FALSE(__node_remove_by_cloud_id_locked(node, ""));
        TEST_ASSERT_FALSE(__node_remove_by_cloud_id_locked(NULL, "aa"));
    }
    esp_rmaker_node_unlock(node);
    schedule_test_node_teardown(node);
}

void test_schedules_one_shot_flagged_in_priv_data(void)
{
    /* The fire path decides spent-ness from this flag alone, so it has to be
     * right for each shape: relative and bare-date are one-shot, a weekday mask
     * and a month mask are not. */
    esp_schedule_trigger_t t = {0};

    t.type = ESP_SCHEDULE_TYPE_RELATIVE;
    t.relative_seconds = 60;
    TEST_ASSERT_TRUE_MESSAGE(__trigger_is_one_shot(&t), "relative is always one-shot");

    memset(&t, 0, sizeof(t));
    t.type = ESP_SCHEDULE_TYPE_DAYS_OF_WEEK;
    t.day.repeat_days = ESP_SCHEDULE_DAY_ONCE;
    TEST_ASSERT_TRUE_MESSAGE(__trigger_is_one_shot(&t), "DAY_ONCE is one-shot");
    t.day.repeat_days = ESP_SCHEDULE_DAY_EVERYDAY;
    TEST_ASSERT_FALSE_MESSAGE(__trigger_is_one_shot(&t), "a weekday mask repeats");

    memset(&t, 0, sizeof(t));
    t.type = ESP_SCHEDULE_TYPE_DATE;
    t.date.day = 15;
    TEST_ASSERT_TRUE_MESSAGE(__trigger_is_one_shot(&t), "a date with no month mask is one-shot");
    t.date.repeat_months = ESP_SCHEDULE_MONTH_ALL;
    TEST_ASSERT_FALSE_MESSAGE(__trigger_is_one_shot(&t), "a month mask repeats");

    memset(&t, 0, sizeof(t));
    t.type = ESP_SCHEDULE_TYPE_INVALID;
    TEST_ASSERT_TRUE_MESSAGE(__trigger_is_one_shot(&t),
                             "an unusable trigger is treated as spent");
}

/* ==========================================================================
 * Persistence / reload tests
 *
 * The service owns its own persistence now: esp_schedule runs with NVS
 * disabled and the raw details JSON is stored per node (self ->
 * ``local_config``, child -> ``bridge_scheds``). Orphan parking is gone --
 * nothing is materialized until its owning node exists, so a child is simply
 * rebuilt from its own stored payload on ``on_child_added``.
 *
 * These tests cover the RAM-side contract, which needs no NVS: the reload of
 * a node with nothing stored, and handle release. The NVS round-trip itself
 * is not exercised here because the parse fixture does not bring up the
 * local_config NVS handle (the getters are NULL-safe, so a reload with no
 * handle is a clean no-op).
 * ========================================================================== */

void test_schedules_reload_no_stored_details_is_noop(void)
{
    esp_rmaker_node_t *node = schedule_test_node_setup();
    if (TEST_PROTECT()) {
        /* Nothing persisted for this bare node: reload must succeed and
         * install nothing rather than reporting an error. */
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __reload_for_node(node));
        TEST_ASSERT_EQUAL_MESSAGE(0, schedule_test_sched(node)->count,
                                  "Reload with nothing stored must install no schedules");
    }
    schedule_test_node_teardown(node);
}

void test_schedules_reload_null_node_rejected(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, __reload_for_node(NULL));
}

void test_schedules_drop_node_releases_handles(void)
{
    const char *json = "[{\"enabled\":true,\"id\":\"Keep\",\"triggers\":[{\"rsec\":3600}],"
                       "\"validity\":{},\"action\":{\"x\":1}}]";

    esp_rmaker_node_t *node = schedule_test_node_setup();
    if (TEST_PROTECT()) {
        esp_rmaker_node_lock(node);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        esp_rmaker_node_unlock(node);
        TEST_ASSERT_EQUAL_MESSAGE(1, schedule_test_sched(node)->count, "Setup should install one schedule");

        /* RAM-only release: the slice empties and the array is freed. */
        esp_rmaker_schedule_service_unload_node(node);
        TEST_ASSERT_EQUAL_MESSAGE(0, schedule_test_sched(node)->count,
                                  "drop_node must release every handle");
        TEST_ASSERT_NULL_MESSAGE(schedule_test_sched(node)->handles,
                                 "drop_node must free the handle array");
    }
    schedule_test_node_teardown(node);
}

void test_schedules_erase_node_releases_handles(void)
{
    const char *json = "[{\"enabled\":true,\"id\":\"Gone\",\"triggers\":[{\"rsec\":3600}],"
                       "\"validity\":{},\"action\":{\"x\":1}}]";

    esp_rmaker_node_t *node = schedule_test_node_setup();
    if (TEST_PROTECT()) {
        esp_rmaker_node_lock(node);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                          __build_schedule_details_for_node_locked(node, NULL, json, strlen(json)));
        esp_rmaker_node_unlock(node);
        TEST_ASSERT_EQUAL(1, schedule_test_sched(node)->count);

        /* Erase drops the handles and (with a live NVS handle) the stored
         * payload; here it must at least leave no live schedules behind. */
        esp_rmaker_schedule_service_erase_node(node);
        TEST_ASSERT_EQUAL_MESSAGE(0, schedule_test_sched(node)->count,
                                  "erase_node must release every handle");
        TEST_ASSERT_NULL(schedule_test_sched(node)->handles);
    }
    schedule_test_node_teardown(node);
}

void test_schedules_drop_and_erase_null_node_safe(void)
{
    esp_rmaker_schedule_service_unload_node(NULL);
    esp_rmaker_schedule_service_erase_node(NULL);
    /* Should not crash */
}
