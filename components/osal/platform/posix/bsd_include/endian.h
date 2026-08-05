/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file endian.h
 * @brief POSIX endianness header file.
 *
 * Linux software expects the Linux-style <endian.h>. macOS has no such header,
 * so this shim (added to the include path only on Apple) provides it.
 *
 * Recent macOS SDKs (added 2024) ship <sys/endian.h> with the BSD byte-order
 * helpers; forward to it when present. Older SDKs lack it, so fall back to
 * <libkern/OSByteOrder.h> (always available on Darwin) and define the BSD
 * byte-order helper families (htole/letoh/htobe/betoh) ourselves.
 */

#ifndef __BSD_ENDIAN_H__
#define __BSD_ENDIAN_H__

#if defined(__has_include)
#if __has_include(<sys/endian.h>)
#define __BSD_ENDIAN_HAVE_SYS_ENDIAN_H 1
#endif
#endif

#ifdef __BSD_ENDIAN_HAVE_SYS_ENDIAN_H

#include <sys/endian.h>

#else /* fall back for macOS SDKs without <sys/endian.h> */

#include <libkern/OSByteOrder.h>

#define htole16(x) OSSwapHostToLittleInt16(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define be64toh(x) OSSwapBigToHostInt64(x)

#endif /* __BSD_ENDIAN_HAVE_SYS_ENDIAN_H */

#endif /* __BSD_ENDIAN_H__ */
