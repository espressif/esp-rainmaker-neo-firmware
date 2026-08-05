/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* POSIX-based scheduler implementation using a worker thread per handle */

#include "osal_scheduler.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

typedef struct platform_posix_scheduled_task {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    bool running;            /* worker thread loop flag */
    bool is_active;          /* timer is active (can be resumed) */
    bool has_due;            /* a due time is scheduled */
    bool is_periodic;        /* true for periodic tasks, false for one-shot */
    uint64_t period_ms;      /* period for periodic tasks */
    struct timespec due;     /* absolute due time */
    osal_scheduler_task_t callback;
    void *arg;
} platform_posix_scheduled_task_t;

static bool is_initialized = false;

static int platform_posix_timespec_from_now_ms(struct timespec *abstime, uint64_t wait_ms)
{
    if (abstime == NULL) {
        return -1;
    }
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        return -1;
    }
    uint64_t nsec_total = (uint64_t)now.tv_nsec + ((uint64_t)(wait_ms % 1000) * 1000000ULL);
    abstime->tv_sec = now.tv_sec + (time_t)(wait_ms / 1000) + (time_t)(nsec_total / 1000000000ULL);
    abstime->tv_nsec = (long)(nsec_total % 1000000000ULL);
    return 0;
}

static void *platform_posix_scheduler_thread(void *arg)
{
    platform_posix_scheduled_task_t *t = (platform_posix_scheduled_task_t *)arg;
    for (;;) {
        pthread_mutex_lock(&t->mutex);
        while (t->running && (!t->is_active || !t->has_due)) {
            pthread_cond_wait(&t->cond, &t->mutex);
        }
        if (!t->running) {
            pthread_mutex_unlock(&t->mutex);
            break;
        }
        /* Wait until due time - only if active and has due time */
        while (t->running && t->is_active && t->has_due) {
            int rc = pthread_cond_timedwait(&t->cond, &t->mutex, &t->due);
            if (!t->running) {
                break;
            }
            if (rc == ETIMEDOUT) {
                /* Fire */
                osal_scheduler_task_t cb = t->callback;
                void *cb_arg = t->arg;
                if (t->is_periodic) {
                    /* Periodic: reschedule next execution */
                    if (platform_posix_timespec_from_now_ms(&t->due, t->period_ms) != 0) {
                        /* Error rescheduling, stop the task */
                        t->has_due = false;
                    }
                } else {
                    /* One-shot: mark as done */
                    t->has_due = false;
                }
                pthread_mutex_unlock(&t->mutex);
                cb(cb_arg);
                pthread_mutex_lock(&t->mutex);
                /* After callback, loop to wait for next execution (periodic) or next reset (one-shot) */
                break;
            }
            /* else: was signaled due to reset/cancel; loop re-check */
        }
        pthread_mutex_unlock(&t->mutex);
    }
    return NULL;
}

osal_err_t osal_scheduler_init(void)
{
    is_initialized = true;
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_deinit(void)
{
    is_initialized = false;
    return OSAL_ERR_OK;
}

/**
 * @brief Internal helper function to schedule a task with periodic option.
 *
 * @param[out] handle Pointer to the scheduled task handle.
 * @param[in] delay_ms Delay in milliseconds between executions.
 * @param[in] task Task to be executed.
 * @param[in] arg Argument to the task.
 * @param[in] is_periodic True for periodic tasks, false for one-shot.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
static osal_err_t osal_scheduler_schedule_task_internal(osal_scheduler_task_handle_t *handle, uint64_t delay_ms, osal_scheduler_task_t task, void *arg, bool is_periodic)
{
    if (!is_initialized) {
        return OSAL_ERR_INVALID_STATE;
    }
    if (handle == NULL || task == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    platform_posix_scheduled_task_t *t = (platform_posix_scheduled_task_t *)calloc(1, sizeof(platform_posix_scheduled_task_t));
    if (t == NULL) {
        return OSAL_ERR_NO_MEM;
    }
    if (pthread_mutex_init(&t->mutex, NULL) != 0) {
        free(t);
        return OSAL_ERR_FAIL;
    }
    if (pthread_cond_init(&t->cond, NULL) != 0) {
        pthread_mutex_destroy(&t->mutex);
        free(t);
        return OSAL_ERR_FAIL;
    }
    t->callback = task;
    t->arg = arg;
    t->running = true;
    t->is_active = true;
    t->has_due = false;
    t->is_periodic = is_periodic;
    t->period_ms = is_periodic ? delay_ms : 0;
    if (platform_posix_timespec_from_now_ms(&t->due, delay_ms) != 0) {
        pthread_cond_destroy(&t->cond);
        pthread_mutex_destroy(&t->mutex);
        free(t);
        return OSAL_ERR_FAIL;
    }
    t->has_due = true;

    int rc = pthread_create(&t->thread, NULL, platform_posix_scheduler_thread, t);
    if (rc != 0) {
        pthread_cond_destroy(&t->cond);
        pthread_mutex_destroy(&t->mutex);
        free(t);
        return OSAL_ERR_FAIL;
    }
    /* Signal worker about the initial due time */
    pthread_mutex_lock(&t->mutex);
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->mutex);

    *handle = (osal_scheduler_task_handle_t)t;
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_schedule_task(osal_scheduler_task_handle_t *handle, uint64_t delay_ms, osal_scheduler_task_t task, void *arg)
{
    return osal_scheduler_schedule_task_internal(handle, delay_ms, task, arg, false);
}

osal_err_t osal_scheduler_schedule_task_periodic(osal_scheduler_task_handle_t *handle, uint64_t delay_ms, osal_scheduler_task_t task, void *arg)
{
    return osal_scheduler_schedule_task_internal(handle, delay_ms, task, arg, true);
}

osal_err_t osal_scheduler_reset_timer(osal_scheduler_task_handle_t handle, uint64_t delay_ms)
{
    if (!is_initialized) {
        return OSAL_ERR_INVALID_STATE;
    }
    if (handle == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    platform_posix_scheduled_task_t *t = (platform_posix_scheduled_task_t *)handle;
    pthread_mutex_lock(&t->mutex);
    if (platform_posix_timespec_from_now_ms(&t->due, delay_ms) != 0) {
        pthread_mutex_unlock(&t->mutex);
        return OSAL_ERR_FAIL;
    }
    t->is_active = true;
    t->has_due = true;
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_stop_timer(osal_scheduler_task_handle_t handle)
{
    if (!is_initialized) {
        return OSAL_ERR_INVALID_STATE;
    }
    if (handle == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    platform_posix_scheduled_task_t *t = (platform_posix_scheduled_task_t *)handle;
    pthread_mutex_lock(&t->mutex);
    t->is_active = false;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_cancel_task(osal_scheduler_task_handle_t *handle)
{
    if (!is_initialized) {
        /*
         * Scheduler is torn down; callers must not keep using this handle. Clear it
         * so retry/backoff layers do not retain a dangling pointer (cancel is a no-op
         * once the scheduler thread is gone - the task may leak until process exit).
         */
        if (handle != NULL) {
            *handle = NULL;
        }
        return OSAL_ERR_INVALID_STATE;
    }
    if (handle == NULL || *handle == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    platform_posix_scheduled_task_t *t = (platform_posix_scheduled_task_t *)*handle;
    pthread_mutex_lock(&t->mutex);
    t->running = false;
    t->has_due = false;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->mutex);

    pthread_join(t->thread, NULL);
    pthread_cond_destroy(&t->cond);
    pthread_mutex_destroy(&t->mutex);
    free(t);
    *handle = NULL;
    return OSAL_ERR_OK;
}
