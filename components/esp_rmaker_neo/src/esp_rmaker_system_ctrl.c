/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_system_ctrl.c
 * @brief System control functions.
 */

/* Includes **************************************************************/

/* Declarations */
#include "esp_rmaker_system_ctrl.h"
#include "system_ctrl_internal.h"

/* Standard includes */
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

/* Platform common includes */
#include "osal_sysctrl.h"
#include "osal_log.h"
#include "osal_scheduler.h"
#include "osal_task.h"
#include "osal_semaphore.h"
#include "osal_ticks.h"

/* NVS common includes */
#include "osal_storage.h"
#include "constants/nvs.h"

/* Event loop includes */
#include "event_loop.h"

/* Node / data model (for clearing per-device stored values) */
#include "sdkconfig.h"
#include "esp_rmaker_node.h"

#include "node_internal.h"
#include "services/schedules.h"
#include "esp_rmaker_data_model.h"

/* Notification includes */
#include "network/notify.h"

/* Preprocessor definitions **************************************************************/

/* Max time to wait for the node_reset notification's QoS1 publish to flush before wiping. */
#define SYSTEM_CTRL_NODE_RESET_NOTIFY_TIMEOUT_MS 3000

/* Worker task that runs delayed resets outside the scheduler's context. */
#define SYSTEM_CTRL_RESET_TASK_STACK_SIZE CONFIG_ESP_RMAKER_NEO_SYSTEM_CTRL_RESET_TASK_STACK_SIZE
#define SYSTEM_CTRL_RESET_TASK_PRIORITY   CONFIG_ESP_RMAKER_NEO_SYSTEM_CTRL_RESET_TASK_PRIORITY

/* Max number of registered factory reset participants. */
#define SYSTEM_CTRL_FACTORY_RESET_PARTICIPANT_MAX_COUNT CONFIG_ESP_RMAKER_NEO_SYSTEM_CTRL_RESET_PARTICIPANTS_MAX

/* Types **************************************************************/

typedef struct __system_ctrl_reset_data __system_ctrl_reset_data_t;

/* Kinds of delayed reset; each has one pending slot, so a repeated request replaces the earlier one.
 * The most overdue reset runs first; resets due at the same time run in this order, so keep it:
 * least to most destructive, ending with the factory reset, whose wipe and reboot supersede the others. */
typedef enum {
    SYSTEM_CTRL_RESET_DATA = 0,
    SYSTEM_CTRL_RESET_NETWORK,
    SYSTEM_CTRL_RESET_FACTORY,
    SYSTEM_CTRL_RESET_TYPE_COUNT,
} __system_ctrl_reset_type_t;

/* Reset routine that the worker task runs. */
typedef esp_rmaker_error_t (*__system_ctrl_reset_run_fn_t)(const __system_ctrl_reset_data_t *reset_data);

/**
 * @brief System control private data.
 */
struct __system_ctrl_reset_data {
    /** The timeout in seconds to reboot the system after resetting the network credentials.
     * 0 means reboot immediately; a negative value means do not reboot.
     */
    int8_t reset_reboot_s;

    /** The function to reset the network credentials. */
    esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn;

    /** The reset routine that the worker task runs. */
    __system_ctrl_reset_run_fn_t run;
};

/**
 * @brief One pending delayed reset.
 */
typedef struct {
    /** Whether this slot holds a reset waiting to run. */
    bool pending;

    /** Tick count at which the reset is due. */
    osal_tick_type_t deadline;

    /** What to run when the reset is due. */
    __system_ctrl_reset_data_t data;
} __system_ctrl_reset_slot_t;

/* Global variables **************************************************************/

/* Tag for logging */
static const char *TAG = "rmng_system_ctrl";

/* Reset type names for logging. */
static const char *const g_reset_type_names[SYSTEM_CTRL_RESET_TYPE_COUNT] = {
    [SYSTEM_CTRL_RESET_NETWORK] = "NETWORK RESET",
    [SYSTEM_CTRL_RESET_FACTORY] = "FACTORY RESET",
    [SYSTEM_CTRL_RESET_DATA] = "DATA RESET",
};

/* Default network-credential reset function, set via esp_rmaker_system_ctrl_register_network_reset_fn(). */
static esp_rmaker_system_ctrl_network_reset_fn_t g_network_reset_fn = NULL;

/* Registered factory reset participants, in registration order. */
static esp_rmaker_system_ctrl_factory_reset_participant_t g_factory_reset_participants[SYSTEM_CTRL_FACTORY_RESET_PARTICIPANT_MAX_COUNT];
static uint8_t g_factory_reset_participant_count = 0;

/* Lifecycle of the worker task that runs delayed resets; started on first use and kept after. */
typedef enum {
    RESET_WORKER_STOPPED = 0,
    RESET_WORKER_STARTING,
    RESET_WORKER_RUNNING,
} __reset_worker_state_t;
static atomic_int g_reset_worker_state = RESET_WORKER_STOPPED;

/* Pending delayed resets, one slot per type, guarded by g_reset_slots_lock. The lock and the wake
 * signal are valid once the worker is running. */
static __system_ctrl_reset_slot_t g_reset_slots[SYSTEM_CTRL_RESET_TYPE_COUNT] = {0};
static osal_semaphore_handle_t g_reset_slots_lock = NULL;

/* Given whenever a slot changes, so the worker re-evaluates the earliest deadline. */
static osal_semaphore_handle_t g_reset_wake = NULL;

/* Set while a factory reset is running, and kept set once its reboot is scheduled. A reset requested
 * meanwhile - from a participant's own wipe, or from another task - is a no-op. */
static atomic_bool g_factory_reset_in_progress = false;

/* Private function declarations **********************************************************/

/**
 * @brief Schedule a task.
 * @param[in] handle Pointer to the handle of the scheduled task.
 * @param[in] timeout_s The timeout in seconds.
 * @param[in] task The task to schedule.
 * @param[in] arg Argument to the task.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __schedule_task(osal_scheduler_task_handle_t *handle, uint8_t timeout_s, osal_scheduler_task_t task, void *arg);

/**
 * @brief Start the worker task that runs delayed resets, if not already running.
 *
 * Started on the first delayed reset rather than at init, so a node that never resets pays no RAM
 * for it, and a failure surfaces to the caller that asked for the reset instead of later in a timer.
 *
 * @return ESP_RMAKER_OK if the worker is running, ESP_RMAKER_NO_MEM if it cannot be created.
 */
static esp_rmaker_error_t __reset_worker_start(void);

/**
 * @brief Put a delayed reset in its type's slot, replacing any pending one, and wake the worker.
 *
 * The delay counts from this call, so a repeated request restarts it, and resets of different types
 * wait out their delays side by side rather than one after another.
 *
 * @param[in] type The kind of reset; selects the slot.
 * @param[in] reset_data The reset to run, copied into the slot.
 * @param[in] delay_s The delay in seconds before the reset runs.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __schedule_reset(__system_ctrl_reset_type_t type, const __system_ctrl_reset_data_t *reset_data,
        uint8_t delay_s);

/**
 * @brief Reboot the system (scheduler task).
 * @param[in] arg Unused.
 */
static void __system_ctrl_reboot_task(void *arg);

/**
 * @brief Run each pending reset once its deadline passes (worker task, never returns).
 * @param[in] arg Unused.
 */
static void __system_ctrl_reset_worker_task(void *arg);

/**
 * @brief Take the most overdue reset out of its slot, or report how long until the next one is due.
 *
 * Ties go to the earlier enum value, so resets due at the same time run data, network, factory.
 *
 * Call with g_reset_slots_lock held.
 *
 * @param[out] due The reset to run, set only when one is due.
 * @param[out] wait Ticks until the next pending reset is due, or OSAL_MAX_DELAY if none is pending.
 * @return true if a reset was taken and should run now.
 */
static bool __take_due_reset(__system_ctrl_reset_data_t *due, osal_tick_type_t *wait);

/**
 * @brief Reset the network credentials.
 * @param[in] reset_data Reset data carrying the reboot timeout and the reset function.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __system_ctrl_network_reset(const __system_ctrl_reset_data_t *reset_data);

/**
 * @brief Factory reset the system.
 * @param[in] reset_data Reset data carrying the reboot timeout and the reset function.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __system_ctrl_factory_reset(const __system_ctrl_reset_data_t *reset_data);

/**
 * @brief Run one participant's wipe, logging a failure.
 * @param[in] participant The participant to wipe.
 * @return The wipe's return value.
 */
static esp_rmaker_error_t __run_factory_reset_participant(const esp_rmaker_system_ctrl_factory_reset_participant_t *participant);

/**
 * @brief Wipe every registered factory reset participant except @p skip.
 *
 * Best-effort: a failing wipe does not stop the others. A participant declaring reboots_on_wipe is
 * wiped last, so an earlier restart cannot strand the other wipes.
 *
 * @param[in] skip Wipe function to leave out, or NULL to wipe all of them.
 * @return ESP_RMAKER_OK if every wipe succeeded, otherwise the first error encountered.
 */
static esp_rmaker_error_t __run_factory_reset_participants(esp_rmaker_system_ctrl_factory_reset_wipe_fn_t skip);

/**
 * @brief Run the whole factory reset: wipe the RainMaker Neo data, the participants, then reboot.
 *
 * @param[in] self Wipe function of the participant that started the reset, skipped in the sweep.
 * @param[in] extra_wipe Extra SDK-internal wipe, run after the RainMaker Neo data is cleared and before
 *            the participants, or NULL for none.
 * @param[in] extra_priv Argument passed to @p extra_wipe.
 * @param[in] network_reset_fn Function to reset the network credentials, or NULL to use the registered one.
 * @param[in] reset_reboot_s Reboot timeout in seconds; a negative value leaves the reboot to the caller.
 * @return ESP_RMAKER_OK if every step succeeded, ESP_RMAKER_INVALID_STATE without running anything if a
 *         factory reset is already running or its reboot is scheduled, otherwise the first error encountered.
 */
static esp_rmaker_error_t __factory_reset_run(esp_rmaker_system_ctrl_factory_reset_wipe_fn_t self,
        esp_rmaker_system_ctrl_factory_reset_wipe_fn_t extra_wipe, void *extra_priv,
        esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn, int8_t reset_reboot_s);

/**
 * @brief Notify payload fn for the node_reset notification.
 * Writes a single boolean inside the framework-supplied 'notify' object,
 * producing {"notify":{"node_reset":true}}.
 * @param[in] jptr Pointer to the JSON generator.
 * @param[in] data Unused.
 * @param[in] is_sizing Unused.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __node_reset_payload_fn(json_gen_str_t *jptr, void *data, bool is_sizing);

/**
 * @brief Post RMAKER_EVENT_FACTORY_RESET, as each factory reset entry point does when called.
 */
static void __post_factory_reset_event(void);

/* Private function definitions **********************************************************/

static esp_rmaker_error_t __schedule_task(osal_scheduler_task_handle_t *handle, uint8_t timeout_s, osal_scheduler_task_t task, void *arg)
{
    if (handle == NULL || task == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Initialize the scheduler once so we can call system control functions without initializing RMNG as a whole */
    osal_err_t err;
    err = osal_scheduler_init();
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_INVALID_STATE;
    }

    uint64_t timeout_ms = ((uint64_t) timeout_s) * 1000;
    if (*handle != NULL) {
        err = osal_scheduler_reset_timer(*handle, timeout_ms);
    } else {
        err = osal_scheduler_schedule_task(handle, timeout_ms, task, arg);
    }

    return err == OSAL_ERR_OK ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

static esp_rmaker_error_t __reset_worker_start(void)
{
    int expected = RESET_WORKER_STOPPED;
    while (!atomic_compare_exchange_weak(&g_reset_worker_state, &expected, RESET_WORKER_STARTING)) {
        if (expected == RESET_WORKER_RUNNING) {
            return ESP_RMAKER_OK;
        }
        if (expected == RESET_WORKER_STARTING) {
            /* Another task is creating it; wait until it is running, or failed and ours to retry. */
            osal_task_delay(osal_ticks_from_ms(10));
        }
        expected = RESET_WORKER_STOPPED;
    }

    g_reset_slots_lock = osal_semaphore_create_mutex();
    g_reset_wake = osal_semaphore_create_binary();
    if (g_reset_slots_lock == NULL || g_reset_wake == NULL ||
            osal_task_create(__system_ctrl_reset_worker_task, "rmng_sys_reset", SYSTEM_CTRL_RESET_TASK_STACK_SIZE,
                             NULL, SYSTEM_CTRL_RESET_TASK_PRIORITY, NULL) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to create the reset task");
        if (g_reset_slots_lock != NULL) {
            osal_semaphore_delete(g_reset_slots_lock);
            g_reset_slots_lock = NULL;
        }
        if (g_reset_wake != NULL) {
            osal_semaphore_delete(g_reset_wake);
            g_reset_wake = NULL;
        }
        atomic_store(&g_reset_worker_state, RESET_WORKER_STOPPED);
        return ESP_RMAKER_NO_MEM;
    }

    atomic_store(&g_reset_worker_state, RESET_WORKER_RUNNING);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __schedule_reset(__system_ctrl_reset_type_t type, const __system_ctrl_reset_data_t *reset_data,
        uint8_t delay_s)
{
    esp_rmaker_error_t err = __reset_worker_start();
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    (void) osal_semaphore_take(g_reset_slots_lock, OSAL_MAX_DELAY);
    __system_ctrl_reset_slot_t *slot = &g_reset_slots[type];
    bool replaced = slot->pending;
    slot->data = *reset_data;
    slot->deadline = osal_task_get_tick_count() + osal_ticks_from_ms((uint32_t) delay_s * 1000);
    slot->pending = true;
    (void) osal_semaphore_give(g_reset_slots_lock);

    OSAL_LOGI(TAG, "%s due in %d s%s", g_reset_type_names[type], (int) delay_s, replaced ? " (previous cancelled)" : "");
    (void) osal_semaphore_give(g_reset_wake);
    return ESP_RMAKER_OK;
}

static void __system_ctrl_reboot_task(void *arg)
{
    OSAL_LOGW(TAG, "Executing reboot");
    (void) osal_sysctrl_reboot();
}

static void __system_ctrl_reset_worker_task(void *arg)
{
    (void) arg;
    while (true) {
        __system_ctrl_reset_data_t due;
        osal_tick_type_t wait;

        (void) osal_semaphore_take(g_reset_slots_lock, OSAL_MAX_DELAY);
        bool have_due = __take_due_reset(&due, &wait);
        (void) osal_semaphore_give(g_reset_slots_lock);

        if (have_due) {
            /* Run outside the lock: a reset may take long, and new requests must not block on it. */
            (void) due.run(&due);
        } else {
            /* Sleep until the next deadline, or until a request changes the slots. */
            (void) osal_semaphore_take(g_reset_wake, wait);
        }
    }
}

static bool __take_due_reset(__system_ctrl_reset_data_t *due, osal_tick_type_t *wait)
{
    osal_tick_type_t now = osal_task_get_tick_count();
    __system_ctrl_reset_slot_t *next = NULL;
    int32_t next_left = 0;

    /* Find the earliest deadline. Strict '<' keeps the earlier enum value on a tie. */
    for (int i = 0; i < SYSTEM_CTRL_RESET_TYPE_COUNT; i++) {
        __system_ctrl_reset_slot_t *slot = &g_reset_slots[i];
        if (!slot->pending) {
            continue;
        }
        /* Signed difference: correct across a tick counter wrap. */
        int32_t left = (int32_t) (slot->deadline - now);
        if (next == NULL || left < next_left) {
            next = slot;
            next_left = left;
        }
    }

    if (next == NULL) {
        *wait = OSAL_MAX_DELAY;
        return false;
    }
    if (next_left > 0) {
        *wait = (osal_tick_type_t) next_left;
        return false;
    }
    *due = next->data;
    next->pending = false;
    return true;
}

static esp_rmaker_error_t __system_ctrl_network_reset(const __system_ctrl_reset_data_t *reset_data)
{
    OSAL_LOGW(TAG, "Executing network reset");
    esp_rmaker_error_t err = reset_data->network_reset_fn();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to reset the network credentials");
        return err;
    }

    if (reset_data->reset_reboot_s >= 0) {
        return esp_rmaker_system_ctrl_reboot((uint8_t) reset_data->reset_reboot_s);
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __run_factory_reset_participant(const esp_rmaker_system_ctrl_factory_reset_participant_t *participant)
{
    OSAL_LOGI(TAG, "Wiping reset participant '%s'", participant->name);
    esp_rmaker_error_t err = participant->wipe(participant->priv);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Reset participant '%s' wipe failed: %d", participant->name, (int) err);
    }
    return err;
}

static esp_rmaker_error_t __run_factory_reset_participants(esp_rmaker_system_ctrl_factory_reset_wipe_fn_t skip)
{
    esp_rmaker_error_t first_err = ESP_RMAKER_OK;
    const esp_rmaker_system_ctrl_factory_reset_participant_t *rebooting = NULL;

    for (uint8_t i = 0; i < g_factory_reset_participant_count; i++) {
        const esp_rmaker_system_ctrl_factory_reset_participant_t *participant = &g_factory_reset_participants[i];
        if (participant->wipe == skip) {
            OSAL_LOGI(TAG, "Reset participant '%s' started this reset; skipping its wipe", participant->name);
            continue;
        }
        if (participant->reboots_on_wipe) {
            /* At most one such participant exists; the register call rejects a second. */
            rebooting = participant;
            continue;
        }
        esp_rmaker_error_t err = __run_factory_reset_participant(participant);
        if (first_err == ESP_RMAKER_OK) {
            first_err = err;
        }
    }

    if (rebooting) {
        esp_rmaker_error_t err = __run_factory_reset_participant(rebooting);
        if (first_err == ESP_RMAKER_OK) {
            first_err = err;
        }
        /* Its wipe was documented not to return. Fall through to the caller's reboot. */
        OSAL_LOGE(TAG, "Reset participant '%s' declared reboots_on_wipe but returned", rebooting->name);
    }

    return first_err;
}

static esp_rmaker_error_t __factory_reset_run(esp_rmaker_system_ctrl_factory_reset_wipe_fn_t self,
        esp_rmaker_system_ctrl_factory_reset_wipe_fn_t extra_wipe, void *extra_priv,
        esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn, int8_t reset_reboot_s)
{
    /* Check and set in one step: the cloud, the console and an external stack can all start a reset. */
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_factory_reset_in_progress, &expected, true)) {
        OSAL_LOGI(TAG, "Factory reset already running; ignoring this request");
        return ESP_RMAKER_INVALID_STATE;
    }

    OSAL_LOGW(TAG, "Executing factory reset");

    /* Tell the cloud this node is resetting itself, before wiping credentials/NVS while MQTT
     * and the node/group identity are still valid. Best-effort: proceed regardless of result, and
     * do not let it decide the return value - a node with no cloud connection must still wipe. */
    OSAL_LOGI(TAG, "Notifying cloud of node reset; waiting up to %d ms for acknowledgement",
              (int) SYSTEM_CTRL_NODE_RESET_NOTIFY_TIMEOUT_MS);
    esp_rmaker_notification_t node_reset_notification = {
        .report_payload_fn = __node_reset_payload_fn,
        .data = NULL,
    };
    esp_rmaker_error_t notify_err = esp_rmaker_notify_send_sync(&node_reset_notification, SYSTEM_CTRL_NODE_RESET_NOTIFY_TIMEOUT_MS);
    if (notify_err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Node reset notification not confirmed (%d); proceeding with the reset", (int) notify_err);
    }

    esp_rmaker_error_t first_err = ESP_RMAKER_OK;
    esp_rmaker_system_ctrl_network_reset_fn_t resolved_fn = network_reset_fn ? network_reset_fn : g_network_reset_fn;
    if (resolved_fn == NULL) {
        OSAL_LOGW(TAG, "No network reset function available; skipping the network credential reset");
        first_err = ESP_RMAKER_INVALID_ARG;
    } else {
        esp_rmaker_error_t network_err = resolved_fn();
        if (network_err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to reset the network credentials: %d", (int) network_err);
            first_err = network_err;
        }
    }

    /* Clear the data even if the network reset failed: leaving stale node data behind is worse
     * than a node that still holds its network credentials. */
    esp_rmaker_error_t data_err = esp_rmaker_system_ctrl_clear_data_namespaces();
    if (data_err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to clear the data namespaces: %d", (int) data_err);
        if (first_err == ESP_RMAKER_OK) {
            first_err = data_err;
        }
    }

    /* SDK-internal state the caller wipes with this reset. After the node_reset notification, which
     * may depend on it, and before the participants, one of which may restart the node. */
    if (extra_wipe != NULL) {
        esp_rmaker_error_t extra_err = extra_wipe(extra_priv);
        if (extra_err != ESP_RMAKER_OK && first_err == ESP_RMAKER_OK) {
            first_err = extra_err;
        }
    }

    /* Let components holding persistent state outside the data model erase theirs. */
    esp_rmaker_error_t participant_err = __run_factory_reset_participants(self);
    if (first_err == ESP_RMAKER_OK) {
        first_err = participant_err;
    }

    /* Reboot even if part of the wipe failed: skipping the reboot would strand the node in a
     * half-reset state with nothing to retry from. */
    bool reboot_scheduled = false;
    if (reset_reboot_s >= 0) {
        esp_rmaker_error_t reboot_err = esp_rmaker_system_ctrl_reboot((uint8_t) reset_reboot_s);
        reboot_scheduled = (reboot_err == ESP_RMAKER_OK);
        if (first_err == ESP_RMAKER_OK) {
            first_err = reboot_err;
        }
    }

    /* Nothing is going to restart on our behalf, so the node stays resettable. */
    if (!reboot_scheduled) {
        atomic_store(&g_factory_reset_in_progress, false);
    }

    return first_err;
}

static esp_rmaker_error_t __system_ctrl_factory_reset(const __system_ctrl_reset_data_t *reset_data)
{
    return __factory_reset_run(NULL, NULL, NULL, reset_data->network_reset_fn, reset_data->reset_reboot_s);
}

static esp_rmaker_error_t __clear_nvs_namespace(const char *name_space)
{
    osal_storage_handle_t nvs_handle;
    osal_err_t nvs_err = osal_storage_open(RMAKER_NVS_PART_NAME, name_space, OSAL_STORAGE_OPEN_READWRITE, &nvs_handle);
    if (nvs_err == OSAL_ERR_NVS_NAMESPACE_NOT_FOUND) {
        OSAL_LOGI(TAG, "Namespace '%s' not found; nothing to clear", name_space);
        return ESP_RMAKER_OK;
    }
    if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to open NVS namespace '%s'", name_space);
        return ESP_RMAKER_FAIL;
    }

    nvs_err = osal_storage_erase_all(nvs_handle);
    if (nvs_err == OSAL_ERR_OK) {
        nvs_err = osal_storage_commit(nvs_handle);
    }
    osal_storage_close(nvs_handle);
    if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to erase NVS namespace '%s'", name_space);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

/* Visitor that releases a node's live schedule handles (RAM only). */
static esp_rmaker_error_t __drop_node_schedules_visitor(const esp_rmaker_node_t *node, void *priv)
{
    (void)priv;
    esp_rmaker_schedule_service_unload_node(node);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_system_ctrl_clear_data_namespaces(void)
{
    /* Clear only the RMNG-owned data; leave the rest of the partition (incl. network credentials)
     * intact.
     *
     * Every step is best-effort: report the first error. */
    static const char *const data_namespaces[] = {
        RMAKER_NVS_CHECKSUM_NAMESPACE,
        RMAKER_NVS_LOCAL_CONFIG_NAMESPACE,
#ifdef CONFIG_ESP_RMAKER_NEO_BRIDGE_ENABLE
        /* Per-child thing_name / group_info / ncfg_ver / checksum entries, per-child trigger
         * entries and per-child schedule details. Factory reset must wipe all of these so the
         * next bridge_init starts clean. */
        RMAKER_NVS_BRIDGE_CHILDREN_NAMESPACE,
        RMAKER_NVS_BRIDGE_TRIGGERS_NAMESPACE,
        RMAKER_NVS_BRIDGE_SCHEDS_NAMESPACE,
#endif
    };

    esp_rmaker_error_t first_err = ESP_RMAKER_OK;
    for (size_t i = 0; i < sizeof(data_namespaces) / sizeof(data_namespaces[0]); i++) {
        esp_rmaker_error_t err = __clear_nvs_namespace(data_namespaces[i]);
        if (err != ESP_RMAKER_OK && first_err == ESP_RMAKER_OK) {
            first_err = err;
        }
    }

    /* Clear all stored per-device parameter values (each device persists under its own namespace). */
    const esp_rmaker_node_t *node = esp_rmaker_get_node();
    if (node) {
        esp_rmaker_error_t err = esp_rmaker_node_clear_stored_values(node);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to clear stored device values: %d", (int) err);
            if (first_err == ESP_RMAKER_OK) {
                first_err = err;
            }
        }
    }

    /* Drop every live schedule handle. The stored details JSON was just wiped
     * with the namespaces above, so this only has to clear RAM - but it must
     * happen, otherwise a data reset that does not reboot would leave armed
     * schedules firing actions for state that no longer exists. */
    esp_rmaker_node_for_each(__drop_node_schedules_visitor, NULL);

    return first_err;
}

static esp_rmaker_error_t __system_ctrl_data_reset(const __system_ctrl_reset_data_t *reset_data)
{
    OSAL_LOGW(TAG, "Executing data reset");
    esp_rmaker_error_t err = esp_rmaker_system_ctrl_clear_data_namespaces();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Data reset reported errors, continuing anyway: %d", (int) err);
    }

    /* Reboot even on a partial wipe: see __factory_reset_run. */
    if (reset_data->reset_reboot_s >= 0) {
        esp_rmaker_error_t reboot_err = esp_rmaker_system_ctrl_reboot((uint8_t) reset_data->reset_reboot_s);
        if (err == ESP_RMAKER_OK) {
            err = reboot_err;
        }
    }
    return err;
}

static void __post_factory_reset_event(void)
{
    osal_event_post(RMAKER_COMMON_EVENT, RMAKER_EVENT_FACTORY_RESET, NULL, 0, OSAL_MAX_DELAY);
}

static esp_rmaker_error_t __node_reset_payload_fn(json_gen_str_t *jptr, void *data, bool is_sizing)
{
    (void) data;
    (void) is_sizing;
    if (jptr == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return json_gen_obj_set_bool(jptr, "node_reset", true) == 0 ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

/* Public function definitions **********************************************************/

esp_rmaker_error_t esp_rmaker_system_ctrl_reboot(uint8_t timeout_s)
{
    static osal_scheduler_task_handle_t reboot_task_handle = NULL;
    osal_event_post(RMAKER_COMMON_EVENT, RMAKER_EVENT_REBOOT, &timeout_s, sizeof(uint8_t), OSAL_MAX_DELAY);
    if (timeout_s > 0) {
        OSAL_LOGI(TAG, "Scheduling reboot task for %d s", timeout_s);
        return __schedule_task(&reboot_task_handle, timeout_s, __system_ctrl_reboot_task, NULL);
    } else {
        OSAL_LOGW(TAG, "Executing reboot");
        return osal_sysctrl_reboot() == OSAL_ERR_OK ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
    }
}

esp_rmaker_error_t esp_rmaker_system_ctrl_network_reset(uint8_t reset_s, int8_t reset_reboot_s, esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn)
{
    esp_rmaker_system_ctrl_network_reset_fn_t resolved_fn = network_reset_fn ? network_reset_fn : g_network_reset_fn;
    if (resolved_fn == NULL) {
        OSAL_LOGE(TAG, "No network reset function available (argument and registered fn are both NULL)");
        return ESP_RMAKER_INVALID_ARG;
    }
    const __system_ctrl_reset_data_t reset_data = {
        .reset_reboot_s = reset_reboot_s,
        .network_reset_fn = resolved_fn,
        .run = __system_ctrl_network_reset,
    };

    osal_event_post(RMAKER_COMMON_EVENT, RMAKER_EVENT_NETWORK_RESET, NULL, 0, OSAL_MAX_DELAY);
    if (reset_s > 0) {
        return __schedule_reset(SYSTEM_CTRL_RESET_NETWORK, &reset_data, reset_s);
    } else {
        return __system_ctrl_network_reset(&reset_data);
    }
}

esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset(uint8_t reset_s, int8_t reset_reboot_s, esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn)
{
    esp_rmaker_system_ctrl_network_reset_fn_t resolved_fn = network_reset_fn ? network_reset_fn : g_network_reset_fn;
    if (resolved_fn == NULL) {
        OSAL_LOGE(TAG, "No network reset function available (argument and registered fn are both NULL)");
        return ESP_RMAKER_INVALID_ARG;
    }
    const __system_ctrl_reset_data_t reset_data = {
        .reset_reboot_s = reset_reboot_s,
        .network_reset_fn = resolved_fn,
        .run = __system_ctrl_factory_reset,
    };

    __post_factory_reset_event();
    if (reset_s > 0) {
        return __schedule_reset(SYSTEM_CTRL_RESET_FACTORY, &reset_data, reset_s);
    } else {
        return __system_ctrl_factory_reset(&reset_data);
    }
}

esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset_from_participant(esp_rmaker_system_ctrl_factory_reset_wipe_fn_t self,
        esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn, int8_t reset_reboot_s)
{
    __post_factory_reset_event();
    return __factory_reset_run(self, NULL, NULL, network_reset_fn, reset_reboot_s);
}

esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset_with_wipe(esp_rmaker_system_ctrl_factory_reset_wipe_fn_t extra_wipe,
        void *extra_priv, int8_t reset_reboot_s)
{
    __post_factory_reset_event();
    return __factory_reset_run(NULL, extra_wipe, extra_priv, NULL, reset_reboot_s);
}

esp_rmaker_error_t esp_rmaker_system_ctrl_register_network_reset_fn(esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn)
{
    g_network_reset_fn = network_reset_fn;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset_participant_register(const esp_rmaker_system_ctrl_factory_reset_participant_t *participant)
{
    if (participant == NULL || participant->wipe == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    int existing = -1;
    for (uint8_t i = 0; i < g_factory_reset_participant_count; i++) {
        if (g_factory_reset_participants[i].wipe == participant->wipe) {
            existing = (int) i;
        } else if (participant->reboots_on_wipe && g_factory_reset_participants[i].reboots_on_wipe) {
            OSAL_LOGE(TAG, "Reset participant '%s' already claimed reboots_on_wipe", g_factory_reset_participants[i].name);
            return ESP_RMAKER_INVALID_STATE;
        }
    }

    if (existing >= 0) {
        g_factory_reset_participants[existing] = *participant;
        return ESP_RMAKER_OK;
    }
    if (g_factory_reset_participant_count >= SYSTEM_CTRL_FACTORY_RESET_PARTICIPANT_MAX_COUNT) {
        return ESP_RMAKER_NO_MEM;
    }

    g_factory_reset_participants[g_factory_reset_participant_count++] = *participant;
    OSAL_LOGI(TAG, "Registered reset participant '%s'", participant->name);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset_participant_unregister(esp_rmaker_system_ctrl_factory_reset_wipe_fn_t wipe)
{
    if (wipe == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    for (uint8_t i = 0; i < g_factory_reset_participant_count; i++) {
        if (g_factory_reset_participants[i].wipe != wipe) {
            continue;
        }
        /* Shift the rest down: the wipe order follows registration order. */
        for (uint8_t j = i; j + 1 < g_factory_reset_participant_count; j++) {
            g_factory_reset_participants[j] = g_factory_reset_participants[j + 1];
        }
        g_factory_reset_participant_count--;
        memset(&g_factory_reset_participants[g_factory_reset_participant_count], 0, sizeof(g_factory_reset_participants[0]));
        return ESP_RMAKER_OK;
    }

    return ESP_RMAKER_NOT_FOUND;
}

esp_rmaker_error_t esp_rmaker_system_ctrl_data_reset(uint8_t reset_s, int8_t reset_reboot_s)
{
    const __system_ctrl_reset_data_t reset_data = {
        .reset_reboot_s = reset_reboot_s,
        .network_reset_fn = NULL,
        .run = __system_ctrl_data_reset,
    };

    if (reset_s > 0) {
        return __schedule_reset(SYSTEM_CTRL_RESET_DATA, &reset_data, reset_s);
    } else {
        return __system_ctrl_data_reset(&reset_data);
    }
}
