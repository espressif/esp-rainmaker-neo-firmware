/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file scheduler_virtual.c
 * @brief Platform common virtual scheduler. Maintains real-time clock ticking while allowing for manual time control.
 */

#include "osal_scheduler.h"
#include "osal_time_control.h"
#include "osal_task.h"
#include "osal_mem_alloc.h"
#include "osal_semaphore.h"
#include "osal_log.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>
#include <inttypes.h>

#include "sdkconfig.h"

/* Preprocessor definitions ******************************************************/

#define VIRTUAL_SCHEDULER_RESOLUTION_MS CONFIG_OSAL_VIRTUAL_SCHEDULER_RESOLUTION_MS
#define VIRTUAL_SCHEDULER_TASK_NAME "virtual_scheduler" // task name
#define VIRTUAL_SCHEDULER_TASK_STACK_DEPTH CONFIG_OSAL_VIRTUAL_SCHEDULER_TASK_STACK_SIZE // task stack depth
#define VIRTUAL_SCHEDULER_TASK_PRIORITY CONFIG_OSAL_VIRTUAL_SCHEDULER_TASK_PRIORITY // task priority

// Maximum number of executions to perform for a task in a single iteration.
// This is to prevent a periodic task from having too many executions due to time advancement.
#define VIRTUAL_SCHEDULER_EXECUTIONS_MAX CONFIG_OSAL_VIRTUAL_SCHEDULER_EXECUTIONS_MAX

/* Types ************************************************************************/

/**
 * @brief Scheduled task.
 */
typedef struct __scheduled_task {
    osal_scheduler_task_t task;
    void *arg;
    uint64_t due_time_ms; // Absolute due time in milliseconds
    bool is_periodic;     // true for periodic tasks, false for one-shot
    uint64_t period_ms;   // period for periodic tasks
    uint32_t num_executions; // Number of executions to perform for this task
    struct __scheduled_task *next;
} __scheduled_task_t;

/* Variables ********************************************************************/

/**
 * @brief Handle of the scheduler task.
 */
static osal_task_handle_t __scheduler_task_handle = NULL;

/**
 * @brief List of scheduled tasks.
 * @note The list is sorted by due time upon insertion.
 */
static struct {
    __scheduled_task_t *head;
    osal_semaphore_handle_t mutex;
} __scheduled_tasks;

/**
 * @brief Virtual time in milliseconds (managed internally by scheduler)
 */
static atomic_uint_fast64_t __virtual_time_ms = 0;

/**
 * @brief Last real time checkpoint for tracking real-time progression
 */
static atomic_uint_fast64_t __last_real_time_ms = 0;

/* Private functions *************************************************************/

/**
 * @brief Get current real time in milliseconds
 */
static uint64_t __get_real_time_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
    }
    return 0;
}

/**
 * @brief Get current virtual time in milliseconds
 */
static uint64_t __get_virtual_time_ms(void)
{
    return atomic_load(&__virtual_time_ms);
}

/**
 * @brief Initialize virtual time from osal_get_time
 */
static void __init_virtual_time(void)
{
    atomic_store(&__virtual_time_ms, __get_real_time_ms());
    atomic_store(&__last_real_time_ms, __get_real_time_ms());
}

/**
 * @brief Update virtual time based on real time progression
 */
static void __update_virtual_time(void)
{
    // Normal real-time progression
    uint64_t current_real_time = __get_real_time_ms();
    uint64_t last_real_time = atomic_load(&__last_real_time_ms);

    if (current_real_time > last_real_time) {
        uint64_t real_time_delta = current_real_time - last_real_time;
        atomic_fetch_add(&__virtual_time_ms, real_time_delta);
        atomic_store(&__last_real_time_ms, current_real_time);
    }
}

/**
 * @brief Lock the scheduled tasks.
 */
static bool __scheduled_tasks_lock(void)
{
    if (__scheduled_tasks.mutex == NULL) {
        return false;
    }
    osal_semaphore_take(__scheduled_tasks.mutex, OSAL_MAX_DELAY);
    return true;
}

/**
 * @brief Unlock the scheduled tasks.
 */
static void __scheduled_tasks_unlock(void)
{
    if (__scheduled_tasks.mutex == NULL) {
        return;
    }
    osal_semaphore_give(__scheduled_tasks.mutex);
}

/**
 * @brief Set the due time of a scheduled task.
 * @param[in] t The scheduled task.
 * @param[in] delay_ms The delay in milliseconds.
 */
static void __set_due_time(__scheduled_task_t *t, uint64_t delay_ms)
{
    uint64_t current_virtual_time = __get_virtual_time_ms();
    t->due_time_ms = current_virtual_time + delay_ms;

    OSAL_LOGD(VIRTUAL_SCHEDULER_TASK_NAME,
              "Set due time for task %p to %" PRIu64 " ms (current: %" PRIu64 ", delay: %" PRIu64 ")",
              t, t->due_time_ms, current_virtual_time, delay_ms);
}

/**
 * @brief Get a scheduled task.
 * @param[in] task The task to schedule.
 * @param[in] arg The argument to pass to the task.
 * @param[in] delay_ms The delay in milliseconds.
 * @param[in] is_periodic Whether the task is periodic.
 * @param[in] period_ms The period for periodic tasks (ignored for one-shot).
 * @return The scheduled task.
 */
static __scheduled_task_t *__get_scheduled_task(osal_scheduler_task_t task, void *arg, uint64_t delay_ms, bool is_periodic, uint64_t period_ms)
{
    __scheduled_task_t *t = (__scheduled_task_t *)OSAL_CALLOC_EXTRAM(1, sizeof(__scheduled_task_t));
    if (t == NULL) {
        return NULL;
    }
    __set_due_time(t, delay_ms);
    t->task = task;
    t->arg = arg;
    t->is_periodic = is_periodic;
    t->period_ms = is_periodic ? period_ms : 0;
    t->next = NULL;
    return t;
}

/**
 * @brief Free the scheduled tasks.
 */
static void __scheduled_tasks_free(void)
{
    __scheduled_task_t *t = __scheduled_tasks.head;
    while (t) {
        __scheduled_task_t *next = t->next;
        free(t);
        t = next;
    }
    __scheduled_tasks.head = NULL;
}

/**
 * @brief Add a scheduled task to the list. The list is sorted by due time upon insertion, in ascending order.
 * @param[in] t The scheduled task to add.
 */
static void __scheduled_task_add(__scheduled_task_t *t)
{
    __scheduled_task_t *current = __scheduled_tasks.head;
    __scheduled_task_t *prev = NULL;

    // Insert in ascending order by due_time_ms
    while (current && current->due_time_ms <= t->due_time_ms) {
        prev = current;
        current = current->next;
    }

    t->next = current;
    if (prev) {
        prev->next = t;
    } else {
        __scheduled_tasks.head = t;
    }
}

/**
 * @brief Pop a scheduled task from the list.
 * @return The scheduled task popped.
 */
static __scheduled_task_t *__scheduled_task_pop(void)
{
    __scheduled_task_t *t = __scheduled_tasks.head;
    if (t != NULL) {
        __scheduled_tasks.head = t->next;
        t->next = NULL;
    }
    return t;
}

/**
 * @brief Remove a scheduled task from the list.
 * @note The task is not freed.
 * @param[in] t The scheduled task to remove.
 */
static void __scheduled_task_remove(__scheduled_task_t *t)
{
    __scheduled_task_t *current = __scheduled_tasks.head;
    __scheduled_task_t *prev = NULL;
    while (current != NULL) {
        if (current == t) {
            if (prev == NULL) {
                __scheduled_tasks.head = current->next;
            } else {
                prev->next = current->next;
            }
            return;
        }
        prev = current;
        current = current->next;
    }
}

/**
 * @brief Virtual scheduler task.
 * @param[in] arg unused
 */
static void __virtual_scheduler_task(void *arg)
{
    // unused
    (void) arg;

    while (1) {
        // Update virtual time based on real time progression
        __update_virtual_time();

        uint64_t current_virtual_time = __get_virtual_time_ms();

        /* Queue tasks to be executed */
        __scheduled_task_t *exec_head = NULL, *exec_tail = NULL;
        if (!__scheduled_tasks_lock()) {
            continue;
        }

        while (__scheduled_tasks.head &&
                __scheduled_tasks.head->due_time_ms <= current_virtual_time) {

            OSAL_LOGD(VIRTUAL_SCHEDULER_TASK_NAME,
                      "Executing task %p with due time %" PRIu64 " (current: %" PRIu64 ")",
                      __scheduled_tasks.head,
                      __scheduled_tasks.head->due_time_ms,
                      current_virtual_time);

            __scheduled_task_t *t = __scheduled_task_pop();

            /* For periodic tasks, calculate the number of executions to perform */
            if (t->is_periodic) {
                /* Calculate how many times this periodic task should have executed since it was last due.
                 * time_since_due / period_ms = number of complete periods elapsed
                 * + 1 = include the current execution that made the task due
                 * Example: if 3.5 periods have passed, execute floor(3.5) + 1 = 4 times */
                uint64_t time_since_due = current_virtual_time - t->due_time_ms;
                t->num_executions = (time_since_due / t->period_ms) + 1;

                /* Advance the due time by num_executions * period_ms to schedule the next execution */
                t->due_time_ms += t->num_executions * t->period_ms;
                __scheduled_task_add(t);

                /* Cap the number of executions at VIRTUAL_SCHEDULER_EXECUTIONS_MAX */
                if (t->num_executions > VIRTUAL_SCHEDULER_EXECUTIONS_MAX) {
                    OSAL_LOGW(VIRTUAL_SCHEDULER_TASK_NAME,
                              "Scheduled periodic task %p tried to execute %" PRIu32 " time(s), capping at %" PRIu32,
                              t, t->num_executions, (uint32_t) VIRTUAL_SCHEDULER_EXECUTIONS_MAX);
                    t->num_executions = VIRTUAL_SCHEDULER_EXECUTIONS_MAX;
                }
            }
            /* For one-shot tasks, set the number of executions to 1 */
            else {
                t->num_executions = 1;
            }

            __scheduled_task_t *copy = (__scheduled_task_t *)OSAL_CALLOC_EXTRAM(1, sizeof(__scheduled_task_t));
            if (copy) {
                copy->task = t->task;
                copy->arg = t->arg;
                copy->num_executions = t->num_executions;
                copy->next = NULL;

                /* Add to end of exec_tail */
                if (!exec_tail) {
                    exec_head = copy;
                    exec_tail = copy;
                } else {
                    exec_tail->next = copy;
                    exec_tail = copy;
                }
            }
        }
        __scheduled_tasks_unlock();

        /* Execute tasks */
        while (exec_head) {
            __scheduled_task_t *t = exec_head;
            exec_head = t->next;
            for (uint32_t i = 0; i < t->num_executions; i++) {
                t->task(t->arg);
            }
            free(t);
        }

        /* Delay before next iteration */
        osal_task_delay(osal_ticks_from_ms(VIRTUAL_SCHEDULER_RESOLUTION_MS));
    }

    osal_task_delete(NULL);
}

osal_err_t osal_scheduler_init(void)
{
    if (__scheduler_task_handle != NULL) {
        OSAL_LOGW(VIRTUAL_SCHEDULER_TASK_NAME, "Scheduler already initialized, skipping initialization");
        return OSAL_ERR_OK;
    }

    __scheduled_tasks.mutex = osal_semaphore_create_mutex();
    if (__scheduled_tasks.mutex == NULL) {
        return OSAL_ERR_NO_MEM;
    }

    __init_virtual_time();

    osal_err_t err;
    err =  osal_task_create(__virtual_scheduler_task, VIRTUAL_SCHEDULER_TASK_NAME, VIRTUAL_SCHEDULER_TASK_STACK_DEPTH, NULL, VIRTUAL_SCHEDULER_TASK_PRIORITY, &__scheduler_task_handle);
    if (err != OSAL_ERR_OK) {
        return err;
    }
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_deinit(void)
{
    if (__scheduler_task_handle != NULL) {
        osal_task_delete(__scheduler_task_handle);
        __scheduler_task_handle = NULL;
    }

    if (__scheduled_tasks.mutex != NULL) {
        __scheduled_tasks_lock();
        __scheduled_tasks_free();
        __scheduled_tasks_unlock();
        osal_semaphore_delete(__scheduled_tasks.mutex);
        __scheduled_tasks.mutex = NULL;
    }
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
    __scheduled_task_t *t = __get_scheduled_task(task, arg, delay_ms, is_periodic, is_periodic ? delay_ms : 0);
    if (t == NULL) {
        return OSAL_ERR_NO_MEM;
    }
    __scheduled_tasks_lock();
    __scheduled_task_add(t);
    __scheduled_tasks_unlock();
    *handle = (osal_scheduler_task_handle_t) t;
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
    __scheduled_task_t *t = (__scheduled_task_t *)handle;

    __scheduled_tasks_lock();
    __scheduled_task_remove(t);
    __set_due_time(t, delay_ms);
    __scheduled_task_add(t);
    __scheduled_tasks_unlock();
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_stop_timer(osal_scheduler_task_handle_t handle)
{
    __scheduled_task_t *t = (__scheduled_task_t *)handle;
    __scheduled_tasks_lock();
    __scheduled_task_remove(t);
    __scheduled_tasks_unlock();
    return OSAL_ERR_OK;
}

osal_err_t osal_scheduler_cancel_task(osal_scheduler_task_handle_t *handle)
{
    __scheduled_task_t *t = (__scheduled_task_t *) *handle;
    __scheduled_tasks_lock();
    __scheduled_task_remove(t);
    free(t);
    __scheduled_tasks_unlock();
    *handle = NULL;
    return OSAL_ERR_OK;
}

/* Time control functions *********************************************************/

static time_t __get_time(time_t *time)
{
    time_t t = __get_virtual_time_ms() / 1000;
    if (time) {
        *time = t;
    }
    return t;
}
osal_time_func osal_get_time = __get_time;

static uint64_t __get_time_ms(uint64_t *time_ms)
{
    uint64_t t = __get_virtual_time_ms();
    if (time_ms) {
        *time_ms = t;
    }
    return t;
}
osal_time_ms_func osal_get_time_ms = __get_time_ms;

bool osal_time_control_set_time(time_t time)
{
    atomic_store(&__virtual_time_ms, (uint64_t)time * 1000);
    return true;
}

bool osal_time_control_advance_time(time_t time_diff)
{
    atomic_fetch_add(&__virtual_time_ms, (uint64_t)time_diff * 1000);
    return true;
}
