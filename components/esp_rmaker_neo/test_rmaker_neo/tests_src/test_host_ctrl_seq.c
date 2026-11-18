/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_host_ctrl_seq.c
 * @brief Unit tests for the host_ctrl correlation id on commands and responses.
 *
 * Built only without CONFIG_ESP_RMAKER_NEO_HOST_CTRL, so this file can compile the
 * response code in and capture what it writes to ext-io.
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Capture the ext-io writes of the included response code. */
#define osal_ext_io_write_line __test_seq_write_line
#include "host_ctrl/host_ctrl_processing_impl.c"
#undef osal_ext_io_write_line

/* Helpers ******************************************************************/

static char __written[64];
static size_t __written_length;

bool __test_seq_write_line(const char *line, size_t line_length)
{
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(__written), line_length);
    memcpy(__written, line, line_length);
    __written_length = line_length;
    return true;
}

static void __seq_test_setup(void)
{
    memset(__written, 0, sizeof(__written));
    __written_length = 0;
    esp_rmaker_host_ctrl_set_response_seq(NULL);
}

static void __assert_written(const char *expected)
{
    TEST_ASSERT_EQUAL_size_t(strlen(expected), __written_length);
    TEST_ASSERT_EQUAL_MEMORY(expected, __written, __written_length);
}

/* Tests ********************************************************************/

void test_host_ctrl_seq_response_untagged(void)
{
    __seq_test_setup();

    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, "abc", 3);
    __assert_written("oabc\r");

    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
    __assert_written("e\r");
}

void test_host_ctrl_seq_response_echoes_id(void)
{
    __seq_test_setup();

    esp_rmaker_host_ctrl_set_response_seq("00A1");
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, "abc", 3);
    __assert_written("#00A1oabc\r");

    /* The id stays set for every response to the same command. */
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
    __assert_written("#00A1o\r");

    esp_rmaker_host_ctrl_set_response_seq(NULL);
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
    __assert_written("o\r");
}

void test_host_ctrl_seq_take_strips_id(void)
{
    __seq_test_setup();

    uint8_t command[] = "#00A1p";
    uint8_t *buffer = command;
    size_t buffer_length = sizeof(command) - 1;

    esp_rmaker_host_ctrl_take_response_seq(&buffer, &buffer_length);
    TEST_ASSERT_EQUAL_PTR(command + 5, buffer);
    TEST_ASSERT_EQUAL_size_t(1, buffer_length);
    TEST_ASSERT_EQUAL_CHAR(RMAKER_HOST_CTRL_COMMAND_CHAR_PING, buffer[0]);

    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
    __assert_written("#00A1o\r");
}

void test_host_ctrl_seq_take_leaves_untagged_command(void)
{
    __seq_test_setup();

    /* A previous command's id must not carry over to an untagged one. */
    esp_rmaker_host_ctrl_set_response_seq("FFFF");

    uint8_t command[] = "p";
    uint8_t *buffer = command;
    size_t buffer_length = sizeof(command) - 1;

    esp_rmaker_host_ctrl_take_response_seq(&buffer, &buffer_length);
    TEST_ASSERT_EQUAL_PTR(command, buffer);
    TEST_ASSERT_EQUAL_size_t(1, buffer_length);

    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
    __assert_written("o\r");
}

void test_host_ctrl_seq_take_ignores_id_without_command(void)
{
    __seq_test_setup();

    /* An id with no command after it is not stripped. */
    uint8_t command[] = "#00A1";
    uint8_t *buffer = command;
    size_t buffer_length = sizeof(command) - 1;

    esp_rmaker_host_ctrl_take_response_seq(&buffer, &buffer_length);
    TEST_ASSERT_EQUAL_PTR(command, buffer);
    TEST_ASSERT_EQUAL_size_t(5, buffer_length);

    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
    __assert_written("i\r");
}
