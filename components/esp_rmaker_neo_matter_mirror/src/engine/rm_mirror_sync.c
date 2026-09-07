/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_sync.c
 * @brief Bidirectional state sync between the canonical RainMaker Neo param store and
 *        the Matter attribute store, with echo suppression.
 *
 * Threading model:
 *  - Outbound: the param update observer runs in the updating task's context
 *    (no RainMaker Neo locks held); it updates binding state under the engine lock and
 *    hands the value to the port, which schedules onto the Matter thread.
 *  - Inbound: the port calls rm_mirror_engine_handle_matter_update() from the
 *    Matter thread; the value is parked on the binding (replace semantics, which
 *    coalesces transition step storms) and the injection task delivers it to the
 *    device write callback, so application/hardware code never runs on the Matter
 *    thread.
 *  - Binding table: every entry point uses it between __table_enter/exit(); adds and
 *    removals wait until no task is between them.
 *
 * Echo suppression is value-based in the destination domain plus a single
 * expected-echo slot per binding. A burst of distinct outbound updates faster
 * than the Matter round-trip can cause at most one extra correcting bounce
 * (never a loop): the stale echo is injected, re-reported, and converges.
 */

#include <string.h>
#include <stdlib.h>

#include "esp_rmaker_data_model_introspect.h"
#include "esp_rmaker_matter_standard_params.h"
#include "esp_rmaker_val.h"

#include "rm_mirror_engine.h"
#include "rm_mirror_internal.h"

#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_semaphore.h"
#include "osal_task.h"
#include "osal_ticks.h"

static const char *TAG = "rm_mirror_sync";

#ifndef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_TASK_STACK
#define CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_TASK_STACK 4096
#endif

#ifndef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_TASK_PRIORITY
#define CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_TASK_PRIORITY 5
#endif

#ifndef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_RENDER_TICK_MS
#define CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_RENDER_TICK_MS 20
#endif

/* A settle tick plus the write callback it may be running */
#define RM_MIRROR_TASK_EXIT_TIMEOUT_MS (CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_RENDER_TICK_MS + 1000)

_Static_assert(RM_MIRROR_COMPOSITE_MAX_ATTRS <= 4, "echo flags exceed the flags byte");

static struct {
    const esp_rmaker_node_t *node;
    const rm_mirror_port_ops_t *port;
    rm_mirror_binding_t **bindings;  /* realloc'd array of pointers; the bindings never move */
    size_t n_bindings;
    uint32_t table_users;  /* tasks between __table_enter/exit(); the table changes only at zero */
    osal_semaphore_handle_t lock;  /* mutex over binding sync state and table_users */
    osal_semaphore_handle_t membership;  /* serializes device adds and removals */
    osal_semaphore_handle_t wake;  /* injection task wakeup */
    osal_semaphore_handle_t exited;  /* injection task has left the loop */
    osal_task_handle_t task;
    bool running;
    bool initialized;
    /* enable() has returned: devices can be added and removed */
    bool devices_open;
    /* Inbound changes only count once the Matter stack is up and the canonical values have been
     * re-asserted: everything before that is tree construction or a persisted attribute being
     * restored, never user intent. */
    bool inbound_ready;
    /* Endpoint of a runtime add while it is still disabled, or 0: no controller can write it, so inbound is dropped */
    uint16_t dormant_endpoint;
} __engine;

/* Binding accessors ***********************************************************/

/** A binding's tail, resolved once (regions documented on rm_mirror_binding_t). */
typedef struct {
    rm_mirror_slot_t *params;   /* param handles */
    rm_mirror_slot_t *last;     /* Matter-side value as last seen */
    rm_mirror_slot_t *echo;     /* outbound write in flight, per F_ECHO(i) */
    rm_mirror_slot_t *committed;  /* value the mirror has committed to the app: delivered, or queued */
    uint8_t n_params;
    uint8_t n_slots;
} rm_mirror_tail_t;

/* Deliberately not inline: one copy of the arithmetic, called once per function
 * that touches slots, after which every access is a local load. */
static rm_mirror_tail_t __b_tail(const rm_mirror_binding_t *b)
{
    const bool composite = (b->kind == RM_MIRROR_BINDING_COMPOSITE);
    rm_mirror_tail_t t = {
        .n_params = composite ? b->comp->n_params : 1,
        .n_slots  = composite ? b->comp->n_attributes : 1,
        .params   = (rm_mirror_slot_t *)b->slots,
    };
    t.last    = t.params + t.n_params;
    t.echo    = t.last + t.n_slots;
    t.committed = t.echo + t.n_slots;
    return t;
}

static inline bool rm_mirror_binding_is_str(const rm_mirror_binding_t *b)
{
    return b->kind == RM_MIRROR_BINDING_STR_ATTR;
}

/** Type of the values in `last`/`echo`/`committed`. */
static esp_rmaker_val_type_t __b_slot_type(const rm_mirror_binding_t *b)
{
    return rm_mirror_binding_is_str(b) ? RMAKER_VAL_TYPE_STRING : RMAKER_VAL_TYPE_INTEGER;
}

/** The device behind a binding: the parent of its first bound param. */
static const esp_rmaker_device_t *__b_device(const rm_mirror_binding_t *b)
{
    return esp_rmaker_param_get_device(b->slots[0].param);
}

/** A param's own value type, read live - it is fixed at param creation. */
static esp_rmaker_val_type_t __param_type(const esp_rmaker_param_t *param)
{
    esp_rmaker_param_val_t *val = esp_rmaker_param_get_val((esp_rmaker_param_t *)param);
    return val ? val->type : RMAKER_VAL_TYPE_INVALID;
}

/* Slot helpers ****************************************************************/

static inline esp_rmaker_param_val_t __slot_val(const rm_mirror_binding_t *b, rm_mirror_slot_t s)
{
    return (esp_rmaker_param_val_t) {
        .type = __b_slot_type(b), .val = s.val
    };
}

/** Whether a slot equals @p val. Callers must have checked the slot's valid flag. */
static bool __slot_eq(const rm_mirror_binding_t *b, rm_mirror_slot_t s,
                      const esp_rmaker_param_val_t *val)
{
    esp_rmaker_param_val_t held = __slot_val(b, s);
    return esp_rmaker_val_compare(&held, val, RMAKER_VAL_COMPARE_EQ) == ESP_RMAKER_OK;
}

/** Release a slot's value; esp_rmaker_val_free leaves the pointer dangling, so clear it. */
static void __slot_clear(const rm_mirror_binding_t *b, rm_mirror_slot_t *s)
{
    esp_rmaker_param_val_t held = __slot_val(b, *s);
    esp_rmaker_val_free(&held);
    s->val.s = NULL;
}

/** Replace a slot's value, freeing any string it held. */
static esp_rmaker_error_t __slot_set(const rm_mirror_binding_t *b, rm_mirror_slot_t *s,
                                     const esp_rmaker_param_val_t *val)
{
    __slot_clear(b, s);
    esp_rmaker_param_val_t copy;
    esp_rmaker_error_t err = esp_rmaker_val_copy(val, &copy);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    s->val = copy.val;
    return ESP_RMAKER_OK;
}

/** Set a slot, keeping @p flag consistent with whether it ended up holding a value. */
static void __slot_store(rm_mirror_binding_t *b, rm_mirror_slot_t *s, uint8_t flag,
                         const esp_rmaker_param_val_t *val)
{
    if (__slot_set(b, s, val) == ESP_RMAKER_OK) {
        b->flags |= flag;
    } else {
        b->flags &= ~flag;
    }
}

/**
 * @brief Copy @p src into @p dst truncated to the width the binding's sink declares
 *        (the capability's transform max_len, capped at the widest string a binding carries).
 */
static void __str_truncate(const rm_mirror_binding_t *b, char *dst, const char *src)
{
    size_t max_len = (size_t)b->cap->xform.u.scalar;
    if (max_len == 0 || max_len > RM_MIRROR_STR_MAX_LEN) {
        max_len = RM_MIRROR_STR_MAX_LEN;
    }
    size_t len = src ? strlen(src) : 0;
    if (len > max_len) {
        len = max_len;
    }
    if (src) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

esp_rmaker_error_t rm_mirror_port_push(const rm_mirror_port_ops_t *port, uint16_t endpoint_id,
                                       const rm_mirror_capability_t *cap, int32_t matter_val, bool init)
{
    if (cap->report_as) {
        /* Nothing to write into during lowering: these clusters keep the value behind their own notify
         * path, which needs an enabled endpoint. The reseed pushes it. */
        if (init) {
            return ESP_RMAKER_OK;
        }
        if (!port->report_as) {
            OSAL_LOGE(TAG, "No report handler for kind %s; value dropped.", cap->report_as);
            return ESP_RMAKER_NOT_SUPPORTED;
        }
        return port->report_as(endpoint_id, cap->cluster_id, cap->attribute_id, cap->report_as,
                               matter_val);
    }
    return rm_mirror_write_capability_attr(init ? port->attribute_set : port->attribute_update,
                                           endpoint_id, cap, matter_val);
}

esp_rmaker_error_t rm_mirror_write_capability_attr(rm_mirror_attr_write_fn write, uint16_t endpoint_id,
        const rm_mirror_capability_t *cap, int32_t matter_val)
{
    esp_rmaker_error_t err = write(endpoint_id, cap->cluster_id, cap->attribute_id, matter_val,
                                   cap->value_type);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    for (size_t i = 0; i < cap->n_shadow_attrs; i++) {
        write(endpoint_id, cap->cluster_id, cap->shadow_attrs[i].attribute_id, matter_val,
              cap->shadow_attrs[i].value_type);
    }
    return ESP_RMAKER_OK;
}

/* Binding table ***************************************************************/

static void __state_lock(void)
{
    osal_semaphore_take(__engine.lock, OSAL_MAX_DELAY);
}

static void __state_unlock(void)
{
    osal_semaphore_give(__engine.lock);
}

/** Start using the table: it and its bindings stay in place until __table_exit(). */
static void __table_enter(void)
{
    if (__engine.lock) {
        __state_lock();
        __engine.table_users++;
        __state_unlock();
    }
}

static void __table_exit(void)
{
    if (__engine.lock) {
        __state_lock();
        __engine.table_users--;
        __state_unlock();
    }
}

/**
 * @brief Take the lock once no task uses the table, so it may change.
 *
 * @return ESP_RMAKER_INVALID_STATE before init, ESP_RMAKER_TIMEOUT when a task stays in, e.g. the
 *         caller itself, or the lock stays held.
 */
static esp_rmaker_error_t __table_lock(void)
{
    if (!__engine.lock) {
        return ESP_RMAKER_INVALID_STATE;
    }
    const osal_tick_type_t start = osal_task_get_tick_count();
    const osal_tick_type_t timeout = osal_ticks_from_ms(RM_MIRROR_TASK_EXIT_TIMEOUT_MS);
    for (;;) {
        const osal_tick_type_t elapsed = osal_task_get_tick_count() - start;
        if (elapsed >= timeout || osal_semaphore_take(__engine.lock, timeout - elapsed) != OSAL_ERR_OK) {
            return ESP_RMAKER_TIMEOUT;
        }
        if (__engine.table_users == 0) {
            return ESP_RMAKER_OK;
        }
        __state_unlock();
        osal_task_delay(1);
    }
}

/** Release the lock that __table_lock() took. */
static void __table_unlock(void)
{
    __state_unlock();
}

static void __binding_free(rm_mirror_binding_t *b)
{
    if (rm_mirror_binding_is_str(b)) {
        rm_mirror_tail_t t = __b_tail(b);
        __slot_clear(b, &t.last[0]);
        __slot_clear(b, &t.echo[0]);
        __slot_clear(b, &t.committed[0]);
    }
    free(b);
}

/** Append a seeded binding to the table, or free it on failure. */
static esp_rmaker_error_t __binding_publish(rm_mirror_binding_t *b)
{
    esp_rmaker_error_t err = __table_lock();
    if (err != ESP_RMAKER_OK) {
        __binding_free(b);
        return err;
    }
    rm_mirror_binding_t **grown = OSAL_REALLOC_EXTRAM(__engine.bindings,
                                  (__engine.n_bindings + 1) * sizeof(rm_mirror_binding_t *));
    if (grown) {
        __engine.bindings = grown;
        __engine.bindings[__engine.n_bindings++] = b;
    }
    __table_unlock();
    if (!grown) {
        __binding_free(b);
        return ESP_RMAKER_NO_MEM;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t rm_mirror_sync_remove_endpoint(uint16_t endpoint_id)
{
    esp_rmaker_error_t err = __table_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    const esp_rmaker_device_t *device = NULL;
    for (size_t i = 0; !device && i < __engine.n_bindings; i++) {
        if (__engine.bindings[i]->endpoint_id == endpoint_id) {
            device = __b_device(__engine.bindings[i]);
        }
    }
    size_t kept = 0;
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        rm_mirror_binding_t *b = __engine.bindings[i];
        /* The node-scoped name binding sits on endpoint 0 but holds the device's name param */
        if (b->endpoint_id == endpoint_id || (rm_mirror_binding_is_str(b) && __b_device(b) == device)) {
            __binding_free(b);
        } else {
            __engine.bindings[kept++] = b;
        }
    }
    err = (kept == __engine.n_bindings) ? ESP_RMAKER_NOT_FOUND : ESP_RMAKER_OK;
    __engine.n_bindings = kept;
    __table_unlock();
    return err;
}

/**
 * @brief Allocate a binding with its tail. Seed it, then add it with __binding_publish().
 *
 * @param[in] table_entry The capability (scalar/string kind) or composite this binding follows.
 */
static rm_mirror_binding_t *__binding_new(rm_mirror_binding_kind_t kind, const void *table_entry,
        uint16_t endpoint_id, const esp_rmaker_param_t *const *params)
{
    const bool composite = (kind == RM_MIRROR_BINDING_COMPOSITE);
    const size_t n_params = composite ? ((const rm_mirror_composite_t *)table_entry)->n_params : 1;
    const size_t n_slots = composite ? ((const rm_mirror_composite_t *)table_entry)->n_attributes : 1;

    rm_mirror_binding_t *b = OSAL_CALLOC_EXTRAM(1, sizeof(*b) +
                             (2 * n_params + 2 * n_slots) * sizeof(rm_mirror_slot_t));
    if (!b) {
        return NULL;
    }
    b->kind = kind;
    b->endpoint_id = endpoint_id;
    b->cap = (const rm_mirror_capability_t *)table_entry;
    for (size_t i = 0; i < n_params; i++) {
        b->slots[i].param = params[i];
    }

    return b;
}

esp_rmaker_error_t rm_mirror_sync_add_binding(const esp_rmaker_param_t *param,
        const rm_mirror_capability_t *cap, uint16_t endpoint_id, int32_t seeded_matter_val)
{
    if (!param || !cap) {
        return ESP_RMAKER_INVALID_ARG;
    }
    rm_mirror_binding_t *b = __binding_new(RM_MIRROR_BINDING_SCALAR, cap, endpoint_id, &param);
    if (!b) {
        return ESP_RMAKER_NO_MEM;
    }
    rm_mirror_tail_t t = __b_tail(b);
    t.last[0].val.i = seeded_matter_val;
    /* Only an attribute-backed capability actually got seeded during lowering (see
     * rm_mirror_port_push); the others hold no value yet, so the first update must push. */
    if (cap->report_as == NULL) {
        b->flags |= RM_MIRROR_F_LAST_VALID;
    }
    return __binding_publish(b);
}

static rm_mirror_binding_t *__find_by_param(const esp_rmaker_param_t *param)
{
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        rm_mirror_binding_t *b = __engine.bindings[i];
        if (b->kind == RM_MIRROR_BINDING_SCALAR && b->slots[0].param == param) {
            return b;
        }
    }
    return NULL;
}

static rm_mirror_binding_t *__find_str_by_param(const esp_rmaker_param_t *param)
{
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        rm_mirror_binding_t *b = __engine.bindings[i];
        if (rm_mirror_binding_is_str(b) && b->slots[0].param == param) {
            return b;
        }
    }
    return NULL;
}

esp_rmaker_error_t rm_mirror_sync_add_composite_binding(const rm_mirror_composite_t *comp,
        const esp_rmaker_param_t *const *params, uint16_t endpoint_id, const int32_t *seeded_matter)
{
    if (!comp || comp->n_params > RM_MIRROR_COMPOSITE_MAX_PARAMS ||
            comp->n_attributes > RM_MIRROR_COMPOSITE_MAX_ATTRS) {
        return ESP_RMAKER_INVALID_ARG;
    }
    rm_mirror_binding_t *b = __binding_new(RM_MIRROR_BINDING_COMPOSITE, comp, endpoint_id, params);
    if (!b) {
        return ESP_RMAKER_NO_MEM;
    }
    rm_mirror_tail_t t = __b_tail(b);
    for (size_t i = 0; i < comp->n_attributes; i++) {
        t.last[i].val.i = seeded_matter[i];
    }
    return __binding_publish(b);
}

static rm_mirror_binding_t *__find_by_path(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id)
{
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        rm_mirror_binding_t *b = __engine.bindings[i];
        if (b->kind == RM_MIRROR_BINDING_SCALAR && b->endpoint_id == endpoint_id &&
                b->cap->cluster_id == cluster_id && b->cap->attribute_id == attribute_id) {
            return b;
        }
    }
    return NULL;
}

esp_rmaker_error_t rm_mirror_sync_add_str_binding(const esp_rmaker_param_t *param,
        const rm_mirror_capability_t *cap, uint16_t endpoint_id)
{
    if (!param || !cap) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_param_val_t *val = esp_rmaker_param_get_val((esp_rmaker_param_t *)param);
    if (!val || val->type != RMAKER_VAL_TYPE_STRING) {
        return ESP_RMAKER_INVALID_ARG;
    }
    rm_mirror_binding_t *b = __binding_new(RM_MIRROR_BINDING_STR_ATTR, cap, endpoint_id, &param);
    return b ? __binding_publish(b) : ESP_RMAKER_NO_MEM;
}

/**
 * @brief Push a value at the binding's sink.
 */
static void __str_push(const rm_mirror_binding_t *b, const char *value)
{
    if (__engine.port->attribute_write_str) {
        __engine.port->attribute_write_str(b->endpoint_id, b->cap->cluster_id,
                                           b->cap->attribute_id, value);
    }
}

/* Outbound: canonical param store -> Matter ***********************************/

/**
 * @brief Outbound handler for the string binding on @p param: push the value at
 *        its sink, truncated to the width that sink declares.
 *
 * @return true when @p param is string-bound, i.e. the update belongs to this
 *         path rather than the scalar one.
 */
static bool __str_handle_param_update(const esp_rmaker_param_t *param,
                                      const esp_rmaker_param_val_t *val)
{
    rm_mirror_binding_t *b = __find_str_by_param(param);
    if (!b) {
        return false;
    }
    if (val->type != RMAKER_VAL_TYPE_STRING) {
        return true;
    }
    rm_mirror_tail_t t = __b_tail(b);
    char truncated[RM_MIRROR_STR_MAX_LEN + 1];
    __str_truncate(b, truncated, val->val.s);
    esp_rmaker_param_val_t tv = esp_rmaker_str(truncated);

    bool push = false;
    __state_lock();
    if (!(b->flags & RM_MIRROR_F_LAST_VALID) || !__slot_eq(b, t.last[0], &tv)) {
        __slot_store(b, &t.last[0], RM_MIRROR_F_LAST_VALID, &tv);
        __slot_store(b, &t.echo[0], RM_MIRROR_F_ECHO(0), &tv);
        push = true;
    }
    __state_unlock();

    if (push) {
        __str_push(b, truncated);
    }
    return true;
}

/**
 * @brief Push a value to the Matter side: invoke the capability's write_as command when it has one,
 *        so the cluster runs its own logic (OnOff coupling, ColorMode, RemainingTime) instead of
 *        leaving those derived attributes stale. Falls back to writing the attribute.
 */
static void __push_to_matter(const rm_mirror_binding_t *b, int32_t matter_val)
{
    const rm_mirror_write_cmd_t *cmd = rm_mirror_table_find_write_cmd(b->cap, matter_val);
    if (cmd && __engine.port->command_invoke
            && __engine.port->command_invoke(b->endpoint_id, b->cap->cluster_id, cmd,
                    matter_val) == ESP_RMAKER_OK) {
        return;
    }
    rm_mirror_port_push(__engine.port, b->endpoint_id, b->cap, matter_val, false);
}

/**
 * @brief Outbound handler for composites containing @p param: recompute the
 *        derived attributes from the member params' current values and push.
 *
 * Composites push attribute writes, not write_as commands: their M transformed
 * values do not fit the single-value command payload contract, and attribute
 * reports do not disturb server-managed state (e.g. ColorMode).
 */
static void __composite_handle_param_update(const esp_rmaker_param_t *param)
{
    for (size_t c = 0; c < __engine.n_bindings; c++) {
        rm_mirror_binding_t *b = __engine.bindings[c];
        if (b->kind != RM_MIRROR_BINDING_COMPOSITE) {
            continue;
        }
        rm_mirror_tail_t t = __b_tail(b);
        bool member = false;
        for (size_t i = 0; i < t.n_params; i++) {
            if (t.params[i].param == param) {
                member = true;
                break;
            }
        }
        if (!member) {
            continue;
        }

        /* Member params' current values (the store is already updated when the
         * observer runs) */
        int32_t rmng_vals[RM_MIRROR_COMPOSITE_MAX_PARAMS] = {0};
        bool readable = true;
        for (size_t i = 0; i < t.n_params; i++) {
            if (rm_mirror_param_val_as_i32(t.params[i].param, &rmng_vals[i]) != ESP_RMAKER_OK) {
                readable = false;
                break;
            }
        }
        if (!readable) {
            continue;
        }

        bool push = false;
        int32_t matter_vals[RM_MIRROR_COMPOSITE_MAX_ATTRS] = {0};
        __state_lock();
        /* RainMaker Neo-domain suppression: these values are our own injection's
         * observer echo. The conversion round-trip is not exactly
         * value-stable, so suppressing here (not in the matter domain) is
         * what guarantees termination - and keeps the controller's exact
         * X/Y on the wire instead of a re-quantized pair. */
        bool is_injection_echo = (b->flags & RM_MIRROR_F_COMMITTED_VALID) != 0;
        for (size_t i = 0; is_injection_echo && i < t.n_params; i++) {
            is_injection_echo = (t.committed[i].val.i == rmng_vals[i]);
        }
        if (!is_injection_echo) {
            b->flags &= ~RM_MIRROR_F_COMMITTED_VALID; /* genuine device-side change */
            rm_mirror_composite_to_matter(b->comp, rmng_vals, matter_vals);
            for (size_t i = 0; i < t.n_slots; i++) {
                if (matter_vals[i] != t.last[i].val.i) {
                    push = true;
                }
            }
            if (push) {
                for (size_t i = 0; i < t.n_slots; i++) {
                    t.last[i].val.i = matter_vals[i];
                    t.echo[i].val.i = matter_vals[i];
                    b->flags |= RM_MIRROR_F_ECHO(i);
                }
            }
        }
        __state_unlock();

        if (push) {
            for (size_t i = 0; i < t.n_slots; i++) {
                __engine.port->attribute_update(b->endpoint_id, b->comp->cluster_id,
                                                b->comp->attribute_ids[i], matter_vals[i],
                                                b->comp->value_types[i]);
            }
        }
    }
}

static void __handle_param_update(const esp_rmaker_param_t *param, const esp_rmaker_param_val_t *val)
{
    if (__str_handle_param_update(param, val)) {
        return;
    }
    /* Composites read member values from the store themselves; run this
     * independently of the scalar binding path (and before its early
     * returns) so membership alone is sufficient. */
    __composite_handle_param_update(param);

    rm_mirror_binding_t *b = __find_by_param(param);
    if (!b) {
        return; /* unmapped param, or update before mirror init (values are seeded at init) */
    }
    rm_mirror_tail_t t = __b_tail(b);

    int32_t matter_val;
    int32_t rmng_val = 0;
    bool rmng_domain = true; /* false for floats, which no many-to-one transform carries */
    if (val->type == RMAKER_VAL_TYPE_BOOLEAN) {
        rmng_val = val->val.b ? 1 : 0;
        matter_val = rm_mirror_xform_to_matter(&b->cap->xform, rmng_val);
    } else if (val->type == RMAKER_VAL_TYPE_INTEGER) {
        rmng_val = val->val.i;
        matter_val = rm_mirror_xform_to_matter(&b->cap->xform, rmng_val);
    } else if (val->type == RMAKER_VAL_TYPE_FLOAT) {
        matter_val = rm_mirror_xform_to_matter_f(&b->cap->xform, val->val.f);
        rmng_domain = false;
    } else {
        return;
    }

    bool push = false;
    __state_lock();
    const bool last_valid = (b->flags & RM_MIRROR_F_LAST_VALID) != 0;
    if ((!last_valid || t.last[0].val.i != matter_val) &&
            /* RainMaker Neo-domain suppression for many-to-one transforms: skip when the
             * Matter side already represents this RainMaker Neo value through an alias
             * (e.g. ColorMode 1/XY vs light-mode HSV, whose canonical mapping
             * is 0) - pushing would flap the Matter value for no state change. */
            !(rmng_domain && last_valid &&
              rm_mirror_xform_to_rmng(&b->cap->xform, t.last[0].val.i) == rmng_val)) {
        t.last[0].val.i = matter_val;
        t.echo[0].val.i = matter_val;
        b->flags |= RM_MIRROR_F_LAST_VALID | RM_MIRROR_F_ECHO(0);
        /* Genuine device-side change: the injection basis no longer describes what the app
         * holds, and keeping it would let it judge the next inbound value a repeat. */
        b->flags &= ~RM_MIRROR_F_COMMITTED_VALID;
        push = true;
    }
    __state_unlock();

    if (push) {
        __push_to_matter(b, matter_val);
    }
}

void rm_mirror_sync_handle_param_update(const esp_rmaker_param_t *param, const esp_rmaker_param_val_t *val)
{
    __table_enter();
    __handle_param_update(param, val);
    __table_exit();
}

static void __param_update_observer(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                                    const esp_rmaker_param_val_t *val, void *priv)
{
    (void)device;
    (void)priv;
    rm_mirror_sync_handle_param_update(param, val);
}

/* Inbound: Matter -> canonical param store ************************************/

/**
 * @brief Record an inbound Matter value on slot @p idx: consume our own echo, or
 *        accept it as the new Matter-side state.
 *
 * @return true when the value is a genuine inbound change rather than our echo.
 *         Called with the engine lock held.
 */
static bool __inbound_slot(rm_mirror_binding_t *b, const rm_mirror_tail_t *t, size_t idx,
                           const esp_rmaker_param_val_t *val)
{
    const bool echo = (b->flags & RM_MIRROR_F_ECHO(idx)) && __slot_eq(b, t->echo[idx], val);
    if (echo) {
        b->flags &= ~RM_MIRROR_F_ECHO(idx);
    }
    __slot_store(b, &t->last[idx], RM_MIRROR_F_LAST_VALID, val);
    return !echo;
}

/**
 * @brief Inbound handler for composite-owned attributes. Returns true when the
 *        (endpoint, cluster, attribute) path belongs to a composite.
 */
static bool __composite_handle_matter_update(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t value)
{
    for (size_t c = 0; c < __engine.n_bindings; c++) {
        rm_mirror_binding_t *b = __engine.bindings[c];
        if (b->kind != RM_MIRROR_BINDING_COMPOSITE || b->endpoint_id != endpoint_id ||
                b->comp->cluster_id != cluster_id) {
            continue;
        }
        rm_mirror_tail_t t = __b_tail(b);
        size_t attr_idx = t.n_slots;
        for (size_t i = 0; i < t.n_slots; i++) {
            if (b->comp->attribute_ids[i] == attribute_id) {
                attr_idx = i;
                break;
            }
        }
        if (attr_idx == t.n_slots) {
            continue;
        }

        esp_rmaker_param_val_t mv = esp_rmaker_int(value);
        bool wake = false;
        __state_lock();
        if (__inbound_slot(b, &t, attr_idx, &mv)) {
            /* Replace semantics: attribute pairs written back-to-back (X then
             * Y) and step storms coalesce into one pending conversion over
             * the latest values */
            b->flags |= RM_MIRROR_F_PENDING;
            wake = true;
        }
        __state_unlock();

        if (wake && __engine.wake) {
            osal_semaphore_give(__engine.wake);
        }
        return true;
    }
    return false;
}

/** Whether an inbound value on @p endpoint_id can come from a controller. */
static bool __inbound_open(uint16_t endpoint_id)
{
    return __engine.inbound_ready && (__engine.dormant_endpoint == 0 || endpoint_id != __engine.dormant_endpoint);
}

static void __handle_matter_update(uint16_t endpoint_id, uint32_t cluster_id,
                                   uint32_t attribute_id, int32_t value)
{
    if (!__inbound_open(endpoint_id)) {
        return;
    }
    rm_mirror_binding_t *b = __find_by_path(endpoint_id, cluster_id, attribute_id);
    if (!b) {
        __composite_handle_matter_update(endpoint_id, cluster_id, attribute_id, value);
        return;
    }
    rm_mirror_tail_t t = __b_tail(b);

    /* Same-value check in the Matter domain: comparing there rather than after the inverse
     * transform avoids a lossy round trip deciding that a value differs when it does not. */
    int32_t param_val = 0;
    const bool param_readable =
        (rm_mirror_param_to_matter(t.params[0].param, b->cap, &param_val) == ESP_RMAKER_OK);

    esp_rmaker_param_val_t mv = esp_rmaker_int(value);
    bool wake = false;
    __state_lock();
    /* Against what the app was last handed (`committed`), not the param: the param only catches up
     * once that injection's write callback returns, so parked and in-flight values lag it. */
    const bool same = (b->flags & RM_MIRROR_F_COMMITTED_VALID)
                      ? (t.committed[0].val.i == value)
                      : (param_readable && param_val == value);
    if (__inbound_slot(b, &t, 0, &mv) && !same) {
        t.committed[0].val.i = value;
        b->flags |= RM_MIRROR_F_PENDING | RM_MIRROR_F_COMMITTED_VALID;
        wake = true;
    }
    __state_unlock();

    if (wake && __engine.wake) {
        osal_semaphore_give(__engine.wake);
    }
}

void rm_mirror_engine_handle_matter_update(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t value)
{
    __table_enter();
    __handle_matter_update(endpoint_id, cluster_id, attribute_id, value);
    __table_exit();
}

/**
 * @brief Common inbound handling for a string binding: consume our own echo,
 *        else park a genuine rename for injection.
 */
static void __str_inbound(rm_mirror_binding_t *b, const char *value)
{
    rm_mirror_tail_t t = __b_tail(b);
    char truncated[RM_MIRROR_STR_MAX_LEN + 1];
    __str_truncate(b, truncated, value);
    esp_rmaker_param_val_t tv = esp_rmaker_str(truncated);

    /* Same-value check against the param truncated to this sink's width, so
     * a longer param whose visible prefix already matches is not a rename. */
    esp_rmaker_param_val_t *current = esp_rmaker_param_get_val((esp_rmaker_param_t *)t.params[0].param);
    char current_trunc[RM_MIRROR_STR_MAX_LEN + 1] = {0};
    if (current && current->type == RMAKER_VAL_TYPE_STRING) {
        __str_truncate(b, current_trunc, current->val.s);
    }
    const bool same = (strcmp(current_trunc, truncated) == 0);

    bool wake = false;
    __state_lock();
    if (__inbound_slot(b, &t, 0, &tv) && !same) {
        __slot_store(b, &t.committed[0], RM_MIRROR_F_PENDING, &tv);
        wake = (b->flags & RM_MIRROR_F_PENDING) != 0;
    }
    __state_unlock();

    if (wake && __engine.wake) {
        osal_semaphore_give(__engine.wake);
    }
}

static void __handle_matter_update_str(uint16_t endpoint_id, uint32_t cluster_id,
                                       uint32_t attribute_id, const char *value)
{
    if (!__inbound_open(endpoint_id) || !value) {
        return;
    }
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        rm_mirror_binding_t *b = __engine.bindings[i];
        if (b->kind == RM_MIRROR_BINDING_STR_ATTR && b->endpoint_id == endpoint_id &&
                b->cap->cluster_id == cluster_id && b->cap->attribute_id == attribute_id) {
            __str_inbound(b, value);
            return;
        }
    }
}

void rm_mirror_engine_handle_matter_update_str(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, const char *value)
{
    __table_enter();
    __handle_matter_update_str(endpoint_id, cluster_id, attribute_id, value);
    __table_exit();
}

/**
 * @brief Deliver a committed string to the device write callback.
 */
static void __inject_str(const rm_mirror_binding_t *b, const char *value)
{
    esp_rmaker_param_write_req_t req = {
        .param = (esp_rmaker_param_t *)b->slots[0].param,
        /* esp_rmaker_str wraps the pointer (no copy); valid for the duration
         * of the synchronous write callback, which copies on apply */
        .val = esp_rmaker_str(value),
    };
    esp_rmaker_error_t err = esp_rmaker_device_write_params(__b_device(b), &req, 1,
                             ESP_RMAKER_REQ_SRC_EXTERNAL);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Matter-originated string write failed: %d", err);
    }
}

/** @brief A RainMaker Neo-domain integer as the param's own value type. */
static esp_rmaker_param_val_t __val_from_rmng(const esp_rmaker_param_t *param, int32_t v)
{
    switch (__param_type(param)) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        return esp_rmaker_bool(v != 0);
    case RMAKER_VAL_TYPE_FLOAT:
        return esp_rmaker_float((float)v);
    default:
        return esp_rmaker_int(v);
    }
}

/**
 * @brief Deliver one committed value to the device write callback, converting the parked
 *        Matter-domain value into the param's own value type.
 */
static void __inject_binding(rm_mirror_binding_t *b, int32_t matter_val)
{
    const esp_rmaker_param_t *param = b->slots[0].param;
    esp_rmaker_param_write_req_t req = {
        .param = (esp_rmaker_param_t *)param,
    };
    /* Floats take the transform in the float domain, so they keep the fractional part. */
    if (__param_type(param) == RMAKER_VAL_TYPE_FLOAT) {
        req.val = esp_rmaker_float(rm_mirror_xform_to_rmng_f(&b->cap->xform, matter_val));
    } else {
        req.val = __val_from_rmng(param, rm_mirror_xform_to_rmng(&b->cap->xform, matter_val));
    }
    esp_rmaker_error_t err = esp_rmaker_device_write_params(__b_device(b), &req, 1,
                             ESP_RMAKER_REQ_SRC_EXTERNAL);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Matter-originated write failed for param %s: %d",
                  esp_rmaker_param_get_id((esp_rmaker_param_t *)param), err);
    }
}

/**
 * @brief Deliver a committed composite conversion: one atomic
 *        multi-param write to the device write callback.
 */
static void __inject_composite(rm_mirror_binding_t *b, const int32_t *rmng_vals)
{
    rm_mirror_tail_t t = __b_tail(b);
    esp_rmaker_param_write_req_t req[RM_MIRROR_COMPOSITE_MAX_PARAMS];
    for (size_t i = 0; i < t.n_params; i++) {
        req[i].param = (esp_rmaker_param_t *)t.params[i].param;
        req[i].val = __val_from_rmng(t.params[i].param, rmng_vals[i]);
    }
    esp_rmaker_error_t err = esp_rmaker_device_write_params(__b_device(b), req, t.n_params,
                             ESP_RMAKER_REQ_SRC_EXTERNAL);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Matter-originated composite write (%s) failed: %d", b->comp->id, err);
    }
}

/** @brief Drain one composite binding's undelivered conversion. */
static void __drain_composite(rm_mirror_binding_t *b)
{
    rm_mirror_tail_t t = __b_tail(b);
    int32_t rmng_vals[RM_MIRROR_COMPOSITE_MAX_PARAMS] = {0};
    int32_t matter_vals[RM_MIRROR_COMPOSITE_MAX_ATTRS] = {0};

    __state_lock();
    bool pending = (b->flags & RM_MIRROR_F_PENDING) != 0;
    if (pending) {
        b->flags &= ~RM_MIRROR_F_PENDING;
        for (size_t i = 0; i < t.n_slots; i++) {
            matter_vals[i] = t.last[i].val.i;
        }
        rm_mirror_composite_to_rmng(b->comp, matter_vals, rmng_vals);
        /* Remember what we are about to inject (RainMaker Neo domain): the
         * resulting observer callbacks are suppressed against these
         * values, which terminates the loop despite conversion rounding */
        for (size_t i = 0; i < t.n_params; i++) {
            t.committed[i].val.i = rmng_vals[i];
        }
        b->flags |= RM_MIRROR_F_COMMITTED_VALID;
    }
    __state_unlock();
    if (!pending) {
        return;
    }
    /* Skip the write entirely when the canonical store already holds
     * these values (e.g. the controller wrote back our own reports) */
    bool all_equal = true;
    for (size_t i = 0; i < t.n_params; i++) {
        int32_t current = 0;
        if (rm_mirror_param_val_as_i32(t.params[i].param, &current) != ESP_RMAKER_OK ||
                current != rmng_vals[i]) {
            all_equal = false;
            break;
        }
    }
    if (!all_equal) {
        __inject_composite(b, rmng_vals);
    }
}

/** @brief Deliver an identify request parked on @p b to the device's identify param. */
static void __drain_identify(rm_mirror_binding_t *b)
{
    __state_lock();
    const bool pending = (b->flags & RM_MIRROR_F_IDENTIFY_PENDING) != 0;
    const int32_t seconds = b->identify_seconds;
    b->flags &= ~RM_MIRROR_F_IDENTIFY_PENDING;
    __state_unlock();
    if (!pending) {
        return;
    }
    const esp_rmaker_device_t *device = __b_device(b);
    esp_rmaker_param_write_req_t req = {
        .param = (esp_rmaker_param_t *)esp_rmaker_device_get_param_by_type(device, ESP_RMAKER_PARAM_IDENTIFY),
        .val = esp_rmaker_int(seconds),
    };
    if (req.param) {
        esp_rmaker_device_write_params(device, &req, 1, ESP_RMAKER_REQ_SRC_EXTERNAL);
    }
}

static void __drain_pending(void)
{
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        rm_mirror_binding_t *b = __engine.bindings[i];
        __drain_identify(b);
        if (b->kind == RM_MIRROR_BINDING_COMPOSITE) {
            __drain_composite(b);
            continue;
        }
        rm_mirror_tail_t t = __b_tail(b);
        char value[RM_MIRROR_STR_MAX_LEN + 1] = {0};
        int32_t matter_val = 0;

        __state_lock();
        bool pending = (b->flags & RM_MIRROR_F_PENDING) != 0;
        if (pending) {
            if (rm_mirror_binding_is_str(b)) {
                /* Copied out under the lock: the slot can be replaced while the
                 * injection runs the application's write callback. */
                strncpy(value, t.committed[0].val.s ? t.committed[0].val.s : "", sizeof(value) - 1);
                __slot_clear(b, &t.committed[0]);
            } else {
                matter_val = t.committed[0].val.i;
            }
            b->flags &= ~RM_MIRROR_F_PENDING;
        }
        __state_unlock();

        if (!pending) {
            continue;
        }
        if (rm_mirror_binding_is_str(b)) {
            __inject_str(b, value);
        } else {
            /* Nothing to deliver when the store already holds it (a walked-back transient) */
            int32_t current = 0;
            if (rm_mirror_param_to_matter(t.params[0].param, b->cap, &current) == ESP_RMAKER_OK
                    && current == matter_val) {
                continue;
            }
            __inject_binding(b, matter_val);
        }
    }
}

void rm_mirror_engine_drain_pending(void)
{
    __table_enter();
    __drain_pending();
    __table_exit();
}

static void __injection_task(void *arg)
{
    (void)arg;
    while (__engine.running) {
        osal_semaphore_take(__engine.wake, OSAL_MAX_DELAY);
        if (!__engine.running) {
            break;
        }
        /* Settle first, so replace semantics collapse a cluster's own back-to-back coupling
         * writes (LevelControl's minimum-then-move) instead of actuating the intermediate. */
        osal_task_delay(osal_ticks_from_ms(CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_RENDER_TICK_MS));
        rm_mirror_engine_drain_pending();
    }
    __engine.running = false;
    if (__engine.exited) {
        osal_semaphore_give(__engine.exited);
    }
    osal_task_delete(NULL);
}

/**
 * @brief The device behind a mirrored endpoint, via any of its bindings.
 *
 * The string binding is skipped: it sits on endpoint 0, which is the root rather
 * than a device.
 */
static rm_mirror_binding_t *__binding_for_endpoint(uint16_t endpoint_id)
{
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        rm_mirror_binding_t *b = __engine.bindings[i];
        if (b->endpoint_id == endpoint_id && !rm_mirror_binding_is_str(b)) {
            return b;
        }
    }
    return NULL;
}

static const esp_rmaker_device_t *__device_for_endpoint(uint16_t endpoint_id)
{
    rm_mirror_binding_t *b = __binding_for_endpoint(endpoint_id);
    return b ? __b_device(b) : NULL;
}

bool rm_mirror_engine_copy_device_id(uint16_t endpoint_id, char *buf, size_t len)
{
    __table_enter();
    const esp_rmaker_device_t *device = __device_for_endpoint(endpoint_id);
    const char *id = device ? esp_rmaker_device_get_id(device) : NULL;
    if (id && len > 0) {
        strncpy(buf, id, len - 1);
        buf[len - 1] = '\0';
    }
    __table_exit();
    return id && len > 0;
}

/* Boot values (StartUp* semantics) ********************************************/

static esp_rmaker_error_t __get_bound_value(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t *matter_val)
{
    rm_mirror_binding_t *b = __find_by_path(endpoint_id, cluster_id, attribute_id);
    if (!b) {
        return ESP_RMAKER_NOT_FOUND;
    }
    int32_t rmng_val = 0;
    esp_rmaker_error_t err = rm_mirror_param_val_as_i32(b->slots[0].param, &rmng_val);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    *matter_val = rm_mirror_xform_to_matter(&b->cap->xform, rmng_val);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t rm_mirror_engine_get_bound_value(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t *matter_val)
{
    __table_enter();
    esp_rmaker_error_t ret = __get_bound_value(endpoint_id, cluster_id, attribute_id, matter_val);
    __table_exit();
    return ret;
}

/* StartUpOnOff's "toggle the previous state" selector; 0/1 assert Off/On. */
#define RM_MIRROR_STARTUP_TOGGLE_SELECTOR 2

static esp_rmaker_error_t __apply_boot_value(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t matter_val);

static void __apply_startup_policies(void)
{
    if (!__engine.port || !__engine.port->attribute_get) {
        return;
    }
    const bool sw_update = __engine.port->boot_is_software_update &&
                           __engine.port->boot_is_software_update();

    for (size_t i = 0; i < __engine.n_bindings; i++) {
        rm_mirror_binding_t *b = __engine.bindings[i];
        if (b->kind != RM_MIRROR_BINDING_SCALAR) {
            continue;
        }
        const rm_mirror_capability_t *cap = b->cap;
        if (cap->startup_policy == RM_MIRROR_STARTUP_NONE ||
                (cap->startup_skip_on_sw_update && sw_update)) {
            continue;
        }
        int32_t startup = 0;
        if (__engine.port->attribute_get(b->endpoint_id, cap->cluster_id,
                                         cap->startup_attr, &startup) != ESP_RMAKER_OK) {
            continue; /* absent or null: no configured boot state */
        }

        int32_t boot_val;
        switch (cap->startup_policy) {
#if RM_MIRROR_USES_STARTUP_TOGGLE_PREVIOUS
        case RM_MIRROR_STARTUP_TOGGLE_PREVIOUS:
            if (startup == RM_MIRROR_STARTUP_TOGGLE_SELECTOR) {
                int32_t previous = 0;
                if (__get_bound_value(b->endpoint_id, cap->cluster_id,
                                      cap->attribute_id, &previous) != ESP_RMAKER_OK) {
                    continue;
                }
                boot_val = previous ? 0 : 1;
            } else {
                boot_val = (startup != 0) ? 1 : 0;
            }
            break;
#endif
#if RM_MIRROR_USES_STARTUP_MIN_CLAMP
        case RM_MIRROR_STARTUP_MIN_CLAMP:
            boot_val = (startup > cap->startup_min) ? startup : cap->startup_min;
            break;
#endif
        default:
            boot_val = startup;
            break;
        }
        __apply_boot_value(b->endpoint_id, cap->cluster_id,
                           cap->attribute_id, boot_val);
    }
}

void rm_mirror_engine_apply_startup_policies(void)
{
    __table_enter();
    __apply_startup_policies();
    __table_exit();
}

static esp_rmaker_error_t __apply_boot_value(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t matter_val)
{
    rm_mirror_binding_t *b = __find_by_path(endpoint_id, cluster_id, attribute_id);
    if (!b) {
        return ESP_RMAKER_NOT_FOUND;
    }
    int32_t rmng_val = rm_mirror_xform_to_rmng(&b->cap->xform, matter_val);
    int32_t current = 0;
    if (rm_mirror_param_val_as_i32(b->slots[0].param, &current) == ESP_RMAKER_OK && current == rmng_val) {
        return ESP_RMAKER_OK; /* previous state already matches the policy */
    }
    OSAL_LOGI(TAG, "Applying Matter boot value for param %s.",
              esp_rmaker_param_get_id((esp_rmaker_param_t *)b->slots[0].param));
    __inject_binding(b, matter_val);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t rm_mirror_engine_apply_boot_value(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t matter_val)
{
    __table_enter();
    esp_rmaker_error_t ret = __apply_boot_value(endpoint_id, cluster_id, attribute_id, matter_val);
    __table_exit();
    return ret;
}

/* Identify ********************************************************************/

/* Registered before the mirror is enabled, so it lives outside __engine, which init clears. */
static esp_rmaker_matter_mirror_identify_t __identify_handler;

void rm_mirror_engine_set_identify_handler(esp_rmaker_matter_mirror_identify_t handler)
{
    __identify_handler = handler;
}

void rm_mirror_engine_handle_identify(uint16_t endpoint_id, int32_t seconds)
{
    rm_mirror_engine_handle_identify_effect(endpoint_id, ESP_RMAKER_MATTER_MIRROR_IDENTIFY_PLAIN, 0, seconds);
}

static void __handle_identify_effect(uint16_t endpoint_id,
                                     esp_rmaker_matter_mirror_identify_effect_t effect_id, uint8_t effect_variant, int32_t seconds)
{
    rm_mirror_binding_t *b = __binding_for_endpoint(endpoint_id);
    if (!b) {
        return;
    }
    const esp_rmaker_device_t *device = __b_device(b);
    /* The device renders effects itself where it can: the param carries a duration alone. */
    if (__identify_handler && __identify_handler(esp_rmaker_device_get_id(device), effect_id,
            effect_variant, seconds) == ESP_RMAKER_OK) {
        return;
    }
    if (!esp_rmaker_device_get_param_by_type(device, ESP_RMAKER_PARAM_IDENTIFY)) {
        OSAL_LOGI(TAG, "Identify requested for device '%s' (%lds) - no %s param, ignored.",
                  esp_rmaker_device_get_id(device), (long)seconds, ESP_RMAKER_PARAM_IDENTIFY);
        return;
    }
    /* Parked like any inbound value, so the write callback runs on the injection task, not the Matter thread. */
    __state_lock();
    b->identify_seconds = seconds;
    b->flags |= RM_MIRROR_F_IDENTIFY_PENDING;
    __state_unlock();
    if (__engine.wake) {
        osal_semaphore_give(__engine.wake);
    }
}

void rm_mirror_engine_handle_identify_effect(uint16_t endpoint_id,
        esp_rmaker_matter_mirror_identify_effect_t effect_id, uint8_t effect_variant, int32_t seconds)
{
    __table_enter();
    __handle_identify_effect(endpoint_id, effect_id, effect_variant, seconds);
    __table_exit();
}

/* Lifecycle *******************************************************************/

esp_rmaker_error_t rm_mirror_sync_init(const rm_mirror_port_ops_t *port)
{
    if (!port || !port->endpoint_create || !port->cluster_create ||
            !port->attribute_set || !port->attribute_update) {
        /* attribute_write_str is optional: a port without it leaves names cloud-only. */
        return ESP_RMAKER_INVALID_ARG;
    }
    memset(&__engine, 0, sizeof(__engine));
    __engine.port = port;
    __engine.lock = osal_semaphore_create_mutex();
    __engine.membership = osal_semaphore_create_mutex();
    if (!__engine.lock || !__engine.membership) {
        if (__engine.lock) {
            osal_semaphore_delete(__engine.lock);
        }
        if (__engine.membership) {
            osal_semaphore_delete(__engine.membership);
        }
        memset(&__engine, 0, sizeof(__engine));
        return ESP_RMAKER_NO_MEM;
    }
    rm_mirror_endpoint_map_load();
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t rm_mirror_engine_init(const esp_rmaker_node_t *node, const rm_mirror_port_ops_t *port)
{
    if (__engine.initialized) {
        return ESP_RMAKER_ALREADY_INITIALIZED;
    }
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = rm_mirror_sync_init(port);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    __engine.node = node;

    err = rm_mirror_bridge_init(port);
    if (err == ESP_RMAKER_OK) {
        err = rm_mirror_lowering_run(node, port);
    }
    if (err != ESP_RMAKER_OK) {
        rm_mirror_engine_deinit();
        return err;
    }

    /* Node-scoped name binding: when exactly one device is mirrored, its name
     * param (the table's node_scoped capability) syncs with the attribute that
     * capability matches on the root endpoint. A multi-device node has no
     * node-level name, so its names stay cloud-side, matched to endpoints
     * through the FixedLabel correlation entry the port serves. */
    const rm_mirror_binding_t *first = NULL;
    bool single_endpoint = false;
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        const rm_mirror_binding_t *b = __engine.bindings[i];
        if (b->kind != RM_MIRROR_BINDING_SCALAR) {
            continue;
        }
        if (!first) {
            first = b;
            single_endpoint = true;
        } else if (b->endpoint_id != first->endpoint_id) {
            single_endpoint = false;
            break;
        }
    }
    if (single_endpoint) {
        const esp_rmaker_device_t *device = __b_device(first);
        for (size_t c = 0; c < rm_mirror_n_capabilities; c++) {
            const rm_mirror_capability_t *cap = &rm_mirror_capabilities[c];
            if (!cap->node_scoped) {
                continue;
            }
            const esp_rmaker_param_t *name_param =
                esp_rmaker_device_get_param_by_type(device, cap->param_type);
            if (name_param) {
                rm_mirror_sync_add_str_binding(name_param, cap, 0);
            }
            break;
        }
    }

    __state_lock();
    __engine.initialized = true;
    __state_unlock();
    OSAL_LOGI(TAG, "Mirror engine initialized: %u binding(s), mapping v%s.",
              (unsigned)__engine.n_bindings, rm_mirror_mapping_version);
    return ESP_RMAKER_OK;
}

/** Endpoint a device is mirrored on, or 0 when it is not mirrored. */
static uint16_t __endpoint_for_device(const esp_rmaker_device_t *device)
{
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        const rm_mirror_binding_t *b = __engine.bindings[i];
        if (!rm_mirror_binding_is_str(b) && __b_device(b) == device) {
            return b->endpoint_id;
        }
    }
    return 0;
}

/** What a reseed asserts: everything, or a runtime add's endpoint before and after it is enabled. */
typedef enum {
    __RESEED_FULL,
    __RESEED_DISABLED,  /* attributes only */
    __RESEED_ENABLED,   /* what needs a live cluster: report_as, strings, seed_as */
} __reseed_phase_t;

static void __reseed(bool all, uint16_t endpoint_id, __reseed_phase_t phase);

/** Undo a failed add: unbind, then drop the endpoint, as a removal does; not while still bound. */
static esp_rmaker_error_t __add_failed(uint16_t ep, esp_rmaker_error_t err)
{
    if (ep == 0) {
        return err;
    }
    /* Lowering can fail before it binds anything; then no removal can find the endpoint later */
    __table_enter();
    const bool bound = (__device_for_endpoint(ep) != NULL);
    __table_exit();
    if (bound && rm_mirror_sync_remove_endpoint(ep) == ESP_RMAKER_TIMEOUT) {
        return ESP_RMAKER_TIMEOUT;
    }
    rm_mirror_lowering_drop_endpoint(__engine.port, ep);
    /* TIMEOUT tells the caller the device is still bound, which it no longer is */
    return (err == ESP_RMAKER_TIMEOUT) ? ESP_RMAKER_FAIL : err;
}

void rm_mirror_sync_set_dormant(uint16_t endpoint_id)
{
    if (!__engine.lock) {
        return;
    }
    __state_lock();
    __engine.dormant_endpoint = endpoint_id;
    __state_unlock();
}

static esp_rmaker_error_t __add_device(const esp_rmaker_device_t *device, uint16_t *endpoint_id)
{
    __table_enter();
    const bool mirrored = (__endpoint_for_device(device) != 0);
    __table_exit();
    if (mirrored) {
        return ESP_RMAKER_ALREADY_EXISTS;
    }

    __state_lock();
    const bool live = __engine.inbound_ready;
    __state_unlock();
    uint16_t ep = 0;
    esp_rmaker_error_t err = rm_mirror_lowering_lower_device(__engine.port, device, live, &ep);
    if (err == ESP_RMAKER_OK && ep == 0) {
        return ESP_RMAKER_NOT_SUPPORTED;
    }
    if (err == ESP_RMAKER_OK && live) {
        /* Before the endpoint is visible, so no controller write can be overwritten */
        __table_enter();
        __reseed(false, ep, __RESEED_DISABLED);
        __table_exit();
    }
    /* From here inbound counts: the cluster init callbacks apply Matter's StartUp*, which wins */
    rm_mirror_sync_set_dormant(0);
    if (err == ESP_RMAKER_OK && __engine.port->endpoint_enable) {
        err = __engine.port->endpoint_enable(ep);
    }
    if (err != ESP_RMAKER_OK) {
        err = __add_failed(ep, err);
        if (err == ESP_RMAKER_TIMEOUT) {
            *endpoint_id = ep;
        }
        return err;
    }
    if (live) {
        __table_enter();
        __reseed(false, ep, __RESEED_ENABLED);
        __table_exit();
    }
    *endpoint_id = ep;
    return ESP_RMAKER_OK;
}

/** Whether the caller holds the Matter stack lock, which a table lock wait can deadlock on. */
static bool __stack_lock_held(void)
{
    return __engine.port->stack_lock_held && __engine.port->stack_lock_held();
}

/** Whether the port opened device membership, once the node's own devices are bound and enable() returned. */
static bool __initialized(void)
{
    if (!__engine.lock) {
        return false;
    }
    __state_lock();
    const bool open = __engine.initialized && __engine.devices_open;
    __state_unlock();
    return open;
}

void rm_mirror_engine_open_devices(void)
{
    if (__engine.lock) {
        __state_lock();
        __engine.devices_open = true;
        __state_unlock();
    }
}

esp_rmaker_error_t rm_mirror_engine_add_device(const esp_rmaker_device_t *device, uint16_t *endpoint_id)
{
    if (!__initialized()) {
        return ESP_RMAKER_NOT_INITIALIZED;
    }
    if (!device || esp_rmaker_device_is_service(device)) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (__stack_lock_held()) {
        return ESP_RMAKER_INVALID_STATE;
    }
    uint16_t ep = 0;
    osal_semaphore_take(__engine.membership, OSAL_MAX_DELAY);
    esp_rmaker_error_t err = __add_device(device, &ep);
    osal_semaphore_give(__engine.membership);
    if (endpoint_id) {
        *endpoint_id = ep;
    }
    return err;
}

const esp_rmaker_node_t *rm_mirror_engine_get_node(void)
{
    return __engine.node;
}

esp_rmaker_error_t rm_mirror_engine_remove_device(const esp_rmaker_device_t *device, uint16_t *endpoint_id)
{
    if (!__initialized()) {
        return ESP_RMAKER_NOT_INITIALIZED;
    }
    if (!device || esp_rmaker_device_is_service(device)) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (__stack_lock_held()) {
        return ESP_RMAKER_INVALID_STATE;
    }
    osal_semaphore_take(__engine.membership, OSAL_MAX_DELAY);
    __table_enter();
    const uint16_t ep = __endpoint_for_device(device);
    __table_exit();
    esp_rmaker_error_t err = ep ? rm_mirror_sync_remove_endpoint(ep) : ESP_RMAKER_NOT_FOUND;
    if (err == ESP_RMAKER_OK) {
        rm_mirror_lowering_drop_endpoint(__engine.port, ep);
    }
    osal_semaphore_give(__engine.membership);
    if (endpoint_id) {
        *endpoint_id = ep;
    }
    return err;
}

esp_rmaker_error_t rm_mirror_engine_set_reachable(const esp_rmaker_device_t *device, bool reachable)
{
    if (!__initialized()) {
        return ESP_RMAKER_NOT_INITIALIZED;
    }
    if (!device || esp_rmaker_device_is_service(device)) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (__stack_lock_held()) {
        return ESP_RMAKER_INVALID_STATE;
    }
    osal_semaphore_take(__engine.membership, OSAL_MAX_DELAY);
    esp_rmaker_error_t err = rm_mirror_lowering_store_reachable(device, reachable);
    __table_enter();
    const uint16_t ep = __endpoint_for_device(device);
    __table_exit();
    if (err == ESP_RMAKER_OK && ep && __engine.port->endpoint_reachable_set) {
        err = __engine.port->endpoint_reachable_set(ep, reachable);
    }
    osal_semaphore_give(__engine.membership);
    return err;
}

static void __engine_delete_task_semaphores(void)
{
    if (__engine.wake) {
        osal_semaphore_delete(__engine.wake);
        __engine.wake = NULL;
    }
    if (__engine.exited) {
        osal_semaphore_delete(__engine.exited);
        __engine.exited = NULL;
    }
}

esp_rmaker_error_t rm_mirror_engine_start(void)
{
    if (!__engine.initialized) {
        return ESP_RMAKER_NOT_INITIALIZED;
    }
    esp_rmaker_error_t err = esp_rmaker_param_update_observer_register(__param_update_observer, NULL);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    __engine.wake = osal_semaphore_create_binary();
    __engine.exited = osal_semaphore_create_binary();
    if (!__engine.wake || !__engine.exited) {
        esp_rmaker_param_update_observer_unregister(__param_update_observer);
        __engine_delete_task_semaphores();
        return ESP_RMAKER_NO_MEM;
    }
    __engine.running = true;
    if (osal_task_create(__injection_task, "rm_mirror", CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_TASK_STACK,
                         NULL, CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_TASK_PRIORITY, &__engine.task) != OSAL_ERR_OK) {
        __engine.running = false;
        esp_rmaker_param_update_observer_unregister(__param_update_observer);
        __engine_delete_task_semaphores();
        return ESP_RMAKER_NO_MEM;
    }
    return ESP_RMAKER_OK;
}

#if RM_MIRROR_SEEDS_SHADOWS
/** Whether a binding before @p upto already carries this seed command on the same path. */
static bool __seed_already_sent(size_t upto, uint16_t endpoint_id, uint32_t cluster_id,
                                uint32_t command_id)
{
    for (size_t i = 0; i < upto; i++) {
        const rm_mirror_binding_t *b = __engine.bindings[i];
        if (b->kind != RM_MIRROR_BINDING_SCALAR || b->endpoint_id != endpoint_id ||
                b->cap->cluster_id != cluster_id) {
            continue;
        }
        for (size_t c = 0; c < b->cap->n_seed_cmds; c++) {
            if (b->cap->seed_cmds[c].command_id == command_id) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Invoke every capability's seed_as commands, once each per (endpoint, cluster).
 *
 * For a cluster server that keeps its own copy of an attribute the mirror owns: the copy is only
 * refreshed by a command, so it has to be told once the canonical values are in place. Two
 * capabilities on one cluster may declare the same seed, hence the dedupe.
 */
static void __seed_cluster_shadows(bool all, uint16_t endpoint_id)
{
    if (!__engine.port->command_invoke) {
        return;
    }
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        const rm_mirror_binding_t *b = __engine.bindings[i];
        if (b->kind != RM_MIRROR_BINDING_SCALAR || b->cap->n_seed_cmds == 0 ||
                (!all && b->endpoint_id != endpoint_id)) {
            continue;
        }
        for (size_t c = 0; c < b->cap->n_seed_cmds; c++) {
            const rm_mirror_write_cmd_t *cmd = &b->cap->seed_cmds[c];
            if (__seed_already_sent(i, b->endpoint_id, b->cap->cluster_id, cmd->command_id)) {
                continue;
            }
            OSAL_LOGI(TAG, "Seeding cluster 0x%04X on endpoint %u with command 0x%02X.",
                      (unsigned)b->cap->cluster_id, (unsigned)b->endpoint_id,
                      (unsigned)cmd->command_id);
            __engine.port->command_invoke(b->endpoint_id, b->cap->cluster_id, cmd, 0);
        }
    }
}
#endif /* RM_MIRROR_SEEDS_SHADOWS */

/** Assert the param values on every endpoint, or on @p endpoint_id alone when not @p all. */
static void __reseed(bool all, uint16_t endpoint_id, __reseed_phase_t phase)
{
    const bool write_attrs = (phase != __RESEED_ENABLED);
    const bool cluster_enabled = (phase != __RESEED_DISABLED);
    if (!__engine.port || !__engine.lock) {
        return;
    }
    /* Attributes the mirror seeded at init can be overwritten while the stack starts: the cluster
     * StartUp* handling, or esp-matter restoring a NONVOLATILE attribute from a previous session.
     * The param store is canonical, so assert it once more now that the stack is up. */
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        rm_mirror_binding_t *b = __engine.bindings[i];
        if (!all && b->endpoint_id != endpoint_id) {
            continue;
        }
        rm_mirror_tail_t t = __b_tail(b);

        switch (b->kind) {
        case RM_MIRROR_BINDING_SCALAR: {
            int32_t matter_val = 0;
            if (rm_mirror_param_to_matter(t.params[0].param, b->cap, &matter_val) != ESP_RMAKER_OK ||
                    (b->cap->report_as ? !cluster_enabled : !write_attrs)) {
                continue;
            }
            __state_lock();
            t.last[0].val.i = matter_val;
            /* Canonical again, so also what the app holds: reseed the injection basis */
            t.committed[0].val.i = matter_val;
            b->flags |= RM_MIRROR_F_LAST_VALID | RM_MIRROR_F_COMMITTED_VALID;
            b->flags &= ~(RM_MIRROR_F_ECHO(0) | RM_MIRROR_F_PENDING);
            __state_unlock();

            if (rm_mirror_param_holds_state(t.params[0].param)) {
                rm_mirror_port_push(__engine.port, b->endpoint_id, b->cap, matter_val, false);
            }
            break;
        }
        case RM_MIRROR_BINDING_COMPOSITE: {
            if (!write_attrs) {
                continue;
            }
            int32_t rmng_vals[RM_MIRROR_COMPOSITE_MAX_PARAMS] = {0};
            bool readable = true;
            for (size_t p = 0; p < t.n_params; p++) {
                if (rm_mirror_param_val_as_i32(t.params[p].param, &rmng_vals[p]) != ESP_RMAKER_OK) {
                    readable = false;
                    break;
                }
            }
            if (!readable) {
                continue;
            }
            int32_t matter_vals[RM_MIRROR_COMPOSITE_MAX_ATTRS] = {0};
            rm_mirror_composite_to_matter(b->comp, rmng_vals, matter_vals);

            __state_lock();
            for (size_t a = 0; a < t.n_slots; a++) {
                t.last[a].val.i = matter_vals[a];
                b->flags &= ~RM_MIRROR_F_ECHO(a);
            }
            b->flags &= ~(RM_MIRROR_F_COMMITTED_VALID | RM_MIRROR_F_PENDING);
            __state_unlock();

            for (size_t a = 0; a < t.n_slots; a++) {
                __engine.port->attribute_update(b->endpoint_id, b->comp->cluster_id,
                                                b->comp->attribute_ids[a], matter_vals[a],
                                                b->comp->value_types[a]);
            }
            break;
        }
        default: {
            /* The string binding's first write: its sink lives in a cluster the
             * stack implements itself, which rejects writes until Matter is up. */
            esp_rmaker_param_val_t *val =
                esp_rmaker_param_get_val((esp_rmaker_param_t *)t.params[0].param);
            if (!cluster_enabled || !val || val->type != RMAKER_VAL_TYPE_STRING) {
                continue;
            }
            char truncated[RM_MIRROR_STR_MAX_LEN + 1];
            __str_truncate(b, truncated, val->val.s);
            esp_rmaker_param_val_t tv = esp_rmaker_str(truncated);

            __state_lock();
            __slot_store(b, &t.last[0], RM_MIRROR_F_LAST_VALID, &tv);
            b->flags &= ~(RM_MIRROR_F_ECHO(0) | RM_MIRROR_F_PENDING);
            __state_unlock();

            __str_push(b, truncated);
            break;
        }
        }
    }

#if RM_MIRROR_SEEDS_SHADOWS
    /* Canonical values are in place, so a server keeping its own copy can be told now. */
    if (cluster_enabled) {
        __seed_cluster_shadows(all, endpoint_id);
    }
#endif /* RM_MIRROR_SEEDS_SHADOWS */

    if (all) {
        __state_lock();
        __engine.inbound_ready = true;
        __state_unlock();
    }
}

void rm_mirror_engine_reseed(void)
{
    __table_enter();
    __reseed(true, 0, __RESEED_FULL);
    __table_exit();
}

void rm_mirror_engine_start_inbound(void)
{
    if (!__engine.membership) {
        return;
    }
    osal_semaphore_take(__engine.membership, OSAL_MAX_DELAY);
    __state_lock();
    const bool started = __engine.inbound_ready;
    __state_unlock();
    /* StartUp* is a boot rule: a second call must not apply it to a running node */
    if (!started) {
        rm_mirror_engine_apply_startup_policies();
        rm_mirror_engine_reseed();
    }
    osal_semaphore_give(__engine.membership);
}

void rm_mirror_engine_deinit(void)
{
    esp_rmaker_param_update_observer_unregister(__param_update_observer);
    if (__engine.running) {
        __engine.running = false;
        osal_semaphore_give(__engine.wake); /* let the task exit */
        /* Drains run outside the lock, so nothing may be freed under one */
        if (__engine.exited) {
            osal_semaphore_take(__engine.exited, osal_ticks_from_ms(RM_MIRROR_TASK_EXIT_TIMEOUT_MS));
        }
        __engine.task = NULL;
    }
    __engine_delete_task_semaphores();
    if (__engine.lock) {
        osal_semaphore_delete(__engine.lock);
    }
    if (__engine.membership) {
        osal_semaphore_delete(__engine.membership);
    }
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        __binding_free(__engine.bindings[i]);
    }
    free(__engine.bindings);
    memset(&__engine, 0, sizeof(__engine));
    rm_mirror_endpoint_map_unload();
    rm_mirror_bridge_deinit();
}

void rm_mirror_engine_set_initialized(void)
{
    if (__engine.lock) {
        __state_lock();
        __engine.initialized = true;
        __engine.devices_open = true;
        __state_unlock();
    }
}

size_t rm_mirror_engine_count_bindings(rm_mirror_binding_kind_t kind)
{
    size_t count = 0;
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        if (__engine.bindings[i]->kind == kind) {
            count++;
        }
    }
    return count;
}

const rm_mirror_binding_t *rm_mirror_engine_get_binding(rm_mirror_binding_kind_t kind, size_t nth)
{
    for (size_t i = 0; i < __engine.n_bindings; i++) {
        if (__engine.bindings[i]->kind == kind && nth-- == 0) {
            return __engine.bindings[i];
        }
    }
    return NULL;
}
