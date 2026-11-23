/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_handle.c
 * @brief The slot table behind every controller handle.
 *
 * A handle holds a slot index in the low 16 bits and the slot generation in the next 16 bits. A
 * freed slot increments its generation, so an old handle stops matching it.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "osal_mem_alloc.h"

#include "controller_model.h"

#define HANDLE_INDEX_BITS 16
#define HANDLE_INDEX_MASK 0xFFFFu
#define SLOT_INDEX_MAX    HANDLE_INDEX_MASK
#define SLOT_GROW         32

typedef struct {
    union {
        void *ptr;              /**< The object, while the slot is live. */
        uint32_t next_free;     /**< The next free slot, while the slot is free. */
    } u;
    uint16_t gen;
    uint8_t kind;               /**< ctrl_kind_t, or 0 for a free slot. */
} ctrl_slot_t;

/* Slot 0 is never used, so the handle 0 is "none". The table never shrinks, and it outlives
 * esp_rmaker_controller_disable(), so a handle from before a disable cannot match a new object. */
static ctrl_slot_t *s_slots;
static uint32_t s_num_slots;
static uint32_t s_free_head;
static uint32_t s_free_tail;

static inline esp_rmaker_handle_t __encode(uint32_t index, uint16_t gen)
{
    return (esp_rmaker_handle_t)(((uint32_t)gen << HANDLE_INDEX_BITS) | index);
}

/* Free slots form a FIFO, so a slot is reused as late as possible. */
static void __push_free(uint32_t index)
{
    s_slots[index].u.next_free = 0;
    if (s_free_tail) {
        s_slots[s_free_tail].u.next_free = index;
    } else {
        s_free_head = index;
    }
    s_free_tail = index;
}

static uint32_t __pop_free(void)
{
    uint32_t index = s_free_head;
    if (index) {
        s_free_head = s_slots[index].u.next_free;
        if (!s_free_head) {
            s_free_tail = 0;
        }
    }
    return index;
}

static bool __grow(void)
{
    uint32_t first = s_num_slots ? s_num_slots : 1;
    if (first > SLOT_INDEX_MAX) {
        return false;
    }
    uint32_t num = first + SLOT_GROW;
    if (num > SLOT_INDEX_MAX + 1) {
        num = SLOT_INDEX_MAX + 1;
    }
    ctrl_slot_t *grown = OSAL_REALLOC_EXTRAM(s_slots, num * sizeof(*grown));
    if (!grown) {
        return false;
    }
    s_slots = grown;
    memset(&s_slots[s_num_slots], 0, (num - s_num_slots) * sizeof(*s_slots));
    for (uint32_t i = first; i < num; i++) {
        __push_free(i);
    }
    s_num_slots = num;
    return true;
}

esp_rmaker_handle_t controller_handle_alloc(void *obj, ctrl_kind_t kind)
{
    if (!obj) {
        return 0;
    }
    /* No lock before enable means no other task, so the table is safe to touch without one. */
    bool locked = controller_model_lock() == ESP_RMAKER_OK;
    uint32_t index = __pop_free();
    if (!index && __grow()) {
        index = __pop_free();
    }
    esp_rmaker_handle_t handle = 0;
    if (index) {
        s_slots[index].u.ptr = obj;
        s_slots[index].kind = (uint8_t)kind;
        handle = __encode(index, s_slots[index].gen);
    }
    if (locked) {
        controller_model_unlock();
    }
    return handle;
}

void controller_handle_free(esp_rmaker_handle_t handle)
{
    if (!handle) {
        return;
    }
    bool locked = controller_model_lock() == ESP_RMAKER_OK;
    uint32_t index = (uint32_t)handle & HANDLE_INDEX_MASK;
    if (index && index < s_num_slots && s_slots[index].kind &&
            __encode(index, s_slots[index].gen) == handle) {
        s_slots[index].kind = 0;
        s_slots[index].gen++;
        __push_free(index);
    }
    if (locked) {
        controller_model_unlock();
    }
}

esp_rmaker_error_t controller_handle_lookup(esp_rmaker_handle_t handle, ctrl_kind_t kind, void **out)
{
    if (out) {
        *out = NULL;
    }
    if (!handle) {
        return ESP_RMAKER_INVALID_ARG;
    }
    uint32_t index = (uint32_t)handle & HANDLE_INDEX_MASK;
    if ((esp_rmaker_handle_t)(uint32_t)handle != handle || !index || index >= s_num_slots) {
        return ESP_RMAKER_NOT_FOUND;
    }
    const ctrl_slot_t *slot = &s_slots[index];
    if (!slot->kind || __encode(index, slot->gen) != handle) {
        return ESP_RMAKER_NOT_FOUND;
    }
    if (slot->kind != (uint8_t)kind) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (out) {
        *out = slot->u.ptr;
    }
    return ESP_RMAKER_OK;
}

void *controller_handle_resolve(esp_rmaker_handle_t handle, ctrl_kind_t kind)
{
    void *obj = NULL;
    (void)controller_handle_lookup(handle, kind, &obj);
    return obj;
}
