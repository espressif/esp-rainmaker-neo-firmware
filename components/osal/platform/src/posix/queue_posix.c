/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* POSIX-based queue implementation with blocking send/receive and timeouts */

#include "osal_queue.h"
#include "osal_ticks.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

typedef struct platform_posix_queue {
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
    uint8_t *buffer;           /* ring buffer bytes */
    uint32_t capacity;         /* number of items */
    uint32_t item_size;        /* bytes per item */
    uint32_t count;            /* items currently stored */
    uint32_t head;             /* read index [0, capacity) */
    uint32_t tail;             /* write index [0, capacity) */
} platform_posix_queue_t;

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

osal_queue_handle_t osal_queue_create(uint32_t queue_length, uint32_t item_size)
{
    if (queue_length == 0 || item_size == 0) {
        return NULL;
    }
    platform_posix_queue_t *q = (platform_posix_queue_t *)calloc(1, sizeof(platform_posix_queue_t));
    if (q == NULL) {
        return NULL;
    }
    q->buffer = (uint8_t *)malloc((size_t)queue_length * (size_t)item_size);
    if (q->buffer == NULL) {
        free(q);
        return NULL;
    }
    q->capacity = queue_length;
    q->item_size = item_size;
    if (pthread_mutex_init(&q->mutex, NULL) != 0) {
        free(q->buffer);
        free(q);
        return NULL;
    }
    if (pthread_cond_init(&q->cond_not_empty, NULL) != 0) {
        pthread_mutex_destroy(&q->mutex);
        free(q->buffer);
        free(q);
        return NULL;
    }
    if (pthread_cond_init(&q->cond_not_full, NULL) != 0) {
        pthread_cond_destroy(&q->cond_not_empty);
        pthread_mutex_destroy(&q->mutex);
        free(q->buffer);
        free(q);
        return NULL;
    }
    return (osal_queue_handle_t)q;
}

osal_queue_handle_t osal_queue_create_ext(uint32_t queue_length, uint32_t item_size)
{
    /* POSIX has no external RAM distinction; behaves identically to osal_queue_create(). */
    return osal_queue_create(queue_length, item_size);
}

void osal_queue_delete(osal_queue_handle_t queue_handle)
{
    if (queue_handle == NULL) {
        return;
    }
    platform_posix_queue_t *q = (platform_posix_queue_t *)queue_handle;
    pthread_cond_destroy(&q->cond_not_full);
    pthread_cond_destroy(&q->cond_not_empty);
    pthread_mutex_destroy(&q->mutex);
    free(q->buffer);
    free(q);
}

osal_err_t osal_queue_send(
    osal_queue_handle_t queue_handle,
    const void *item_to_queue,
    osal_tick_type_t ticks_to_wait
)
{
    if (queue_handle == NULL || item_to_queue == NULL) {
        return OSAL_ERR_FAIL;
    }
    platform_posix_queue_t *q = (platform_posix_queue_t *)queue_handle;
    int rc = pthread_mutex_lock(&q->mutex);
    if (rc != 0) {
        return OSAL_ERR_FAIL;
    }
    if (ticks_to_wait == 0) {
        if (q->count == q->capacity) {
            pthread_mutex_unlock(&q->mutex);
            return OSAL_ERR_TIMEOUT;
        }
    } else if (ticks_to_wait != OSAL_MAX_DELAY) {
        struct timespec abstime;
        uint32_t wait_ms = osal_ms_from_ticks(ticks_to_wait);
        rc = platform_posix_timespec_from_now_ms(&abstime, wait_ms);
        if (rc != 0) {
            pthread_mutex_unlock(&q->mutex);
            return OSAL_ERR_FAIL;
        }
        while (q->count == q->capacity) {
            rc = pthread_cond_timedwait(&q->cond_not_full, &q->mutex, &abstime);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return OSAL_ERR_TIMEOUT;
            }
            if (rc != 0) {
                pthread_mutex_unlock(&q->mutex);
                return OSAL_ERR_FAIL;
            }
        }
    } else {
        while (q->count == q->capacity) {
            pthread_cond_wait(&q->cond_not_full, &q->mutex);
        }
    }

    /* Write item at tail */
    uint8_t *dst = q->buffer + ((size_t)q->tail * (size_t)q->item_size);
    memcpy(dst, item_to_queue, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->cond_not_empty);
    pthread_mutex_unlock(&q->mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_queue_receive(
    osal_queue_handle_t queue_handle,
    void *buffer,
    osal_tick_type_t ticks_to_wait
)
{
    if (queue_handle == NULL || buffer == NULL) {
        return OSAL_ERR_FAIL;
    }
    platform_posix_queue_t *q = (platform_posix_queue_t *)queue_handle;
    int rc = pthread_mutex_lock(&q->mutex);
    if (rc != 0) {
        return OSAL_ERR_FAIL;
    }
    if (ticks_to_wait == 0) {
        if (q->count == 0) {
            pthread_mutex_unlock(&q->mutex);
            return OSAL_ERR_TIMEOUT;
        }
    } else if (ticks_to_wait != OSAL_MAX_DELAY) {
        struct timespec abstime;
        uint32_t wait_ms = osal_ms_from_ticks(ticks_to_wait);
        rc = platform_posix_timespec_from_now_ms(&abstime, wait_ms);
        if (rc != 0) {
            pthread_mutex_unlock(&q->mutex);
            return OSAL_ERR_FAIL;
        }
        while (q->count == 0) {
            rc = pthread_cond_timedwait(&q->cond_not_empty, &q->mutex, &abstime);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return OSAL_ERR_TIMEOUT;
            }
            if (rc != 0) {
                pthread_mutex_unlock(&q->mutex);
                return OSAL_ERR_FAIL;
            }
        }
    } else {
        while (q->count == 0) {
            pthread_cond_wait(&q->cond_not_empty, &q->mutex);
        }
    }

    /* Read item from head */
    uint8_t *src = q->buffer + ((size_t)q->head * (size_t)q->item_size);
    memcpy(buffer, src, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->cond_not_full);
    pthread_mutex_unlock(&q->mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_queue_peek(
    osal_queue_handle_t queue_handle,
    void *buffer,
    osal_tick_type_t ticks_to_wait
)
{
    if (queue_handle == NULL || buffer == NULL) {
        return OSAL_ERR_FAIL;
    }
    platform_posix_queue_t *q = (platform_posix_queue_t *)queue_handle;
    int rc = pthread_mutex_lock(&q->mutex);
    if (rc != 0) {
        return OSAL_ERR_FAIL;
    }
    if (ticks_to_wait == 0) {
        if (q->count == 0) {
            pthread_mutex_unlock(&q->mutex);
            return OSAL_ERR_TIMEOUT;
        }
    } else if (ticks_to_wait != OSAL_MAX_DELAY) {
        struct timespec abstime;
        uint32_t wait_ms = osal_ms_from_ticks(ticks_to_wait);
        rc = platform_posix_timespec_from_now_ms(&abstime, wait_ms);
        if (rc != 0) {
            pthread_mutex_unlock(&q->mutex);
            return OSAL_ERR_FAIL;
        }
        while (q->count == 0) {
            rc = pthread_cond_timedwait(&q->cond_not_empty, &q->mutex, &abstime);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return OSAL_ERR_TIMEOUT;
            }
            if (rc != 0) {
                pthread_mutex_unlock(&q->mutex);
                return OSAL_ERR_FAIL;
            }
        }
    } else {
        while (q->count == 0) {
            pthread_cond_wait(&q->cond_not_empty, &q->mutex);
        }
    }

    /* Read item from head without removing it */
    uint8_t *src = q->buffer + ((size_t)q->head * (size_t)q->item_size);
    memcpy(buffer, src, q->item_size);
    pthread_mutex_unlock(&q->mutex);
    return OSAL_ERR_OK;
}
