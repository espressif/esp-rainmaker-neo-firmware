/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_semaphore.h"

/* Standard includes */
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    OSAL_POSIX_SEM_KIND_MUTEX = 0,
    OSAL_POSIX_SEM_KIND_BINARY = 1,
    OSAL_POSIX_SEM_KIND_COUNTING = 2,
} platform_posix_semaphore_kind_t;

typedef struct platform_posix_semaphore {
    platform_posix_semaphore_kind_t kind;
    pthread_mutex_t state_mutex;
    pthread_cond_t cond;
    /* For counting and binary semaphores */
    unsigned int count;
    unsigned int max_count;
    /* For mutex semantics */
    bool mutex_locked;
    pthread_t owner;
    /* For recursive mutexes: nested take count held by ``owner``. */
    unsigned int recursion_depth;
} platform_posix_semaphore_t;

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

static platform_posix_semaphore_t *platform_posix_semaphore_create_common(platform_posix_semaphore_kind_t kind, uint32_t max_count, uint32_t initial_count)
{
    platform_posix_semaphore_t *sem = (platform_posix_semaphore_t *)malloc(sizeof(platform_posix_semaphore_t));
    if (sem == NULL) {
        return NULL;
    }
    sem->kind = kind;
    sem->recursion_depth = 0;
    sem->count = initial_count;
    sem->max_count = max_count;

    if (pthread_mutex_init(&sem->state_mutex, NULL) != 0) {
        free(sem);
        return NULL;
    }
    if (pthread_cond_init(&sem->cond, NULL) != 0) {
        pthread_mutex_destroy(&sem->state_mutex);
        free(sem);
        return NULL;
    }

    return sem;
}

osal_semaphore_handle_t osal_semaphore_create_mutex(void)
{
    platform_posix_semaphore_t *sem = platform_posix_semaphore_create_common(OSAL_POSIX_SEM_KIND_MUTEX, 1, 1);
    if (sem == NULL) {
        return NULL;
    }
    sem->mutex_locked = false;
    sem->owner = (pthread_t)0;
    return (osal_semaphore_handle_t)sem;
}

osal_semaphore_handle_t osal_semaphore_create_recursive_mutex(void)
{
    /* Same state as a plain mutex; recursion is tracked by owner + depth in
     * osal_semaphore_take_recursive/give_recursive. */
    platform_posix_semaphore_t *sem = platform_posix_semaphore_create_common(OSAL_POSIX_SEM_KIND_MUTEX, 1, 1);
    if (sem == NULL) {
        return NULL;
    }
    sem->mutex_locked = false;
    sem->owner = (pthread_t)0;
    return (osal_semaphore_handle_t)sem;
}

osal_semaphore_handle_t osal_semaphore_create_binary(void)
{
    /* FreeRTOS binary semaphores start in the 'empty' state. */
    return (osal_semaphore_handle_t)platform_posix_semaphore_create_common(OSAL_POSIX_SEM_KIND_BINARY, 1, 0);
}

osal_semaphore_handle_t osal_semaphore_create_counting(uint32_t max_count, uint32_t initial_count)
{
    if (max_count == 0 || initial_count > max_count) {
        return NULL;
    }
    return (osal_semaphore_handle_t)platform_posix_semaphore_create_common(OSAL_POSIX_SEM_KIND_COUNTING, max_count, initial_count);
}

void osal_semaphore_delete(osal_semaphore_handle_t semaphore_handle)
{
    if (semaphore_handle == NULL) {
        return;
    }
    platform_posix_semaphore_t *sem = (platform_posix_semaphore_t *)semaphore_handle;
    pthread_cond_destroy(&sem->cond);
    pthread_mutex_destroy(&sem->state_mutex);
    free(sem);
}

osal_err_t osal_semaphore_take(osal_semaphore_handle_t semaphore_handle, osal_tick_type_t ticks_to_wait)
{
    if (semaphore_handle == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    platform_posix_semaphore_t *sem = (platform_posix_semaphore_t *)semaphore_handle;

    if (sem->kind == OSAL_POSIX_SEM_KIND_MUTEX) {
        int rc = pthread_mutex_lock(&sem->state_mutex);
        if (rc != 0) {
            return OSAL_ERR_FAIL;
        }
        if (ticks_to_wait == 0) {
            if (!sem->mutex_locked) {
                sem->mutex_locked = true;
                sem->owner = pthread_self();
                pthread_mutex_unlock(&sem->state_mutex);
                return OSAL_ERR_OK;
            } else {
                pthread_mutex_unlock(&sem->state_mutex);
                return OSAL_ERR_TIMEOUT;
            }
        }
        if (ticks_to_wait == OSAL_MAX_DELAY) {
            while (sem->mutex_locked) {
                rc = pthread_cond_wait(&sem->cond, &sem->state_mutex);
                if (rc != 0) {
                    pthread_mutex_unlock(&sem->state_mutex);
                    return OSAL_ERR_FAIL;
                }
            }
            sem->mutex_locked = true;
            sem->owner = pthread_self();
            pthread_mutex_unlock(&sem->state_mutex);
            return OSAL_ERR_OK;
        }
        struct timespec abstime;
        uint32_t wait_ms = osal_ms_from_ticks(ticks_to_wait);
        rc = platform_posix_timespec_from_now_ms(&abstime, wait_ms);
        if (rc != 0) {
            pthread_mutex_unlock(&sem->state_mutex);
            return OSAL_ERR_FAIL;
        }
        while (sem->mutex_locked) {
            rc = pthread_cond_timedwait(&sem->cond, &sem->state_mutex, &abstime);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&sem->state_mutex);
                return OSAL_ERR_TIMEOUT;
            }
            if (rc != 0) {
                pthread_mutex_unlock(&sem->state_mutex);
                return OSAL_ERR_FAIL;
            }
        }
        sem->mutex_locked = true;
        sem->owner = pthread_self();
        pthread_mutex_unlock(&sem->state_mutex);
        return OSAL_ERR_OK;
    }

    /* Binary / counting semaphore path */
    int rc = pthread_mutex_lock(&sem->state_mutex);
    if (rc != 0) {
        return OSAL_ERR_FAIL;
    }

    if (ticks_to_wait == 0) {
        if (sem->count > 0) {
            sem->count--;
            pthread_mutex_unlock(&sem->state_mutex);
            return OSAL_ERR_OK;
        } else {
            pthread_mutex_unlock(&sem->state_mutex);
            return OSAL_ERR_TIMEOUT;
        }
    }

    if (ticks_to_wait == OSAL_MAX_DELAY) {
        while (sem->count == 0) {
            rc = pthread_cond_wait(&sem->cond, &sem->state_mutex);
            if (rc != 0) {
                pthread_mutex_unlock(&sem->state_mutex);
                return OSAL_ERR_FAIL;
            }
        }
        sem->count--;
        pthread_mutex_unlock(&sem->state_mutex);
        return OSAL_ERR_OK;
    }

    struct timespec abstime;
    uint32_t wait_ms = osal_ms_from_ticks(ticks_to_wait);
    rc = platform_posix_timespec_from_now_ms(&abstime, wait_ms);
    if (rc != 0) {
        pthread_mutex_unlock(&sem->state_mutex);
        return OSAL_ERR_FAIL;
    }
    while (sem->count == 0) {
        rc = pthread_cond_timedwait(&sem->cond, &sem->state_mutex, &abstime);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&sem->state_mutex);
            return OSAL_ERR_TIMEOUT;
        }
        if (rc != 0) {
            pthread_mutex_unlock(&sem->state_mutex);
            return OSAL_ERR_FAIL;
        }
    }
    sem->count--;
    pthread_mutex_unlock(&sem->state_mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_semaphore_give(osal_semaphore_handle_t semaphore_handle)
{
    if (semaphore_handle == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    platform_posix_semaphore_t *sem = (platform_posix_semaphore_t *)semaphore_handle;

    if (sem->kind == OSAL_POSIX_SEM_KIND_MUTEX) {
        int rc = pthread_mutex_lock(&sem->state_mutex);
        if (rc != 0) {
            return OSAL_ERR_FAIL;
        }
        /* Optionally enforce ownership: only owner can unlock. */
        if (!sem->mutex_locked) {
            pthread_mutex_unlock(&sem->state_mutex);
            return OSAL_ERR_FAIL;
        }
        if (!pthread_equal(sem->owner, pthread_self())) {
            pthread_mutex_unlock(&sem->state_mutex);
            return OSAL_ERR_FAIL;
        }
        sem->mutex_locked = false;
        sem->owner = (pthread_t)0;
        pthread_cond_signal(&sem->cond);
        pthread_mutex_unlock(&sem->state_mutex);
        return OSAL_ERR_OK;
    }

    int rc = pthread_mutex_lock(&sem->state_mutex);
    if (rc != 0) {
        return OSAL_ERR_FAIL;
    }
    if (sem->count < sem->max_count) {
        sem->count++;
        pthread_cond_signal(&sem->cond);
        pthread_mutex_unlock(&sem->state_mutex);
        return OSAL_ERR_OK;
    } else {
        /* Overflow would occur */
        pthread_mutex_unlock(&sem->state_mutex);
        return OSAL_ERR_FAIL;
    }
}

osal_err_t osal_semaphore_take_recursive(osal_semaphore_handle_t semaphore_handle, osal_tick_type_t ticks_to_wait)
{
    if (semaphore_handle == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    platform_posix_semaphore_t *sem = (platform_posix_semaphore_t *)semaphore_handle;
    if (sem->kind != OSAL_POSIX_SEM_KIND_MUTEX) {
        return OSAL_ERR_FAIL;
    }

    if (pthread_mutex_lock(&sem->state_mutex) != 0) {
        return OSAL_ERR_FAIL;
    }
    /* Already ours: just deepen, never block on ourselves. */
    if (sem->mutex_locked && pthread_equal(sem->owner, pthread_self())) {
        sem->recursion_depth++;
        pthread_mutex_unlock(&sem->state_mutex);
        return OSAL_ERR_OK;
    }

    if (ticks_to_wait == 0) {
        if (sem->mutex_locked) {
            pthread_mutex_unlock(&sem->state_mutex);
            return OSAL_ERR_TIMEOUT;
        }
    } else if (ticks_to_wait == OSAL_MAX_DELAY) {
        while (sem->mutex_locked) {
            if (pthread_cond_wait(&sem->cond, &sem->state_mutex) != 0) {
                pthread_mutex_unlock(&sem->state_mutex);
                return OSAL_ERR_FAIL;
            }
        }
    } else {
        struct timespec abstime;
        if (platform_posix_timespec_from_now_ms(&abstime, osal_ms_from_ticks(ticks_to_wait)) != 0) {
            pthread_mutex_unlock(&sem->state_mutex);
            return OSAL_ERR_FAIL;
        }
        while (sem->mutex_locked) {
            int rc = pthread_cond_timedwait(&sem->cond, &sem->state_mutex, &abstime);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&sem->state_mutex);
                return OSAL_ERR_TIMEOUT;
            }
            if (rc != 0) {
                pthread_mutex_unlock(&sem->state_mutex);
                return OSAL_ERR_FAIL;
            }
        }
    }

    sem->mutex_locked = true;
    sem->owner = pthread_self();
    sem->recursion_depth = 1;
    pthread_mutex_unlock(&sem->state_mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_semaphore_give_recursive(osal_semaphore_handle_t semaphore_handle)
{
    if (semaphore_handle == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    platform_posix_semaphore_t *sem = (platform_posix_semaphore_t *)semaphore_handle;
    if (sem->kind != OSAL_POSIX_SEM_KIND_MUTEX) {
        return OSAL_ERR_FAIL;
    }

    if (pthread_mutex_lock(&sem->state_mutex) != 0) {
        return OSAL_ERR_FAIL;
    }
    /* Only the owner may give, and only a held mutex can be given. */
    if (!sem->mutex_locked || !pthread_equal(sem->owner, pthread_self())) {
        pthread_mutex_unlock(&sem->state_mutex);
        return OSAL_ERR_FAIL;
    }
    if (sem->recursion_depth > 1) {
        sem->recursion_depth--;
        pthread_mutex_unlock(&sem->state_mutex);
        return OSAL_ERR_OK;
    }
    sem->recursion_depth = 0;
    sem->mutex_locked = false;
    sem->owner = (pthread_t)0;
    pthread_cond_signal(&sem->cond);
    pthread_mutex_unlock(&sem->state_mutex);
    return OSAL_ERR_OK;
}
