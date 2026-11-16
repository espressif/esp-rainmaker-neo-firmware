/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_mirror_vectors.c
 * @brief Golden-vector parity: the compiled transform and conversion primitives must reproduce the
 *        mapping repo's reference implementations bit for bit. rm_mirror_vectors_gen.h is lowered at
 *        build time from the committed vectors JSON under mapping/vectors, which the mapping repo emits.
 *
 * The test build uses the test profile, so every implemented primitive is compiled and runs. The
 * vectors are library-level while the tables are profile-resolved, so both tests scope themselves by
 * looking each vector's subject up in the compiled tables and skipping what a narrower profile left
 * out. Neither names a transform kind or a conversion, and neither assumes how many values a
 * conversion takes: a primitive added upstream is covered as soon as its vectors arrive.
 */

#include <string.h>

#include "unity.h"

#include "rm_mirror_engine.h"
#include "rm_mirror_vectors_gen.h"

static const rm_mirror_capability_t *__find_capability(const char *param_type)
{
    for (size_t i = 0; i < rm_mirror_n_capabilities; i++) {
        if (strcmp(rm_mirror_capabilities[i].param_type, param_type) == 0) {
            return &rm_mirror_capabilities[i];
        }
    }
    return NULL;
}

RM_MIRROR_STATIC_ASSERT(RM_MIRROR_VEC_MAX_VALS >= RM_MIRROR_COMPOSITE_MAX_PARAMS,
                        "vector rows are narrower than the widest composite the engine allows");
RM_MIRROR_STATIC_ASSERT(RM_MIRROR_VEC_MAX_VALS >= RM_MIRROR_COMPOSITE_MAX_ATTRS,
                        "vector rows are narrower than the widest composite the engine allows");

/** @brief Render @p n values as "a, b, c", for an assertion message. */
static void __join(char *buf, size_t len, const int32_t *vals, uint8_t n)
{
    size_t off = 0;
    for (uint8_t k = 0; k < n && off + 1 < len; k++) {
        int wrote = snprintf(buf + off, len - off, k ? ", %d" : "%d", (int)vals[k]);
        if (wrote < 0) {
            break;
        }
        off += (size_t)wrote;
    }
}

static const rm_mirror_composite_t *__find_composite(const char *id)
{
    for (size_t i = 0; i < rm_mirror_n_composites; i++) {
        if (strcmp(rm_mirror_composites[i].id, id) == 0) {
            return &rm_mirror_composites[i];
        }
    }
    return NULL;
}

void test_mirror_golden_vectors_transforms(void)
{
    TEST_ASSERT_EQUAL_STRING(RM_MIRROR_VECTORS_MAPPING_VERSION, rm_mirror_mapping_version);

    size_t checked = 0;
    for (size_t t = 0; t < rm_mirror_n_vec_transforms; t++) {
        const rm_mirror_vec_transform_t *vec = &rm_mirror_vec_transforms[t];
        const rm_mirror_capability_t *cap = __find_capability(vec->param_type);
        if (!cap) {
            continue; /* not part of this profile's tables */
        }
        for (size_t i = 0; i < vec->n_to_matter; i++) {
            char where[96];
            snprintf(where, sizeof(where), "%s to_matter(%d)", vec->param_type, (int)vec->to_matter[i].in);
            TEST_ASSERT_EQUAL_INT32_MESSAGE(vec->to_matter[i].out,
                                            rm_mirror_xform_to_matter(&cap->xform, vec->to_matter[i].in), where);
            checked++;
        }
        for (size_t i = 0; i < vec->n_to_rmng; i++) {
            char where[96];
            snprintf(where, sizeof(where), "%s to_rmng(%d)", vec->param_type, (int)vec->to_rmng[i].in);
            TEST_ASSERT_EQUAL_INT32_MESSAGE(vec->to_rmng[i].out,
                                            rm_mirror_xform_to_rmng(&cap->xform, vec->to_rmng[i].in), where);
            checked++;
        }
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, checked, "no transform vectors matched the compiled tables");
}

void test_mirror_golden_vectors_composites(void)
{
    if (rm_mirror_n_composites == 0) {
        TEST_IGNORE_MESSAGE("no composites in this profile's tables");
    }

    size_t checked = 0;
    for (size_t c = 0; c < rm_mirror_n_vec_composites; c++) {
        const rm_mirror_vec_composite_t *vec = &rm_mirror_vec_composites[c];
        const rm_mirror_composite_t *comp = __find_composite(vec->id);
        if (!comp) {
            continue; /* not part of this profile's tables */
        }
        /* The vectors were computed for the arity the tables carry, or one of the two is stale */
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(vec->n_params, comp->n_params, vec->id);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(vec->n_attributes, comp->n_attributes, vec->id);

        char where[128], vals[64];
        for (size_t i = 0; i < vec->n_to_matter; i++) {
            int32_t out[RM_MIRROR_COMPOSITE_MAX_ATTRS] = {0};
            rm_mirror_composite_to_matter(comp, vec->to_matter[i].in, out);
            __join(vals, sizeof(vals), vec->to_matter[i].in, comp->n_params);
            snprintf(where, sizeof(where), "%s to_matter(%s)", vec->id, vals);
            for (uint8_t k = 0; k < comp->n_attributes; k++) {
                TEST_ASSERT_EQUAL_INT32_MESSAGE(vec->to_matter[i].out[k], out[k], where);
            }
            checked++;
        }
        for (size_t i = 0; i < vec->n_to_rmng; i++) {
            int32_t out[RM_MIRROR_COMPOSITE_MAX_PARAMS] = {0};
            rm_mirror_composite_to_rmng(comp, vec->to_rmng[i].in, out);
            __join(vals, sizeof(vals), vec->to_rmng[i].in, comp->n_attributes);
            snprintf(where, sizeof(where), "%s to_rmng(%s)", vec->id, vals);
            for (uint8_t k = 0; k < comp->n_params; k++) {
                TEST_ASSERT_EQUAL_INT32_MESSAGE(vec->to_rmng[i].out[k], out[k], where);
            }
            checked++;
        }
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, checked, "composites are compiled in but no vector matched them");
}
