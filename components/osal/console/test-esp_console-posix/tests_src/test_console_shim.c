/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "unity.h"
#include <esp_console.h>

#include "test_console_prototypes.h"

static int s_call_count;
static int s_last_argc;
static char s_last_arg1[64];
static int s_handler_ret;

static int capture_handler(int argc, char **argv)
{
    s_call_count++;
    s_last_argc = argc;
    s_last_arg1[0] = '\0';
    if (argc > 1) {
        strncpy(s_last_arg1, argv[1], sizeof(s_last_arg1) - 1);
        s_last_arg1[sizeof(s_last_arg1) - 1] = '\0';
    }
    return s_handler_ret;
}

static void reset_console(void)
{
    s_call_count = 0;
    s_last_argc = 0;
    s_last_arg1[0] = '\0';
    s_handler_ret = 0;
    TEST_ASSERT_EQUAL(ESP_OK, esp_console_init(NULL));
}

static esp_err_t register_capture(const char *name)
{
    const esp_console_cmd_t cmd = {
        .command = name,
        .help = "test command",
        .hint = NULL,
        .func = &capture_handler,
        .argtable = NULL,
    };
    return esp_console_cmd_register(&cmd);
}

void test_console_register_and_dispatch(void)
{
    reset_console();
    s_handler_ret = 42;
    TEST_ASSERT_EQUAL(ESP_OK, register_capture("set"));

    int cmd_ret = -1;
    esp_err_t err = esp_console_run("set hello world", &cmd_ret);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT(1, s_call_count);
    TEST_ASSERT_EQUAL_INT(3, s_last_argc);          /* "set", "hello", "world" */
    TEST_ASSERT_EQUAL_STRING("hello", s_last_arg1);
    TEST_ASSERT_EQUAL_INT(42, cmd_ret);             /* handler return propagated */
}

void test_console_unknown_command(void)
{
    reset_console();
    int cmd_ret = 0;
    esp_err_t err = esp_console_run("does-not-exist", &cmd_ret);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
    TEST_ASSERT_EQUAL_INT(1, cmd_ret);
    TEST_ASSERT_EQUAL_INT(0, s_call_count);
}

void test_console_empty_line(void)
{
    reset_console();
    TEST_ASSERT_EQUAL(ESP_OK, register_capture("set"));

    int cmd_ret = -1;
    esp_err_t err = esp_console_run("   \t  ", &cmd_ret);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT(0, cmd_ret);
    TEST_ASSERT_EQUAL_INT(0, s_call_count);         /* no handler invoked */
}

void test_console_replace_same_name(void)
{
    reset_console();
    /* Register the same command name twice; the second registration must win. */
    TEST_ASSERT_EQUAL(ESP_OK, register_capture("dup"));
    TEST_ASSERT_EQUAL(ESP_OK, register_capture("dup"));

    int cmd_ret = 0;
    TEST_ASSERT_EQUAL(ESP_OK, esp_console_run("dup", &cmd_ret));
    TEST_ASSERT_EQUAL_INT(1, s_call_count);         /* dispatched exactly once */
}

void test_console_help_command(void)
{
    reset_console();
    TEST_ASSERT_EQUAL(ESP_OK, esp_console_register_help_command());
    TEST_ASSERT_EQUAL(ESP_OK, register_capture("set"));

    /* The built-in help command is dispatchable and returns success. */
    int cmd_ret = -1;
    esp_err_t err = esp_console_run("help", &cmd_ret);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT(0, cmd_ret);
    TEST_ASSERT_EQUAL_INT(0, s_call_count);         /* help is not the capture handler */
}
