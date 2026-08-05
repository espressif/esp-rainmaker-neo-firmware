/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_task.h"

/* Standard includes */
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

/* Structure definitions ******************************************************/

/**
 * @brief Metadata for a task.
 *
 * @note This is a private structure and should not be used directly.
 */
typedef struct posix_task_metadata {
    pthread_t thread;
    const char *name;
    uint32_t stack_depth;
    void *param;
    uint32_t priority;
    struct posix_task_metadata *next;
} posix_task_metadata_t;

/* Global variables ************************************************************/

static posix_task_metadata_t *posix_task_metadata_head = NULL;

/* Static function declarations ***********************************************/

static void posix_task_metadata_add(pthread_t thread, const char *name, uint32_t stack_depth, void *param, uint32_t priority);
static void posix_task_metadata_remove(pthread_t thread);

/* Static function definitions ***********************************************/

static void posix_task_metadata_add(pthread_t thread, const char *name, uint32_t stack_depth, void *param, uint32_t priority)
{
    posix_task_metadata_t *metadata = (posix_task_metadata_t *)malloc(sizeof(posix_task_metadata_t));
    if (metadata == NULL) {
        return;
    }

    metadata->thread = thread;
    metadata->name = name;
    metadata->stack_depth = stack_depth;
    metadata->param = param;
    metadata->priority = priority;
    metadata->next = posix_task_metadata_head;
    posix_task_metadata_head = metadata;
}

static void posix_task_metadata_remove(pthread_t thread)
{
    posix_task_metadata_t **current = &posix_task_metadata_head;
    while (*current) {
        if ((*current)->thread == thread) {
            posix_task_metadata_t *to_remove = *current;
            *current = (*current)->next;
            free(to_remove);
            return;
        }
        current = &(*current)->next;
    }
}

/* Function definitions ********************************************************/

osal_err_t  osal_task_create(
    osal_task_function_t task_function,
    const char *name,
    uint32_t stack_depth,
    void *parameters,
    uint32_t priority,
    osal_task_handle_t *task_handle
)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, (void *(*)(void *))task_function, parameters) != 0) {
        return OSAL_ERR_INVALID_STATE;
    }

    posix_task_metadata_add(thread, name, stack_depth, parameters, priority);
    if (task_handle != NULL) {
        *task_handle = (osal_task_handle_t) thread;
    }
    return OSAL_ERR_OK;
}

void osal_task_delete(osal_task_handle_t task_handle)
{
    pthread_t thread;
    if (task_handle == NULL) {
        thread = pthread_self();
    } else {
        thread = (pthread_t) task_handle;
    }

    if (pthread_self() == thread) {
        pthread_exit(NULL);
    } else {
        pthread_cancel(thread);
        pthread_join(thread, NULL);
    }

    posix_task_metadata_remove(thread);
}

void osal_task_delay(osal_tick_type_t ticks_to_delay)
{
    struct timespec ts;
    ts.tv_sec = ticks_to_delay / 1000;
    ts.tv_nsec = (ticks_to_delay % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

osal_tick_type_t osal_task_get_tick_count(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (osal_tick_type_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

const char *osal_task_get_name(osal_task_handle_t task_handle)
{
    pthread_t thread = (pthread_t) task_handle;
    if (thread == (pthread_t) NULL) {
        thread = pthread_self();
    }

    posix_task_metadata_t *current = posix_task_metadata_head;
    while (current) {
        if (current->thread == thread) {
            return current->name;
        }
        current = current->next;
    }
    return NULL;
}
