/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file random_posix.c
 * @brief POSIX RNG implementation.
 *
 * Uses OS-backed entropy rather than rand(3): LCG outputs have poor low-bit
 * statistics and are unsuitable for uniform byte/word generation.
 */

/* Includes *************************************************************/

/* Declarations includes. */
#include "osal_random.h"

/* Standard includes. */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

/* Private function definitions *************************************************************/

#if !(defined(__APPLE__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__))

/**
 * @brief Read `len` bytes from /dev/urandom (Linux fallback / non-BSD Unix without arc4random).
 */
static int urandom_read(void *buffer, size_t len)
{
    unsigned char *p = (unsigned char *)buffer;
    size_t remaining = len;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        return -1;
    }
    while (remaining > 0) {
        ssize_t n = read(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -1;
        }
        if (n == 0) {
            close(fd);
            return -1;
        }
        p += (size_t)n;
        remaining -= (size_t)n;
    }
    close(fd);
    return 0;
}

#endif /* !(APPLE || BSD with arc4random_buf) */

/* Public function definitions *************************************************************/

uint32_t osal_random_generate(void)
{
    uint32_t v;
    osal_random_fill(&v, sizeof(v));
    return v;
}

void osal_random_fill(void *buffer, size_t size)
{
#if defined(__APPLE__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    arc4random_buf(buffer, size);
#elif defined(__linux__)
    unsigned char *p = (unsigned char *)buffer;
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t n = getrandom(p, remaining, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            /* Older kernel, seccomp, etc. */
            if (urandom_read(p, remaining) != 0) {
                abort();
            }
            return;
        }
        p += (size_t)n;
        remaining -= (size_t)n;
    }
#else
    if (urandom_read(buffer, size) != 0) {
        abort();
    }
#endif
}
