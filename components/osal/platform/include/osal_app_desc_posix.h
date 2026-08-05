/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_app_desc_posix.h
 * @brief Embedded application descriptor for POSIX-built firmware binaries.
 *
 * On ESP-IDF targets the bootloader places `esp_app_desc_t` at a fixed offset in
 * the app partition. POSIX has no equivalent, so this header defines a self-
 * describing descriptor embedded into the binary via a custom section + magic.
 *
 * Readers (e.g. osal_ota_posix.c) locate the descriptor by scanning the
 * entire partition file for the 8-byte magic, then validating CRC32 over
 * the preceding payload bytes.
 */

#ifndef __OSAL_APP_DESC_POSIX_H__
#define __OSAL_APP_DESC_POSIX_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 8-byte high-entropy magic word identifying the embedded descriptor.
 *
 * Chosen to be unlikely to appear elsewhere in a typical ELF/Mach-O binary.
 */
#define OSAL_APP_DESC_MAGIC 0xE9F53284C1A76B5EULL

/**
 * @brief Section attribute spelling for the descriptor.
 *
 * ELF takes a single section name; Mach-O requires "segment,section" with the
 * legacy 16-byte-cap convention (segment names UPPERCASE, section names lowercase
 * with leading underscores).
 */
#if defined(__APPLE__)
#define OSAL_APP_DESC_SECTION "__DATA,__app_desc"
#else
#define OSAL_APP_DESC_SECTION ".app_desc"
#endif

/**
 * @brief Length of project name / version strings (matches osal_ota_app_desc_t).
 */
#define OSAL_APP_DESC_STR_LEN 32

/**
 * @brief Length of app ELF SHA256 (matches osal_ota_app_desc_t).
 */
#define OSAL_APP_DESC_SHA256_LEN 32

/**
 * @brief Embedded application descriptor layout.
 *
 * CRC32 covers all bytes from `magic` through `app_elf_sha256` (i.e. every
 * field except `crc32` itself). All multi-byte integers are little-endian.
 */
typedef struct {
    uint64_t magic;                                          /**< Must equal OSAL_APP_DESC_MAGIC */
    uint32_t secure_version;                                 /**< Secure version (anti-rollback counter) */
    char     version[OSAL_APP_DESC_STR_LEN];      /**< Null-padded firmware version string */
    char     project_name[OSAL_APP_DESC_STR_LEN]; /**< Null-padded project name */
    uint8_t  app_elf_sha256[OSAL_APP_DESC_SHA256_LEN]; /**< ELF SHA256 (zero on POSIX) */
    uint32_t crc32;                                          /**< CRC32 over preceding bytes */
} __attribute__((packed)) osal_app_desc_embed_t;

/**
 * @brief Number of bytes covered by the CRC32 (i.e. the struct minus the trailing crc32 field).
 */
#define OSAL_APP_DESC_CRC_PAYLOAD_LEN \
    (sizeof(osal_app_desc_embed_t) - sizeof(uint32_t))

/**
 * @brief The embedded descriptor instance in this binary.
 */
extern const osal_app_desc_embed_t g_platform_common_app_desc;

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_APP_DESC_POSIX_H__ */
