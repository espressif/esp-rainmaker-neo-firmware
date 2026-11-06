/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ota_stub.c
 * @brief Stub OTA implementation for platforms with no app partitions (the ESP-IDF linux target).
 *
 * Every call fails with OSAL_ERR_NOT_SUPPORTED, and every partition query returns NULL, so a
 * caller sees an image with no OTA state that cannot roll back.
 */

#include "osal_ota.h"

osal_err_t osal_ota_begin(const osal_ota_partition_t *partition, size_t image_size, osal_ota_handle_t *out_handle)
{
    (void)partition;
    (void)image_size;
    (void)out_handle;
    return OSAL_ERR_NOT_SUPPORTED;
}

osal_err_t osal_ota_resume(const osal_ota_partition_t *partition, const size_t erase_size, const size_t image_offset,
                           osal_ota_handle_t *out_handle)
{
    (void)partition;
    (void)erase_size;
    (void)image_offset;
    (void)out_handle;
    return OSAL_ERR_NOT_SUPPORTED;
}

osal_err_t osal_ota_end(osal_ota_handle_t handle)
{
    (void)handle;
    return OSAL_ERR_NOT_SUPPORTED;
}

osal_err_t osal_ota_abort(osal_ota_handle_t handle)
{
    (void)handle;
    return OSAL_ERR_NOT_SUPPORTED;
}

osal_err_t osal_ota_write_with_offset(osal_ota_handle_t handle, const void *data, size_t size, uint32_t offset)
{
    (void)handle;
    (void)data;
    (void)size;
    (void)offset;
    return OSAL_ERR_NOT_SUPPORTED;
}

osal_err_t osal_ota_set_boot_partition(const osal_ota_partition_t *partition)
{
    (void)partition;
    return OSAL_ERR_NOT_SUPPORTED;
}

const osal_ota_partition_t *osal_ota_get_boot_partition(void)
{
    return NULL;
}

const osal_ota_partition_t *osal_ota_get_running_partition(void)
{
    return NULL;
}

const osal_ota_partition_t *osal_ota_get_next_update_partition(const osal_ota_partition_t *start_from)
{
    (void)start_from;
    return NULL;
}

osal_err_t osal_ota_get_partition_description(const osal_ota_partition_t *partition, osal_ota_app_desc_t *app_desc)
{
    (void)partition;
    (void)app_desc;
    return OSAL_ERR_NOT_SUPPORTED;
}

osal_err_t osal_ota_get_partition_hash(const osal_ota_partition_t *partition, size_t size_to_hash,
                                       osal_ota_hash_type_t hash_type, uint8_t *hash, size_t hash_len)
{
    (void)partition;
    (void)size_to_hash;
    (void)hash_type;
    (void)hash;
    (void)hash_len;
    return OSAL_ERR_NOT_SUPPORTED;
}

osal_err_t osal_ota_mark_app_valid_cancel_rollback(void)
{
    return OSAL_ERR_NOT_SUPPORTED;
}

osal_err_t osal_ota_mark_app_invalid_rollback_and_reboot(void)
{
    return OSAL_ERR_NOT_SUPPORTED;
}

osal_err_t osal_ota_get_state_partition(const osal_ota_partition_t *partition, osal_ota_img_states_t *ota_state)
{
    (void)partition;
    (void)ota_state;
    return OSAL_ERR_NOT_SUPPORTED;
}

const osal_ota_partition_t *osal_ota_get_last_invalid_partition(void)
{
    return NULL;
}

uint8_t osal_ota_get_app_partition_count(void)
{
    return 0;
}

osal_err_t osal_ota_erase_last_boot_app_partition(void)
{
    return OSAL_ERR_NOT_SUPPORTED;
}

bool osal_ota_check_rollback_is_possible(void)
{
    return false;
}
