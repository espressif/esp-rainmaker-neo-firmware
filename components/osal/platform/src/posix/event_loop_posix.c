/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* POSIX-based default event loop implementation compatible with the public API */

#include "osal_event_loop.h"
#include "osal_ticks.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct platform_posix_event_item {
    osal_event_base_t event_base;
    int32_t event_id;
    void *event_data; /* heap copy */
    size_t event_data_size;
} platform_posix_event_item_t;

typedef struct platform_posix_event_handler_entry {
    osal_event_base_t event_base;
    int32_t event_id;
    osal_event_handler_t handler;
    void *handler_arg;
} platform_posix_event_handler_entry_t;

typedef struct platform_posix_event_loop {
    pthread_t worker;
    bool running;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;

    /* Queue */
    platform_posix_event_item_t *queue;
    size_t capacity;
    size_t size;
    size_t head;
    size_t tail;

    /* Handlers */
    platform_posix_event_handler_entry_t *handlers;
    size_t handlers_count;
    size_t handlers_capacity;
} platform_posix_event_loop_t;

static platform_posix_event_loop_t *g_default_loop = NULL;

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

static void platform_posix_event_free_item(platform_posix_event_item_t *item)
{
    if (item == NULL) {
        return;
    }
    if (item->event_data != NULL && item->event_data_size > 0) {
        free(item->event_data);
        item->event_data = NULL;
        item->event_data_size = 0;
    }
}

static void *platform_posix_event_loop_thread(void *arg)
{
    platform_posix_event_loop_t *loop = (platform_posix_event_loop_t *)arg;
    for (;;) {
        pthread_mutex_lock(&loop->mutex);
        while (loop->running && loop->size == 0) {
            pthread_cond_wait(&loop->cond_not_empty, &loop->mutex);
        }
        if (!loop->running && loop->size == 0) {
            pthread_mutex_unlock(&loop->mutex);
            break;
        }
        /* Dequeue one item */
        platform_posix_event_item_t item = loop->queue[loop->head];
        loop->head = (loop->head + 1) % loop->capacity;
        loop->size--;
        pthread_cond_signal(&loop->cond_not_full);

        /* Copy matching handlers to a local list to call outside the lock */
        size_t max_tmp = loop->handlers_count;
        platform_posix_event_handler_entry_t *tmp = NULL;
        if (max_tmp > 0) {
            tmp = (platform_posix_event_handler_entry_t *)malloc(max_tmp * sizeof(*tmp));
        }
        size_t tmp_count = 0;
        for (size_t i = 0; i < loop->handlers_count; i++) {
            platform_posix_event_handler_entry_t h = loop->handlers[i];
            if (h.event_base == item.event_base && (h.event_id == item.event_id || h.event_id == OSAL_EVENT_ID_ANY)) {
                if (tmp != NULL && tmp_count < max_tmp) {
                    tmp[tmp_count++] = h;
                }
            }
        }
        pthread_mutex_unlock(&loop->mutex);

        /* Dispatch without holding the lock */
        for (size_t i = 0; i < tmp_count; i++) {
            tmp[i].handler(tmp[i].handler_arg, item.event_base, item.event_id, item.event_data);
        }

        if (tmp != NULL) {
            free(tmp);
        }
        platform_posix_event_free_item(&item);
    }
    return NULL;
}

osal_err_t osal_event_loop_create_default(void)
{
    if (g_default_loop != NULL) {
        return OSAL_ERR_INVALID_STATE;
    }
    platform_posix_event_loop_t *loop = (platform_posix_event_loop_t *)calloc(1, sizeof(platform_posix_event_loop_t));
    if (loop == NULL) {
        return OSAL_ERR_NO_MEM;
    }
    loop->capacity = 32; /* default queue capacity */
    loop->queue = (platform_posix_event_item_t *)calloc(loop->capacity, sizeof(platform_posix_event_item_t));
    if (loop->queue == NULL) {
        free(loop);
        return OSAL_ERR_NO_MEM;
    }
    loop->handlers_capacity = 8;
    loop->handlers = (platform_posix_event_handler_entry_t *)calloc(loop->handlers_capacity, sizeof(platform_posix_event_handler_entry_t));
    if (loop->handlers == NULL) {
        free(loop->queue);
        free(loop);
        return OSAL_ERR_NO_MEM;
    }
    if (pthread_mutex_init(&loop->mutex, NULL) != 0) {
        free(loop->handlers);
        free(loop->queue);
        free(loop);
        return OSAL_ERR_FAIL;
    }
    if (pthread_cond_init(&loop->cond_not_empty, NULL) != 0) {
        pthread_mutex_destroy(&loop->mutex);
        free(loop->handlers);
        free(loop->queue);
        free(loop);
        return OSAL_ERR_FAIL;
    }
    if (pthread_cond_init(&loop->cond_not_full, NULL) != 0) {
        pthread_cond_destroy(&loop->cond_not_empty);
        pthread_mutex_destroy(&loop->mutex);
        free(loop->handlers);
        free(loop->queue);
        free(loop);
        return OSAL_ERR_FAIL;
    }
    loop->running = true;
    int rc = pthread_create(&loop->worker, NULL, platform_posix_event_loop_thread, loop);
    if (rc != 0) {
        pthread_cond_destroy(&loop->cond_not_full);
        pthread_cond_destroy(&loop->cond_not_empty);
        pthread_mutex_destroy(&loop->mutex);
        free(loop->handlers);
        free(loop->queue);
        free(loop);
        return OSAL_ERR_FAIL;
    }
    g_default_loop = loop;
    return OSAL_ERR_OK;
}

osal_err_t osal_event_loop_delete_default(void)
{
    platform_posix_event_loop_t *loop = g_default_loop;
    if (loop == NULL) {
        return OSAL_ERR_OK;
    }
    pthread_mutex_lock(&loop->mutex);
    loop->running = false;
    pthread_cond_broadcast(&loop->cond_not_empty);
    pthread_cond_broadcast(&loop->cond_not_full);
    pthread_mutex_unlock(&loop->mutex);

    pthread_join(loop->worker, NULL);

    /* Free remaining queued items */
    pthread_mutex_lock(&loop->mutex);
    while (loop->size > 0) {
        platform_posix_event_item_t *item = &loop->queue[loop->head];
        platform_posix_event_free_item(item);
        loop->head = (loop->head + 1) % loop->capacity;
        loop->size--;
    }
    pthread_mutex_unlock(&loop->mutex);

    pthread_cond_destroy(&loop->cond_not_full);
    pthread_cond_destroy(&loop->cond_not_empty);
    pthread_mutex_destroy(&loop->mutex);
    free(loop->handlers);
    free(loop->queue);
    free(loop);
    g_default_loop = NULL;
    return OSAL_ERR_OK;
}

osal_err_t osal_event_handler_register(osal_event_base_t event_base, int32_t event_id, osal_event_handler_t event_handler, void *event_handler_arg)
{
    if (g_default_loop == NULL) {
        return OSAL_ERR_INVALID_STATE;
    }
    if (event_base == NULL || event_handler == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    platform_posix_event_loop_t *loop = g_default_loop;
    pthread_mutex_lock(&loop->mutex);
    if (loop->handlers_count == loop->handlers_capacity) {
        size_t new_cap = loop->handlers_capacity * 2;
        platform_posix_event_handler_entry_t *new_arr = (platform_posix_event_handler_entry_t *)realloc(loop->handlers, new_cap * sizeof(*new_arr));
        if (new_arr == NULL) {
            pthread_mutex_unlock(&loop->mutex);
            return OSAL_ERR_NO_MEM;
        }
        loop->handlers = new_arr;
        loop->handlers_capacity = new_cap;
    }
    loop->handlers[loop->handlers_count++] = (platform_posix_event_handler_entry_t) {
        .event_base = event_base,
        .event_id = event_id,
        .handler = event_handler,
        .handler_arg = event_handler_arg,
    };
    pthread_mutex_unlock(&loop->mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_event_handler_unregister(osal_event_base_t event_base, int32_t event_id, osal_event_handler_t event_handler)
{
    if (g_default_loop == NULL) {
        return OSAL_ERR_INVALID_STATE;
    }
    if (event_base == NULL || event_handler == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    platform_posix_event_loop_t *loop = g_default_loop;
    pthread_mutex_lock(&loop->mutex);
    size_t write_idx = 0;
    bool removed = false;
    for (size_t i = 0; i < loop->handlers_count; i++) {
        platform_posix_event_handler_entry_t h = loop->handlers[i];
        if (!removed && h.event_base == event_base && h.event_id == event_id && h.handler == event_handler) {
            removed = true;
            continue;
        }
        if (write_idx != i) {
            loop->handlers[write_idx] = h;
        }
        write_idx++;
    }
    loop->handlers_count = write_idx;
    pthread_mutex_unlock(&loop->mutex);
    (void)removed;
    return OSAL_ERR_OK;
}

osal_err_t osal_event_post(osal_event_base_t event_base, int32_t event_id, void *event_data, size_t event_data_size, osal_tick_type_t ticks_to_wait)
{
    if (g_default_loop == NULL) {
        return OSAL_ERR_INVALID_STATE;
    }
    if (event_base == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    platform_posix_event_loop_t *loop = g_default_loop;

    /* Prepare item with copied data */
    platform_posix_event_item_t item;
    item.event_base = event_base;
    item.event_id = event_id;
    item.event_data_size = event_data_size;
    if (event_data != NULL && event_data_size > 0) {
        item.event_data = malloc(event_data_size);
        if (item.event_data == NULL) {
            return OSAL_ERR_NO_MEM;
        }
        memcpy(item.event_data, event_data, event_data_size);
    } else {
        item.event_data = NULL;
        item.event_data_size = 0;
    }

    int rc = pthread_mutex_lock(&loop->mutex);
    if (rc != 0) {
        platform_posix_event_free_item(&item);
        return OSAL_ERR_FAIL;
    }

    if (ticks_to_wait == 0) {
        if (loop->size == loop->capacity) {
            pthread_mutex_unlock(&loop->mutex);
            platform_posix_event_free_item(&item);
            return OSAL_ERR_TIMEOUT;
        }
    } else if (ticks_to_wait != OSAL_MAX_DELAY) {
        struct timespec abstime;
        uint32_t wait_ms = osal_ms_from_ticks(ticks_to_wait);
        rc = platform_posix_timespec_from_now_ms(&abstime, wait_ms);
        if (rc != 0) {
            pthread_mutex_unlock(&loop->mutex);
            platform_posix_event_free_item(&item);
            return OSAL_ERR_FAIL;
        }
        while (loop->size == loop->capacity && loop->running) {
            rc = pthread_cond_timedwait(&loop->cond_not_full, &loop->mutex, &abstime);
            if (rc != 0) {
                pthread_mutex_unlock(&loop->mutex);
                platform_posix_event_free_item(&item);
                return rc == ETIMEDOUT ? OSAL_ERR_TIMEOUT : OSAL_ERR_FAIL;
            }
        }
    } else {
        /* Infinite wait */
        while (loop->size == loop->capacity && loop->running) {
            pthread_cond_wait(&loop->cond_not_full, &loop->mutex);
        }
    }

    if (!loop->running) {
        pthread_mutex_unlock(&loop->mutex);
        platform_posix_event_free_item(&item);
        return OSAL_ERR_FAIL;
    }

    /* Enqueue */
    loop->queue[loop->tail] = item;
    loop->tail = (loop->tail + 1) % loop->capacity;
    loop->size++;
    pthread_cond_signal(&loop->cond_not_empty);
    pthread_mutex_unlock(&loop->mutex);
    return OSAL_ERR_OK;
}
