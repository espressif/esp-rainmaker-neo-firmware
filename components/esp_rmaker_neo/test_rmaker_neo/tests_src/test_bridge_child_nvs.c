/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_bridge_child_nvs.c
 * @brief Unit tests for the per-child NVS persistence module.
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include "bridge/bridge_child_nvs.h"
#include "bridge/bridge_internal.h"
#include "constants/nvs.h"
#include "constants/identity.h"

#include "osal_storage.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TEST_PARTITION RMAKER_NVS_PART_NAME
#define TEST_NAMESPACE RMAKER_NVS_BRIDGE_CHILDREN_NAMESPACE

/* Tests fabricate child handles on the stack: a handle is just a
 * pointer to ``struct esp_rmaker_bridge_child``, and bridge_child_nvs
 * only reads the slot's pre-computed ``nvs_key`` (via the accessor).
 * Compute the same key the production allocator would, so two distinct
 * fabricated handles for the same ``bridge_local_id`` collide on the
 * same NVS entry just like the real path. */
static struct esp_rmaker_bridge_child __mk_child(const char *local_id)
{
    struct esp_rmaker_bridge_child c;
    memset(&c, 0, sizeof(c));
    c.bridge_local_id = (char *)local_id;
    (void)bridge_internal_compute_nvs_key(local_id, c.nvs_key, sizeof(c.nvs_key));
    return c;
}

static void __reset_namespace(void)
{
    osal_storage_init(TEST_PARTITION);
    osal_storage_handle_t h;
    if (osal_storage_open(TEST_PARTITION, TEST_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &h) == OSAL_ERR_OK) {
        osal_storage_erase_all(h);
        osal_storage_commit(h);
        osal_storage_close(h);
    }
}

static void __setup(void)
{
    __reset_namespace();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_init());
}

/* Called at the end of every test to clear the namespace and prevent
 * one test's state from leaking into the next. (Tests that hit a failed
 * assertion longjmp past this; the next test's __setup() re-clears, so
 * isolation holds either way.) */
static void __teardown(void)
{
    __reset_namespace();
}

/* Load on absent -> NOT_FOUND ************************************************/

void test_bridge_child_nvs_load_absent_returns_not_found(void)
{
    __setup();
    struct esp_rmaker_bridge_child c = __mk_child("absent_id");
    bridge_child_nvs_record_t r;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, bridge_child_nvs_load(&c, &r));
    __teardown();
}

/* Store + load round-trip ***************************************************/

void test_bridge_child_nvs_store_load_roundtrip(void)
{
    __setup();
    bridge_child_nvs_record_t in;
    memset(&in, 0, sizeof(in));
    in.sched_ver = 42;
    in.trigger_ver = 7;
    in.ncfg_checksum_set = 1;
    for (int i = 0; i < BRIDGE_CHILD_NCFG_CHECKSUM_LEN; i++) {
        in.ncfg_checksum[i] = (uint8_t)i;
    }
    struct esp_rmaker_bridge_child c = __mk_child("lid1");

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_store(&c, &in));

    bridge_child_nvs_record_t out;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_load(&c, &out));
    TEST_ASSERT_EQUAL(BRIDGE_CHILD_NVS_VERSION, out.nvs_version);
    TEST_ASSERT_EQUAL_INT32(42, out.sched_ver);
    TEST_ASSERT_EQUAL_INT32(7, out.trigger_ver);
    TEST_ASSERT_EQUAL_UINT8(1, out.ncfg_checksum_set);
    for (int i = 0; i < BRIDGE_CHILD_NCFG_CHECKSUM_LEN; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, out.ncfg_checksum[i]);
    }
    __teardown();
}

/* Atomic RMW: set_sched_ver preserves other fields **************************/

void test_bridge_child_nvs_set_sched_ver_preserves_other_fields(void)
{
    __setup();
    bridge_child_nvs_record_t in;
    memset(&in, 0, sizeof(in));
    in.sched_ver = -1;
    in.trigger_ver = 9;
    struct esp_rmaker_bridge_child c = __mk_child("lid2");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_store(&c, &in));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_set_sched_ver(&c, 123));

    bridge_child_nvs_record_t out;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_load(&c, &out));
    TEST_ASSERT_EQUAL_INT32(123, out.sched_ver);
    TEST_ASSERT_EQUAL_INT32(9, out.trigger_ver);
    __teardown();
}

/* RMW on absent entry creates fresh record with unset sentinels *************/

void test_bridge_child_nvs_set_sched_ver_creates_when_absent(void)
{
    __setup();
    struct esp_rmaker_bridge_child c = __mk_child("brand_new");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_set_sched_ver(&c, 5));

    bridge_child_nvs_record_t out;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_load(&c, &out));
    TEST_ASSERT_EQUAL_INT32(5, out.sched_ver);
    TEST_ASSERT_EQUAL_INT32(-1, out.trigger_ver);
    TEST_ASSERT_EQUAL_UINT8(0, out.ncfg_checksum_set);
    __teardown();
}

/* set_node_config writes the checksum (the ncfg_ver change-token) ***********/

void test_bridge_child_nvs_set_node_config_writes_both(void)
{
    __setup();
    uint8_t cs[BRIDGE_CHILD_NCFG_CHECKSUM_LEN];
    for (int i = 0; i < BRIDGE_CHILD_NCFG_CHECKSUM_LEN; i++) {
        cs[i] = (uint8_t)(0xA0 + i);
    }
    struct esp_rmaker_bridge_child c = __mk_child("lid3");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_set_node_config(&c, cs));

    bridge_child_nvs_record_t out;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_load(&c, &out));
    TEST_ASSERT_EQUAL_UINT8(1, out.ncfg_checksum_set);
    for (int i = 0; i < BRIDGE_CHILD_NCFG_CHECKSUM_LEN; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(0xA0 + i), out.ncfg_checksum[i]);
    }
    __teardown();
}

/* erase actually removes the entry ******************************************/

void test_bridge_child_nvs_erase(void)
{
    __setup();
    struct esp_rmaker_bridge_child c = __mk_child("lid5");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_set_sched_ver(&c, 1));

    bridge_child_nvs_record_t out;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_load(&c, &out));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_erase(&c));
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, bridge_child_nvs_load(&c, &out));
    __teardown();
}

/* Stale on-disk version invalidates the entry on next load ******************/

void test_bridge_child_nvs_invalidates_on_version_mismatch(void)
{
    __setup();
    /* Write a normal record then directly corrupt nvs_version. */
    bridge_child_nvs_record_t in;
    memset(&in, 0, sizeof(in));
    in.sched_ver = 11;
    in.trigger_ver = -1;
    struct esp_rmaker_bridge_child c = __mk_child("lid6");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_store(&c, &in));

    /* Tamper: overwrite the blob with a bumped version byte. Write
     * under the same derived NVS key the production code uses. */
    in.nvs_version = (uint8_t)(BRIDGE_CHILD_NVS_VERSION + 1);
    osal_storage_handle_t h;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_open(TEST_PARTITION, TEST_NAMESPACE,
                      OSAL_STORAGE_OPEN_READWRITE, &h));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_set(h, c.nvs_key, &in, sizeof(in), OSAL_STORAGE_TYPE_BINARY));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_commit(h));
    osal_storage_close(h);

    /* Load should detect the version mismatch, erase the entry, and
     * return NOT_FOUND. */
    bridge_child_nvs_record_t out;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, bridge_child_nvs_load(&c, &out));

    /* And a subsequent load is still NOT_FOUND (erased, not re-read). */
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, bridge_child_nvs_load(&c, &out));
    __teardown();
}

/* Wrong-size blob is invalidated on next load *******************************/

void test_bridge_child_nvs_invalidates_on_size_mismatch(void)
{
    __setup();
    struct esp_rmaker_bridge_child c = __mk_child("lid7");
    /* Write a too-short blob directly under the derived NVS key. */
    uint8_t stub[8] = { BRIDGE_CHILD_NVS_VERSION, 0, 0, 0, 0, 0, 0, 0 };
    osal_storage_handle_t h;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_open(TEST_PARTITION, TEST_NAMESPACE,
                      OSAL_STORAGE_OPEN_READWRITE, &h));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_set(h, c.nvs_key, stub, sizeof(stub), OSAL_STORAGE_TYPE_BINARY));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_commit(h));
    osal_storage_close(h);
    bridge_child_nvs_record_t out;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, bridge_child_nvs_load(&c, &out));
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, bridge_child_nvs_load(&c, &out));
    __teardown();
}

/* Long local_ids that share a 15-char prefix do NOT collide ****************/

void test_bridge_child_nvs_long_local_id_no_truncation_collision(void)
{
    __setup();
    /* Two ids that share the first RMAKER_NVS_KEY_LEN_MAX characters
     * and diverge thereafter. Prior implementation truncated to the
     * cap and collided; SHA-256-derived keys make them distinct. */
    char a[RMAKER_NVS_KEY_LEN_MAX + 5];
    char b[RMAKER_NVS_KEY_LEN_MAX + 5];
    for (int i = 0; i < RMAKER_NVS_KEY_LEN_MAX; i++) {
        a[i] = 'x';
        b[i] = 'x';
    }
    a[RMAKER_NVS_KEY_LEN_MAX] = 'A';
    a[RMAKER_NVS_KEY_LEN_MAX + 1] = '\0';
    b[RMAKER_NVS_KEY_LEN_MAX] = 'B';
    b[RMAKER_NVS_KEY_LEN_MAX + 1] = '\0';

    struct esp_rmaker_bridge_child ca = __mk_child(a);
    struct esp_rmaker_bridge_child cb = __mk_child(b);

    /* Derived keys must differ. */
    TEST_ASSERT_TRUE(strcmp(ca.nvs_key, cb.nvs_key) != 0);
    /* Both within the NVS cap. */
    TEST_ASSERT_TRUE(strlen(ca.nvs_key) <= RMAKER_NVS_KEY_LEN_MAX);
    TEST_ASSERT_TRUE(strlen(cb.nvs_key) <= RMAKER_NVS_KEY_LEN_MAX);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_set_sched_ver(&ca, 10));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_set_sched_ver(&cb, 20));

    bridge_child_nvs_record_t out;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_load(&ca, &out));
    TEST_ASSERT_EQUAL_INT32(10, out.sched_ver);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_child_nvs_load(&cb, &out));
    TEST_ASSERT_EQUAL_INT32(20, out.sched_ver);

    __teardown();
}

/* Derived NVS key is deterministic + within the cap ************************/

void test_bridge_child_nvs_compute_key(void)
{
    char key1[RMAKER_NVS_KEY_LEN_MAX + 1];
    char key2[RMAKER_NVS_KEY_LEN_MAX + 1];

    /* Same input -> same key. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_internal_compute_nvs_key("lid_same", key1, sizeof(key1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_internal_compute_nvs_key("lid_same", key2, sizeof(key2)));
    TEST_ASSERT_EQUAL_STRING(key1, key2);

    /* Always exactly 14 hex chars + NUL. */
    TEST_ASSERT_EQUAL_size_t(14, strlen(key1));

    /* Different inputs -> different keys (with overwhelming probability). */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridge_internal_compute_nvs_key("lid_other", key2, sizeof(key2)));
    TEST_ASSERT_TRUE(strcmp(key1, key2) != 0);

    /* Invalid args. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, bridge_internal_compute_nvs_key(NULL, key1, sizeof(key1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, bridge_internal_compute_nvs_key("", key1, sizeof(key1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, bridge_internal_compute_nvs_key("ok", NULL, sizeof(key1)));
    /* Buffer too small. */
    char small[8];
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, bridge_internal_compute_nvs_key("ok", small, sizeof(small)));
}

/* Invalid arguments **********************************************************/

void test_bridge_child_nvs_invalid_arg(void)
{
    __setup();
    struct esp_rmaker_bridge_child empty = __mk_child("");
    struct esp_rmaker_bridge_child ok = __mk_child("ok");
    bridge_child_nvs_record_t r;
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, bridge_child_nvs_load(NULL, &r));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, bridge_child_nvs_load(&empty, &r));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, bridge_child_nvs_load(&ok, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, bridge_child_nvs_store(&ok, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, bridge_child_nvs_set_node_config(&ok, NULL));
    __teardown();
}

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
