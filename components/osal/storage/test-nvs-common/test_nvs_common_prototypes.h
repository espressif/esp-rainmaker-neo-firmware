/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_nvs_common_prototypes.h
 * @brief Prototypes for the OSAL storage (NVS) test suite.
 */

#ifndef OSAL_STORAGE_TEST_PROTOTYPES_H
#define OSAL_STORAGE_TEST_PROTOTYPES_H

/* test_nvs_basic.c */
void test_nvs_basic_flow(void);
void test_nvs_iterator_basic_flow(void);
void test_nvs_error_paths(void);

/* tests_src/test_nvs_partition_isolation.c */
void test_nvs_partition_deinit_isolates_labels(void);
void test_nvs_partition_open_after_deinit_own_label_only(void);
void test_nvs_partition_reset_isolates_labels(void);
void test_nvs_partition_entry_find_after_peer_deinit(void);

/* test_all.c */
int test_nvs_common_all_tests_unity(void);

#endif /* OSAL_STORAGE_TEST_PROTOTYPES_H */
