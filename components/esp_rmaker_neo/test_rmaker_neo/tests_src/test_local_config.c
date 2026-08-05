/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_local_config.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <string.h>
#include <stdlib.h>

#include "esp_rmaker_core.h"
#include "local_config.h"
#include "esp_rmaker_credentials_access.h"
#include "constants/cloud.h"
#include "osal_storage.h"

static esp_rmaker_node_t *__node = NULL;

static void __setup(void)
{
    esp_rmaker_config_t config = {
        .enable_time_sync = false,
    };
    __node = esp_rmaker_node_init(&config, "test_node", "test_type");
    TEST_ASSERT_NOT_NULL(__node);
}

static void __teardown(void)
{
    esp_rmaker_node_deinit(__node);
    __node = NULL;
}


void test_local_config_group_info_format(void)
{
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE] = "grp";
    /* subgroup buffer allows up to 3 chars (+NUL). Use 2-char values. */
    char subgroups[3][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE] = {
        "sg2", "sg1", "sg3"
    };
    char *group = NULL;

    esp_rmaker_error_t err = esp_rmaker_local_config_format_group_info_str(primary, subgroups, 3, &group);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(group);
    /* subgroups should be sorted lexicographically */
    TEST_ASSERT_EQUAL_STRING("grp-sg1-sg2-sg3", group);
    free(group);
}

void test_local_config_group_info_empty(void)
{
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE] = "";
    char subgroups[3][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE] = { "", "", "" };
    char *group = NULL;
    esp_rmaker_error_t err = esp_rmaker_local_config_format_group_info_str(primary, subgroups, 3, &group);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL_MESSAGE(group, "Group string should not be NULL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(group, "", "Group string should be empty");

    __setup();
    err = esp_rmaker_local_config_set_group_info_str(group);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    char *group2 = esp_rmaker_local_config_get_group_info_str();
    TEST_ASSERT_NOT_NULL_MESSAGE(group2, "Group string from local config should not be NULL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(group, group2, "Group strings from local config should be equal");
    free(group2);
    free(group);
    __teardown();
}

void test_local_config_group_info_format_skips_empty_subgroups(void)
{
    /* The sizing pass ignores empty subgroups, so the copy pass must too. Emitting a
     * separator for one that contributed nothing overruns the allocation by a byte
     * each - and the cloud does send these: getGroupInfo substitutes "" for any
     * "subgrps" element it cannot read as a string. */
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE] = "grp";
    char subgroups[3][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE] = { "sg1", "", "" };
    char *group = NULL;

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_local_config_format_group_info_str(primary, subgroups, 3, &group));
    TEST_ASSERT_NOT_NULL(group);
    TEST_ASSERT_EQUAL_STRING("grp-sg1", group);
    /* Exactly the allocated length: a stray separator would show up here. */
    TEST_ASSERT_EQUAL_UINT(strlen("grp-sg1"), strlen(group));
    free(group);

    /* Every subgroup empty, but a non-empty primary - the all-empty case short-circuits
     * on a zero total length and never reaches the copy loop. */
    char only_empty[3][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE] = { "", "", "" };
    group = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_local_config_format_group_info_str(primary, only_empty, 3, &group));
    TEST_ASSERT_NOT_NULL(group);
    TEST_ASSERT_EQUAL_STRING("grp", group);
    free(group);
}

void test_local_config_group_info_parse_packs_subgroups(void)
{
    /* "grp1--sgY" has an empty segment. Indexing subgroups by segment position would
     * skip slot 0 while still counting it, handing back an uninitialised subgroup. */
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
    char subgroups[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE];
    size_t num_subgroups = 0;

    memset(subgroups, 0xAA, sizeof(subgroups));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_local_config_parse_group_info_str("grp1--sgY", primary, sizeof(primary),
                              subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, &num_subgroups));
    TEST_ASSERT_EQUAL_STRING("grp1", primary);
    /* One real subgroup, packed into slot 0 - not two with slot 0 untouched. */
    TEST_ASSERT_EQUAL_UINT(1, num_subgroups);
    TEST_ASSERT_EQUAL_STRING("sgY", subgroups[0]);

    /* Trailing separator: "grp1-sgX-" must not count a phantom subgroup either. */
    memset(subgroups, 0xAA, sizeof(subgroups));
    num_subgroups = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_local_config_parse_group_info_str("grp1-sgX-", primary, sizeof(primary),
                              subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, &num_subgroups));
    TEST_ASSERT_EQUAL_UINT(1, num_subgroups);
    TEST_ASSERT_EQUAL_STRING("sgX", subgroups[0]);
}

void test_local_config_group_info_set_get(void)
{
    __setup();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_group_info_str("grp-a-b"));
    char *group = esp_rmaker_local_config_get_group_info_str();
    TEST_ASSERT_NOT_NULL(group);
    TEST_ASSERT_EQUAL_STRING("grp-a-b", group);
    free(group);
    __teardown();
}

void test_local_config_other_accessors(void)
{
    __setup();
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    osal_mqtt_conn_params_t *params;
    err = esp_rmaker_credentials_get_mqtt_conn_params(&params);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(params);

    /* Retrieve via thing name and private key */
    char *thing_name;
    err = esp_rmaker_credentials_get_thing_name(&thing_name);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(thing_name);
    TEST_ASSERT_EQUAL_STRING(params->client_id, thing_name);
    free(thing_name);
    esp_rmaker_credential_t private_key;
    err = esp_rmaker_credentials_get_private_key(&private_key);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(private_key.credential);
    TEST_ASSERT_EQUAL_MEMORY(params->client_key, private_key.credential, private_key.len);
    esp_rmaker_credentials_free_credential(&private_key);

    esp_rmaker_credentials_free_mqtt_conn_params(params);

    /* alexa flag */
    esp_rmaker_local_config_set_alexa_en(true);
    TEST_ASSERT_TRUE(esp_rmaker_local_config_get_alexa_en());

    /* sched/trigger versions */
    esp_rmaker_local_config_set_sched_ver(5);
    esp_rmaker_local_config_set_trigger_ver(9);
    TEST_ASSERT_EQUAL(5, esp_rmaker_local_config_get_sched_ver());
    TEST_ASSERT_EQUAL(9, esp_rmaker_local_config_get_trigger_ver());

    /* sched details (string) */
    esp_rmaker_local_config_set_sched_details("sched_details");
    char *sched_details = esp_rmaker_local_config_get_sched_details();
    TEST_ASSERT_NOT_NULL(sched_details);
    TEST_ASSERT_EQUAL_STRING("sched_details", sched_details);
    free(sched_details);

    /* trigger details (binary blob, may contain NULs) */
    const uint8_t trig_blob[] = {0x01, 0x00, 0xFE, 0x42};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_trigger_details(trig_blob, sizeof(trig_blob)));
    size_t trig_len = 0;
    uint8_t *trigger_details = esp_rmaker_local_config_get_trigger_details(&trig_len);
    TEST_ASSERT_NOT_NULL(trigger_details);
    TEST_ASSERT_EQUAL_UINT32(sizeof(trig_blob), (uint32_t)trig_len);
    TEST_ASSERT_EQUAL_MEMORY(trig_blob, trigger_details, trig_len);
    free(trigger_details);

    __teardown();
}

void test_local_config_group_info_invalid_args(void)
{
    char subgroups[1][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE] = { "sg" };
    char *group = NULL;

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_config_format_group_info_str(NULL, subgroups, 1, &group));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_config_format_group_info_str("grp", subgroups, 1, NULL));
}

void test_local_config_group_info_null_subgroups(void)
{
    __setup();
    char *group = NULL;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_format_group_info_str("primary", NULL, 0, &group));
    TEST_ASSERT_NOT_NULL(group);
    TEST_ASSERT_EQUAL_STRING("primary", group);
    free(group);
    __teardown();
}

void test_local_config_alexa_flag_default(void)
{
    __setup();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, osal_storage_erase(esp_rmaker_local_config_nvs_handle, RMAKER_NVS_LOCAL_CONFIG_KEY_ALEXA_EN));
    TEST_ASSERT_FALSE(esp_rmaker_local_config_get_alexa_en());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_alexa_en(true));
    TEST_ASSERT_TRUE(esp_rmaker_local_config_get_alexa_en());
    __teardown();
}

void test_local_config_sched_trigger_details_roundtrip(void)
{
    __setup();

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_sched_details("sched-details"));
    char *sched_details = esp_rmaker_local_config_get_sched_details();
    TEST_ASSERT_NOT_NULL(sched_details);
    TEST_ASSERT_EQUAL_STRING("sched-details", sched_details);
    free(sched_details);

    const uint8_t trig_blob[] = {0xAA, 0x00, 0xBB, 0x00, 0xCC};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_local_config_set_trigger_details(trig_blob, sizeof(trig_blob)));
    size_t trig_len = 0;
    uint8_t *trigger_details = esp_rmaker_local_config_get_trigger_details(&trig_len);
    TEST_ASSERT_NOT_NULL(trigger_details);
    TEST_ASSERT_EQUAL_UINT32(sizeof(trig_blob), (uint32_t)trig_len);
    TEST_ASSERT_EQUAL_MEMORY(trig_blob, trigger_details, trig_len);
    free(trigger_details);

    __teardown();
}

/* --- Parse group info string tests --- */

void test_local_config_parse_group_info_primary_only(void)
{
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
    char subgroups[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE];
    size_t num_subgroups = 0;

    esp_rmaker_error_t err = esp_rmaker_local_config_parse_group_info_str("grp1", primary, sizeof(primary), subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, &num_subgroups);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_EQUAL_STRING("grp1", primary);
    TEST_ASSERT_EQUAL(0, num_subgroups);
}

void test_local_config_parse_group_info_with_subgroups(void)
{
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
    char subgroups[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE];
    size_t num_subgroups = 0;

    esp_rmaker_error_t err = esp_rmaker_local_config_parse_group_info_str("grp1-sgX-sgY", primary, sizeof(primary), subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, &num_subgroups);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_EQUAL_STRING("grp1", primary);
    TEST_ASSERT_EQUAL(2, num_subgroups);
    TEST_ASSERT_EQUAL_STRING("sgX", subgroups[0]);
    TEST_ASSERT_EQUAL_STRING("sgY", subgroups[1]);
}

void test_local_config_parse_group_info_empty_and_null(void)
{
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
    char subgroups[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE];
    size_t num_subgroups = 99;

    /* Empty string */
    esp_rmaker_error_t err = esp_rmaker_local_config_parse_group_info_str("", primary, sizeof(primary), subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, &num_subgroups);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_EQUAL_STRING("", primary);
    TEST_ASSERT_EQUAL(0, num_subgroups);

    /* NULL string */
    num_subgroups = 99;
    err = esp_rmaker_local_config_parse_group_info_str(NULL, primary, sizeof(primary), subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, &num_subgroups);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_EQUAL_STRING("", primary);
    TEST_ASSERT_EQUAL(0, num_subgroups);
}

void test_local_config_parse_group_info_invalid_args(void)
{
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
    char subgroups[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE];
    size_t num_subgroups = 0;

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_config_parse_group_info_str("grp", NULL, sizeof(primary), subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, &num_subgroups));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_config_parse_group_info_str("grp", primary, 0, subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, &num_subgroups));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_local_config_parse_group_info_str("grp", primary, sizeof(primary), subgroups, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, NULL));
}

void test_local_config_parse_group_info_roundtrip(void)
{
    /* Format then parse: result should match (format sorts subgroups) */
    char primary_fmt[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE] = "grp";
    char subgroups_fmt[3][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE] = { "sg2", "sg1", "sg3" };
    char *group_str = NULL;

    esp_rmaker_error_t err = esp_rmaker_local_config_format_group_info_str(primary_fmt, subgroups_fmt, 3, &group_str);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(group_str);
    TEST_ASSERT_EQUAL_STRING("grp-sg1-sg2-sg3", group_str);

    char primary_out[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
    char subgroups_out[RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE];
    size_t num_subgroups = 0;

    err = esp_rmaker_local_config_parse_group_info_str(group_str, primary_out, sizeof(primary_out), subgroups_out, RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT, &num_subgroups);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_EQUAL_STRING("grp", primary_out);
    TEST_ASSERT_EQUAL(3, num_subgroups);
    TEST_ASSERT_EQUAL_STRING("sg1", subgroups_out[0]);
    TEST_ASSERT_EQUAL_STRING("sg2", subgroups_out[1]);
    TEST_ASSERT_EQUAL_STRING("sg3", subgroups_out[2]);

    free(group_str);
}

void test_local_config_parse_group_info_max_subgroups(void)
{
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
    char subgroups[2][RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE]; /* only 2 slots */
    size_t num_subgroups = 0;

    /* String has 3 subgroups but max_subgroups=2: only first 2 are parsed */
    esp_rmaker_error_t err = esp_rmaker_local_config_parse_group_info_str("grp-a-b-c", primary, sizeof(primary), subgroups, 2, &num_subgroups);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_EQUAL_STRING("grp", primary);
    TEST_ASSERT_EQUAL(2, num_subgroups);
    TEST_ASSERT_EQUAL_STRING("a", subgroups[0]);
    TEST_ASSERT_EQUAL_STRING("b", subgroups[1]);
}

void test_local_config_parse_group_info_null_subgroups(void)
{
    char primary[RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE];
    size_t num_subgroups = 99;

    /* subgroups=NULL, max_subgroups=0: primary still parsed, num_subgroups stays 0 */
    esp_rmaker_error_t err = esp_rmaker_local_config_parse_group_info_str("grp1-sgX", primary, sizeof(primary), NULL, 0, &num_subgroups);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_EQUAL_STRING("grp1", primary);
    TEST_ASSERT_EQUAL(0, num_subgroups);
}
