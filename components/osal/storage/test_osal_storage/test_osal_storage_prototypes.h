/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_osal_storage_prototypes.h
 * @brief Prototypes for the OSAL storage (NVS) test suite.
 */

#ifndef __TEST_OSAL_STORAGE_PROTOTYPES_H__
#define __TEST_OSAL_STORAGE_PROTOTYPES_H__

/* Basic paths. */
void test_storage_basic_flow(void);
void test_storage_iterator_basic_flow(void);
void test_storage_error_paths(void);

/* Partition isolation. */
void test_storage_partition_deinit_isolates_labels(void);
void test_storage_partition_open_after_deinit_own_label_only(void);
void test_storage_partition_reset_isolates_labels(void);
void test_storage_partition_entry_find_after_peer_deinit(void);

/* All tests. */
int test_osal_storage_all_tests_unity(void);

#endif /* __TEST_OSAL_STORAGE_PROTOTYPES_H__ */
