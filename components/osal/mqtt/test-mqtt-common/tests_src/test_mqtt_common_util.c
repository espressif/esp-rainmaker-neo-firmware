/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_mqtt_common_util.c
 * @brief Unit tests for osal_mqtt_util (topic matching).
 */

#include "unity.h"
#include <string.h>

#include "osal_mqtt_util.h"

/* Exact match */
void test_mqtt_common_match_topic_exact(void)
{
    const char *topic = "a/b/c";
    const char *filter = "a/b/c";
    TEST_ASSERT_TRUE(osal_mqtt_match_topic(topic, strlen(topic), filter, strlen(filter)));
}

void test_mqtt_common_match_topic_exact_mismatch(void)
{
    const char *topic = "a/b/c";
    const char *filter = "a/b/d";
    TEST_ASSERT_FALSE(osal_mqtt_match_topic(topic, strlen(topic), filter, strlen(filter)));
}

void test_mqtt_common_match_topic_topic_longer_than_filter(void)
{
    const char *topic = "a/b/c";
    const char *filter = "a/b";
    TEST_ASSERT_FALSE(osal_mqtt_match_topic(topic, strlen(topic), filter, strlen(filter)));
}

void test_mqtt_common_match_topic_filter_longer_than_topic(void)
{
    const char *topic = "a/b";
    const char *filter = "a/b/c";
    TEST_ASSERT_FALSE(osal_mqtt_match_topic(topic, strlen(topic), filter, strlen(filter)));
}

/* Single-level wildcard '+' */
void test_mqtt_common_match_topic_single_level_plus(void)
{
    const char *filter = "a/+/c";
    size_t filter_len = strlen(filter);
    TEST_ASSERT_TRUE(osal_mqtt_match_topic("a/x/c", 5, filter, filter_len));
    TEST_ASSERT_TRUE(osal_mqtt_match_topic("a/b/c", 5, filter, filter_len));
    TEST_ASSERT_TRUE(osal_mqtt_match_topic("a//c", 4, filter, filter_len));
    TEST_ASSERT_FALSE(osal_mqtt_match_topic("a/c", 3, filter, filter_len));
    TEST_ASSERT_FALSE(osal_mqtt_match_topic("a/x/y", 5, filter, filter_len));
}

void test_mqtt_common_match_topic_plus_at_start(void)
{
    const char *topic = "x/b/c";
    const char *filter = "+/b/c";
    TEST_ASSERT_TRUE(osal_mqtt_match_topic(topic, strlen(topic), filter, strlen(filter)));
}

void test_mqtt_common_match_topic_plus_at_end(void)
{
    const char *topic = "a/b/x";
    const char *filter = "a/b/+";
    TEST_ASSERT_TRUE(osal_mqtt_match_topic(topic, strlen(topic), filter, strlen(filter)));
}

/* Multi-level wildcard '#' */
void test_mqtt_common_match_topic_multilevel_hash(void)
{
    const char *filter = "a/#";
    size_t filter_len = strlen(filter);
    TEST_ASSERT_TRUE(osal_mqtt_match_topic("a", 1, filter, filter_len));
    TEST_ASSERT_TRUE(osal_mqtt_match_topic("a/b", 3, filter, filter_len));
    TEST_ASSERT_TRUE(osal_mqtt_match_topic("a/b/c", 5, filter, filter_len));
    TEST_ASSERT_FALSE(osal_mqtt_match_topic("b", 1, filter, filter_len));
    TEST_ASSERT_FALSE(osal_mqtt_match_topic("x/a", 3, filter, filter_len));
}

void test_mqtt_common_match_topic_hash_only(void)
{
    const char *filter = "#";
    size_t filter_len = 1;
    TEST_ASSERT_TRUE(osal_mqtt_match_topic("", 0, filter, filter_len));
    TEST_ASSERT_TRUE(osal_mqtt_match_topic("a", 1, filter, filter_len));
    TEST_ASSERT_TRUE(osal_mqtt_match_topic("a/b/c", 5, filter, filter_len));
}

void test_mqtt_common_match_topic_hash_must_be_last(void)
{
    /* '#' not at end of filter - treated as literal in our impl (no match for "a/#" as topic) */
    const char *topic = "a/b";
    const char *filter = "a/#/c";
    /* Filter "a/#/c": after '#' we require end of filter, so "a/#/c" doesn't match "a/b" */
    TEST_ASSERT_FALSE(osal_mqtt_match_topic(topic, strlen(topic), filter, strlen(filter)));
}

void test_mqtt_common_match_topic_empty_topic_empty_filter(void)
{
    TEST_ASSERT_TRUE(osal_mqtt_match_topic("", 0, "", 0));
}

void test_mqtt_common_match_topic_empty_filter_non_empty_topic(void)
{
    TEST_ASSERT_FALSE(osal_mqtt_match_topic("a", 1, "", 0));
}

void test_mqtt_common_match_topic_length_limits(void)
{
    /* Use lengths shorter than strlen to test length-based boundaries */
    const char *topic = "a/b/c";
    const char *filter = "a/b/c";
    TEST_ASSERT_FALSE(osal_mqtt_match_topic(topic, 2, filter, 5)); /* topic only "a/" */
    TEST_ASSERT_TRUE(osal_mqtt_match_topic(topic, 5, filter, 5));
}
