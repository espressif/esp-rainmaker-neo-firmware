/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_schedule_port_osal.c
 * @brief osal-backed port for the esp_schedule component.
 */

#include "esp_schedule_port_osal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_scheduler.h"
#include "osal_time.h"
#include "osal_semaphore.h"
#include "osal_ticks.h"

/* Types ************************************************************************/

/**
 * @brief What esp_schedule holds as its ``esp_schedule_timer_handle_t``.
 *
 * The callback pair has to be reachable from stop/cancel, and osal hands back
 * only its own task handle, so this record is the handle and owns the osal task.
 *
 * Records live in a pool and are **never returned to the heap** - see
 * ::__pool_acquire. Releasing one only marks it free, so a dispatch still
 * holding a reference to it can never read freed memory.
 */
typedef struct {
    osal_scheduler_task_handle_t task; /**< Underlying osal scheduled task. */
    esp_schedule_timer_cb_t cb;        /**< esp_schedule's timer callback. */
    void *priv_data;                   /**< Argument for ``cb``. */
    uint16_t index;                    /**< Own pool index; identifies the slot. */
    uint16_t gen;                      /**< Bumped on release; stale ids stop matching. */
    uint16_t next_free;                /**< Free-list link, ::PORT_NO_SLOT when in use. */
    bool valid;                        /**< False while the slot sits on the free list. */
} __port_timer_t;

/** Slots per pool block. Blocks are allocated on demand and never freed, so
 *  slot addresses are stable for the life of the process. */
#define PORT_BLOCK_SLOTS 8
/** Upper bound on pool growth: 64 * 8 = 512 concurrent timers. */
#define PORT_MAX_BLOCKS  64
/** Free-list terminator / "no such slot". */
#define PORT_NO_SLOT     0xFFFFU

/* Variables ********************************************************************/

static const char *TAG = "rmng_sched_port";

/**
 * @brief Serializes a running timer callback against ::__timer_cancel.
 *
 * ``esp_schedule_delete`` frees the schedule right after cancelling its timer,
 * and the schedule is what the callback receives as ``priv_data``, so cancel
 * must not return while a callback is still running - the port contract requires
 * exactly that. Recursive because the contract also requires tolerating cancel
 * from *inside* the callback (esp_schedule supports a one-shot deleting itself),
 * where the callback already holds this mutex.
 *
 * It is equally the *serializer* the port contract demands (see the note on
 * ``esp_schedule_timer_ops_t``): at most one ``esp_schedule_timer_cb_t`` may run
 * at a time across all timers, or the component's self-delete detection
 * (``s_dispatching``) frees a schedule whose callback is still on the stack.
 * That is not free on every backend - the POSIX scheduler runs one thread per
 * timer - so holding this across the whole callback body is load-bearing, not
 * merely a cancel barrier.
 *
 * Created once in ::esp_schedule_port_osal_get, which runs single-threaded at
 * init, rather than lazily on first arm: two tasks arming their first timer
 * concurrently would both see NULL, and the second create would strand the
 * first mutex with callbacks already serialising on it.
 */
static osal_semaphore_handle_t __cb_mutex = NULL;

/* Timer record pool ************************************************************/

/*
 * Why a pool rather than one malloc per timer.
 *
 * osal's ``cancel`` is documented non-blocking ("a task already executing when
 * the cancel lands runs to completion... do not free heap state passed as the
 * task argument right after cancelling" - osal_scheduler.h), and only some
 * backends actually suppress a dispatch that is already under way. So a record
 * handed to osal as the task argument cannot safely be freed on cancel: the
 * dispatch may still be holding it.
 *
 * Slots are therefore allocated in blocks and never freed. Releasing a slot only
 * marks it reusable, so a stale dispatch always reads live memory. To stop it
 * acting on a *recycled* slot, what osal carries is not a pointer but a packed
 * ``(gen << 16) | index`` id: release bumps ``gen``, so a dispatch issued before
 * the release no longer matches and is dropped. Without that a cancel-then-arm
 * (every cloud push that replaces a node's schedules) could fire the new
 * schedule at the old one's due time.
 *
 * All of this is mutated under ::__cb_mutex, which the trampoline takes before
 * touching the pool at all - the packed id is an integer, so nothing is
 * dereferenced until the lock is held.
 */

static __port_timer_t *__blocks[PORT_MAX_BLOCKS];
static uint16_t __block_count = 0;
static uint16_t __free_head = PORT_NO_SLOT;

static __port_timer_t *__slot_at(uint16_t index)
{
    return &__blocks[index / PORT_BLOCK_SLOTS][index % PORT_BLOCK_SLOTS];
}

/** Add one block to the pool and thread its slots onto the free list. */
static bool __pool_grow(void)
{
    if (__block_count >= PORT_MAX_BLOCKS) {
        OSAL_LOGE(TAG, "Timer pool exhausted at %d slots; schedule cannot be armed",
                  PORT_MAX_BLOCKS * PORT_BLOCK_SLOTS);
        return false;
    }
    __port_timer_t *block = (__port_timer_t *)OSAL_CALLOC_EXTRAM(PORT_BLOCK_SLOTS, sizeof(*block));
    if (block == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate a timer pool block");
        return false;
    }
    __blocks[__block_count] = block;
    const uint16_t base = (uint16_t)(__block_count * PORT_BLOCK_SLOTS);
    /* Descending, so the list pops in ascending index order. */
    for (int i = PORT_BLOCK_SLOTS - 1; i >= 0; i--) {
        block[i].index = (uint16_t)(base + i);
        block[i].next_free = __free_head;
        __free_head = (uint16_t)(base + i);
    }
    __block_count++;
    return true;
}

/** Take a free slot, growing the pool if needed. Caller holds ::__cb_mutex. */
static __port_timer_t *__pool_acquire(void)
{
    if (__free_head == PORT_NO_SLOT && !__pool_grow()) {
        return NULL;
    }
    __port_timer_t *t = __slot_at(__free_head);
    __free_head = t->next_free;
    t->next_free = PORT_NO_SLOT;
    t->valid = true;
    return t;
}

/** Mark a slot reusable. Caller holds ::__cb_mutex. Never frees the memory. */
static void __pool_release(__port_timer_t *t)
{
    t->valid = false;
    t->gen++;            /* invalidates every id already handed to osal */
    t->cb = NULL;
    t->priv_data = NULL;
    t->task = NULL;
    t->next_free = __free_head;
    __free_head = t->index;
}

/** Resolve a packed id, or NULL if the slot was released or recycled since. */
static __port_timer_t *__pool_lookup(uint32_t id)
{
    const uint16_t index = (uint16_t)(id & 0xFFFFU);
    const uint16_t gen = (uint16_t)(id >> 16);
    if (index >= (uint16_t)(__block_count * PORT_BLOCK_SLOTS)) {
        return NULL;
    }
    __port_timer_t *t = __slot_at(index);
    if (!t->valid || t->gen != gen || t->cb == NULL) {
        return NULL;
    }
    return t;
}

static uint32_t __pool_id_of(const __port_timer_t *t)
{
    return ((uint32_t)t->gen << 16) | t->index;
}

/* Timer ops ********************************************************************/

/** Runs the esp_schedule callback with ::__cb_mutex held. */
static void __timer_trampoline(void *arg)
{
    /* ``arg`` is a packed id, not a pointer - nothing is dereferenced before the
     * lock, which is what makes a stale dispatch safe. */
    const uint32_t id = (uint32_t)(uintptr_t)arg;
    if (__cb_mutex == NULL) {
        return;
    }
    osal_semaphore_take_recursive(__cb_mutex, OSAL_MAX_DELAY);
    __port_timer_t *timer = __pool_lookup(id);
    if (timer != NULL) {
        timer->cb(timer->priv_data);
    }
    osal_semaphore_give_recursive(__cb_mutex);
}

static bool __timer_start(esp_schedule_timer_handle_t *p_timer_handle, uint32_t delay_seconds,
                          esp_schedule_timer_cb_t cb, void *priv_data)
{
    if (p_timer_handle == NULL || cb == NULL) {
        return false;
    }
    const uint64_t delay_ms = ((uint64_t) delay_seconds) * 1000;

    /* Reuse on every later arm, as the contract requires: esp_schedule arms
     * through this one entry point, including the re-arm a repeating schedule
     * issues from inside its own fired callback. Creating a second osal task
     * would leak the first and leave it firing on the stale deadline.
     *
     * ``osal_scheduler_reset_timer`` re-arms the existing task (a stopped task is
     * resumable through it) and is non-blocking on both backends, so it is safe
     * on the re-arm-from-callback path. The callback pair is rebound because the
     * contract says it may change. */
    if (__cb_mutex == NULL) {
        OSAL_LOGE(TAG, "No callback mutex; refusing to arm rather than run unsynchronized");
        return false;
    }

    if (*p_timer_handle != NULL) {
        /* Re-arm keeps the same slot and generation: it is the same timer, so a
         * dispatch already in flight for it is not stale. */
        __port_timer_t *timer = (__port_timer_t *)(*p_timer_handle);
        osal_semaphore_take_recursive(__cb_mutex, OSAL_MAX_DELAY);
        timer->cb = cb;
        timer->priv_data = priv_data;
        osal_semaphore_give_recursive(__cb_mutex);
        return osal_scheduler_reset_timer(timer->task, delay_ms) == OSAL_ERR_OK;
    }

    osal_semaphore_take_recursive(__cb_mutex, OSAL_MAX_DELAY);
    __port_timer_t *timer = __pool_acquire();
    if (timer == NULL) {
        osal_semaphore_give_recursive(__cb_mutex);
        return false;
    }
    timer->cb = cb;
    timer->priv_data = priv_data;
    /* The trampoline is registered rather than ``cb`` itself so the callback runs
     * under the mutex cancel barriers against, and it is handed the slot's packed
     * id rather than its address so a stale dispatch can be recognised. */
    const uint32_t id = __pool_id_of(timer);
    if (osal_scheduler_schedule_task(&timer->task, delay_ms, __timer_trampoline,
                                     (void *)(uintptr_t)id) != OSAL_ERR_OK) {
        __pool_release(timer);
        osal_semaphore_give_recursive(__cb_mutex);
        return false;
    }
    *p_timer_handle = (esp_schedule_timer_handle_t) timer;
    osal_semaphore_give_recursive(__cb_mutex);
    return true;
}

static void __timer_stop(esp_schedule_timer_handle_t timer_handle)
{
    if (timer_handle == NULL) {
        return;
    }
    osal_scheduler_stop_timer(((__port_timer_t *)timer_handle)->task);
}

static void __timer_cancel(esp_schedule_timer_handle_t *p_timer_handle)
{
    if (p_timer_handle == NULL || *p_timer_handle == NULL) {
        return;
    }
    __port_timer_t *timer = (__port_timer_t *)(*p_timer_handle);

    /* Stop dispatching first. Backends differ in whether this also suppresses a
     * dispatch already under way, which is exactly why the slot is not freed
     * below - see the pool note above. */
    osal_scheduler_cancel_task(&timer->task);

    /* Taking the mutex is the barrier esp_schedule's ``cancel`` contract asks
     * for: it cannot be held while a callback runs, so acquiring it means none
     * is. Releasing the slot under the same lock makes "callback finished" and
     * "slot reusable" a single step. Taken recursively because the caller may be
     * the callback itself, where there is nothing to wait for and a plain take
     * would deadlock. */
    if (__cb_mutex) {
        osal_semaphore_take_recursive(__cb_mutex, OSAL_MAX_DELAY);
        __pool_release(timer);
        osal_semaphore_give_recursive(__cb_mutex);
    }

    *p_timer_handle = NULL;
}

/* Time ops *********************************************************************/

static time_t __get_time(time_t *p_time)
{
    /* Must go through osal rather than calling time() directly: ``osal_get_time``
     * is a function-pointer variable that the virtual scheduler retargets, which
     * is how firmware tests move the clock. */
    return osal_get_time(p_time);
}

/* Log ops **********************************************************************/

/* Named ``__port_log`` rather than ``__log``: glibc declares ``__log`` as the
 * internal alias for ``log()`` in <bits/mathcalls.h>. */
static void __port_log(esp_schedule_log_level_t level, const char *tag, const char *message)
{
    switch (level) {
    case ESP_SCHEDULE_LOG_ERROR:
        OSAL_LOGE(tag, "%s", message);
        break;
    case ESP_SCHEDULE_LOG_WARN:
        OSAL_LOGW(tag, "%s", message);
        break;
    case ESP_SCHEDULE_LOG_INFO:
        OSAL_LOGI(tag, "%s", message);
        break;
    case ESP_SCHEDULE_LOG_DEBUG:
        OSAL_LOGD(tag, "%s", message);
        break;
    default:
        OSAL_LOGV(tag, "%s", message);
        break;
    }
}

/* Memory ops *******************************************************************/

/* osal's allocators are macros (on ESP they expand to heap_caps_*_prefer), so
 * they cannot be assigned to the port's function pointers directly. These
 * wrappers put esp_schedule's internal allocations on the same
 * external-RAM-preferring heap as the rest of the SDK.
 *
 * There is no matching OSAL_FREE: heap_caps allocations are released with plain
 * free(), so the port hands over ``free`` as-is. */
static void *__mem_malloc(size_t size)
{
    return OSAL_MALLOC_EXTRAM(size);
}

static void *__mem_calloc(size_t num, size_t size)
{
    return OSAL_CALLOC_EXTRAM(num, size);
}

/* Port table *******************************************************************/

const esp_schedule_port_config_t *esp_schedule_port_osal_get(void)
{
    /* Single-threaded init is the only safe place to create ::__cb_mutex; see
     * the note there. Idempotent because the tests fetch the table on every
     * setup, and nothing ever destroys the mutex. */
    if (__cb_mutex == NULL) {
        __cb_mutex = osal_semaphore_create_recursive_mutex();
        if (__cb_mutex == NULL) {
            OSAL_LOGE(TAG, "Failed to create the timer callback mutex; callbacks cannot be serialized and cancel cannot barrier against a running one");
        }
    }

    static const esp_schedule_port_config_t port = {
        .timer = {
            .start = __timer_start,
            .stop = __timer_stop,
            .cancel = __timer_cancel,
        },
        /* Storage left NULL on purpose: the schedule service persists the
         * details JSON itself, because esp_schedule stores only the trigger
         * config and not the action a fired schedule has to apply. */
        .nvs = {0},
        .time_sync = {
            .get_time = __get_time,
            /* osal owns time synchronisation; esp_schedule must not start its
             * own SNTP. */
            .timesync_init = NULL,
        },
        .mem = {
            .malloc = __mem_malloc,
            .calloc = __mem_calloc,
            .free = free,
        },
        .log = __port_log,
    };
    return &port;
}
