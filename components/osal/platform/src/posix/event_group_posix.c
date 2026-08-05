/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* OS-agnostic POSIX implementation of FreeRTOS-like event groups */

#include "osal_event_group.h"
#include "osal_ticks.h"

#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>

typedef struct platform_posix_event_group {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    osal_event_group_bits_t bits;
} platform_posix_event_group_t;

static int platform_posix_timespec_from_now_ms(struct timespec *abstime, uint32_t wait_ms)
{
    if (abstime == NULL) {
        return EINVAL;
    }
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        return errno;
    }
    uint64_t nsec_total = (uint64_t)now.tv_nsec + ((uint64_t)(wait_ms % 1000) * 1000000ULL);
    abstime->tv_sec = now.tv_sec + (time_t)(wait_ms / 1000) + (time_t)(nsec_total / 1000000000ULL);
    abstime->tv_nsec = (long)(nsec_total % 1000000000ULL);
    return 0;
}

osal_event_group_handle_t osal_event_group_create(void)
{
    platform_posix_event_group_t *eg = (platform_posix_event_group_t *)malloc(sizeof(platform_posix_event_group_t));
    if (eg == NULL) {
        return NULL;
    }
    eg->bits = 0;
    if (pthread_mutex_init(&eg->mutex, NULL) != 0) {
        free(eg);
        return NULL;
    }
    if (pthread_cond_init(&eg->cond, NULL) != 0) {
        pthread_mutex_destroy(&eg->mutex);
        free(eg);
        return NULL;
    }
    return (osal_event_group_handle_t)eg;
}

void osal_event_group_delete(osal_event_group_handle_t event_group)
{
    if (event_group == NULL) {
        return;
    }
    platform_posix_event_group_t *eg = (platform_posix_event_group_t *)event_group;
    pthread_cond_destroy(&eg->cond);
    pthread_mutex_destroy(&eg->mutex);
    free(eg);
}

osal_event_group_bits_t osal_event_group_wait_bits(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_wait_for,
    const bool clear_on_exit,
    const bool wait_for_all_bits,
    osal_tick_type_t ticks_to_wait
)
{
    if (event_group == NULL) {
        return 0;
    }
    platform_posix_event_group_t *eg = (platform_posix_event_group_t *)event_group;
    int rc = pthread_mutex_lock(&eg->mutex);
    if (rc != 0) {
        return 0;
    }

    osal_event_group_bits_t before_bits = eg->bits;
    const osal_event_group_bits_t mask = bits_to_wait_for;

    if (ticks_to_wait == 0) {
        /* Non-blocking check */
        bool cond_met = wait_for_all_bits ? ((eg->bits & mask) == mask) : ((eg->bits & mask) != 0);
        before_bits = eg->bits;
        if (cond_met && clear_on_exit) {
            eg->bits &= ~mask;
        }
        pthread_mutex_unlock(&eg->mutex);
        return before_bits;
    }

    if (ticks_to_wait == OSAL_MAX_DELAY) {
        while (!(wait_for_all_bits ? ((eg->bits & mask) == mask) : ((eg->bits & mask) != 0))) {
            rc = pthread_cond_wait(&eg->cond, &eg->mutex);
            if (rc != 0) {
                pthread_mutex_unlock(&eg->mutex);
                return before_bits;
            }
        }
        before_bits = eg->bits;
        if (clear_on_exit) {
            eg->bits &= ~mask;
        }
        pthread_mutex_unlock(&eg->mutex);
        return before_bits;
    }

    struct timespec abstime;
    uint32_t wait_ms = osal_ms_from_ticks(ticks_to_wait);
    rc = platform_posix_timespec_from_now_ms(&abstime, wait_ms);
    if (rc != 0) {
        pthread_mutex_unlock(&eg->mutex);
        return before_bits;
    }
    while (!(wait_for_all_bits ? ((eg->bits & mask) == mask) : ((eg->bits & mask) != 0))) {
        rc = pthread_cond_timedwait(&eg->cond, &eg->mutex, &abstime);
        if (rc == ETIMEDOUT) {
            /* Timeout: return current bits without clearing */
            before_bits = eg->bits;
            pthread_mutex_unlock(&eg->mutex);
            return before_bits;
        }
        if (rc != 0) {
            pthread_mutex_unlock(&eg->mutex);
            return before_bits;
        }
    }
    before_bits = eg->bits;
    if (clear_on_exit) {
        eg->bits &= ~mask;
    }
    pthread_mutex_unlock(&eg->mutex);
    return before_bits;
}

osal_event_group_bits_t osal_event_group_set_bits(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_set
)
{
    if (event_group == NULL) {
        return 0;
    }
    platform_posix_event_group_t *eg = (platform_posix_event_group_t *)event_group;
    int rc = pthread_mutex_lock(&eg->mutex);
    if (rc != 0) {
        return 0;
    }
    eg->bits |= bits_to_set;
    osal_event_group_bits_t ret_bits = eg->bits;
    pthread_cond_broadcast(&eg->cond);
    pthread_mutex_unlock(&eg->mutex);
    return ret_bits;
}

osal_event_group_bits_t osal_event_group_clear_bits(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_clear
)
{
    if (event_group == NULL) {
        return 0;
    }
    platform_posix_event_group_t *eg = (platform_posix_event_group_t *)event_group;
    int rc = pthread_mutex_lock(&eg->mutex);
    if (rc != 0) {
        return 0;
    }
    osal_event_group_bits_t ret_bits = eg->bits;
    eg->bits &= ~bits_to_clear;
    pthread_mutex_unlock(&eg->mutex);
    return ret_bits;
}

osal_event_group_bits_t osal_event_group_get_bits(
    osal_event_group_handle_t event_group
)
{
    if (event_group == NULL) {
        return 0;
    }
    platform_posix_event_group_t *eg = (platform_posix_event_group_t *)event_group;
    int rc = pthread_mutex_lock(&eg->mutex);
    if (rc != 0) {
        return 0;
    }
    osal_event_group_bits_t bits = eg->bits;
    pthread_mutex_unlock(&eg->mutex);
    return bits;
}

osal_event_group_bits_t osal_event_group_sync(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_set,
    const osal_event_group_bits_t bits_to_wait_for,
    osal_tick_type_t ticks_to_wait
)
{
    if (event_group == NULL) {
        return 0;
    }
    platform_posix_event_group_t *eg = (platform_posix_event_group_t *)event_group;
    int rc = pthread_mutex_lock(&eg->mutex);
    if (rc != 0) {
        return 0;
    }
    /* Set bits first (atomic with wait) */
    eg->bits |= bits_to_set;
    pthread_cond_broadcast(&eg->cond);

    osal_event_group_bits_t before_bits = eg->bits;
    const osal_event_group_bits_t mask = bits_to_wait_for;

    if (ticks_to_wait == 0) {
        if ((eg->bits & mask) == mask) {
            before_bits = eg->bits;
            eg->bits &= ~mask; /* clear on exit */
        } else {
            before_bits = eg->bits; /* not clearing on timeout */
        }
        pthread_mutex_unlock(&eg->mutex);
        return before_bits;
    }

    if (ticks_to_wait == OSAL_MAX_DELAY) {
        while (((eg->bits & mask) != mask)) {
            rc = pthread_cond_wait(&eg->cond, &eg->mutex);
            if (rc != 0) {
                pthread_mutex_unlock(&eg->mutex);
                return before_bits;
            }
        }
        before_bits = eg->bits;
        eg->bits &= ~mask; /* clear on exit */
        pthread_mutex_unlock(&eg->mutex);
        return before_bits;
    }

    struct timespec abstime;
    uint32_t wait_ms = osal_ms_from_ticks(ticks_to_wait);
    rc = platform_posix_timespec_from_now_ms(&abstime, wait_ms);
    if (rc != 0) {
        pthread_mutex_unlock(&eg->mutex);
        return before_bits;
    }
    while (((eg->bits & mask) != mask)) {
        rc = pthread_cond_timedwait(&eg->cond, &eg->mutex, &abstime);
        if (rc == ETIMEDOUT) {
            before_bits = eg->bits; /* timeout: no clear */
            pthread_mutex_unlock(&eg->mutex);
            return before_bits;
        }
        if (rc != 0) {
            pthread_mutex_unlock(&eg->mutex);
            return before_bits;
        }
    }
    before_bits = eg->bits;
    eg->bits &= ~mask; /* clear on exit */
    pthread_mutex_unlock(&eg->mutex);
    return before_bits;
}
