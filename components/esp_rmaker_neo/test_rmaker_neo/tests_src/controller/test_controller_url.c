/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_controller_url.c
 * @brief Tests for the base-URL split in controller_parse.c.
 *
 * Every data-API path is built as base_path + "/v1/...", so a stage path that keeps its trailing
 * slash produces "//v1/..." and a 403 from API Gateway rather than an obvious failure.
 */

#include <string.h>

#include "unity.h"

#include "controller_internal.h"

static char s_host[CONTROLLER_HOST_MAX_LEN];
static char s_path[CONTROLLER_BASE_PATH_MAX_LEN];

static void split(const char *url)
{
    memset(s_host, 'x', sizeof(s_host));
    memset(s_path, 'x', sizeof(s_path));
    controller_parse_base_url(url, s_host, sizeof(s_host), s_path, sizeof(s_path));
}

void test_controller_url_splits_host_and_stage_path(void)
{
    split("https://abc123.execute-api.us-east-1.amazonaws.com/prod");
    TEST_ASSERT_EQUAL_STRING("abc123.execute-api.us-east-1.amazonaws.com", s_host);
    TEST_ASSERT_EQUAL_STRING("/prod", s_path);
}

void test_controller_url_without_path_yields_empty_base_path(void)
{
    split("https://api.example.com");
    TEST_ASSERT_EQUAL_STRING("api.example.com", s_host);
    TEST_ASSERT_EQUAL_STRING("", s_path);
}

void test_controller_url_strips_one_trailing_slash(void)
{
    /* The regression this guards: "/prod/" + "/v1/x" is "//v1/x", which the API rejects. */
    split("https://api.example.com/prod/");
    TEST_ASSERT_EQUAL_STRING("api.example.com", s_host);
    TEST_ASSERT_EQUAL_STRING("/prod", s_path);
}

void test_controller_url_without_scheme_is_tolerated(void)
{
    split("api.example.com/prod");
    TEST_ASSERT_EQUAL_STRING("api.example.com", s_host);
    TEST_ASSERT_EQUAL_STRING("/prod", s_path);
}

void test_controller_url_truncates_an_oversized_host(void)
{
    char host[8];
    char path[8];
    controller_parse_base_url("https://averylonghost.example.com/prod", host, sizeof(host),
                              path, sizeof(path));
    TEST_ASSERT_EQUAL_STRING("averylo", host);
    TEST_ASSERT_EQUAL_STRING("/prod", path);
}

void test_controller_url_null_yields_empty_outputs(void)
{
    split(NULL);
    TEST_ASSERT_EQUAL_STRING("", s_host);
    TEST_ASSERT_EQUAL_STRING("", s_path);
}
