/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_transform.c
 * @brief Pure value transforms between the RainMaker Neo and Matter domains.
 */

#include <math.h>

#include "rm_mirror_engine.h"

#if RM_MIRROR_USES_XFORM_LINEAR
static int32_t __clamp(int32_t val, int32_t lo, int32_t hi)
{
    if (val < lo) {
        return lo;
    }
    if (val > hi) {
        return hi;
    }
    return val;
}

/**
 * @brief Linear range remap with round-half-up, clamped to the output range.
 */
static int32_t __linear_map(int32_t val, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max)
{
    if (in_max == in_min) {
        return out_min;
    }
    val = __clamp(val, in_min, in_max);
    int64_t num = (int64_t)(val - in_min) * (out_max - out_min);
    int64_t den = in_max - in_min;
    int32_t mapped = (int32_t)(out_min + ((num + den / 2) / den));
    return __clamp(mapped, out_min, out_max);
}
#endif

#if RM_MIRROR_USES_XFORM_KELVIN_MIREDS
/**
 * @brief Kelvin <-> mireds: 1e6 / x with round-half-up. Self-inverse.
 */
static int32_t __kelvin_mireds(int32_t val)
{
    if (val <= 0) {
        return 0;
    }
    return (int32_t)((1000000LL + val / 2) / val);
}
#endif

/**
 * @brief Scale factor of a SCALE transform; 1 for every other type, so the scalar paths below stay
 *        branch-free. Never 0 (the generator rejects that).
 */
static int32_t __scale_factor(const rm_mirror_xform_t *xform)
{
    return (xform->type == RM_MIRROR_XFORM_SCALE && xform->u.scalar != 0) ? xform->u.scalar : 1;
}

int32_t rm_mirror_xform_to_matter(const rm_mirror_xform_t *xform, int32_t rmng_val)
{
    switch (xform->type) {
    case RM_MIRROR_XFORM_IDENTITY:
        return rmng_val;
#if RM_MIRROR_USES_XFORM_LINEAR
    case RM_MIRROR_XFORM_LINEAR:
        return __linear_map(rmng_val, xform->u.linear.in_min, xform->u.linear.in_max,
                            xform->u.linear.out_min, xform->u.linear.out_max);
#endif
#if RM_MIRROR_USES_XFORM_KELVIN_MIREDS
    case RM_MIRROR_XFORM_KELVIN_MIREDS:
        return __kelvin_mireds(rmng_val);
#endif
#if RM_MIRROR_USES_XFORM_ENUM_MAP
    case RM_MIRROR_XFORM_ENUM_MAP:
        for (size_t i = 0; i < xform->n_pairs; i++) {
            if (xform->u.enum_map.pairs[i].rmng == rmng_val) {
                return xform->u.enum_map.pairs[i].matter;
            }
        }
        return rmng_val; /* unmapped enum member: pass through (defensive) */
#endif
#if RM_MIRROR_USES_XFORM_SCALE
    case RM_MIRROR_XFORM_SCALE:
        return rmng_val * __scale_factor(xform);
#endif
    default:
        return rmng_val;
    }
}

int32_t rm_mirror_xform_to_rmng(const rm_mirror_xform_t *xform, int32_t matter_val)
{
    switch (xform->type) {
    case RM_MIRROR_XFORM_IDENTITY:
        return matter_val;
#if RM_MIRROR_USES_XFORM_LINEAR
    case RM_MIRROR_XFORM_LINEAR:
        return __linear_map(matter_val, xform->u.linear.out_min, xform->u.linear.out_max,
                            xform->u.linear.in_min, xform->u.linear.in_max);
#endif
#if RM_MIRROR_USES_XFORM_KELVIN_MIREDS
    case RM_MIRROR_XFORM_KELVIN_MIREDS:
        return __kelvin_mireds(matter_val);
#endif
#if RM_MIRROR_USES_XFORM_ENUM_MAP
    case RM_MIRROR_XFORM_ENUM_MAP:
        for (size_t i = 0; i < xform->n_pairs; i++) {
            if (xform->u.enum_map.pairs[i].matter == matter_val) {
                return xform->u.enum_map.pairs[i].rmng;
            }
        }
        /* aliases: extra matter -> rmng foldings (many-to-one, e.g. ColorMode
         * XY folds to the HSV light mode) */
        for (size_t i = 0; i < xform->n_aliases; i++) {
            if (xform->u.enum_map.aliases[i].matter == matter_val) {
                return xform->u.enum_map.aliases[i].rmng;
            }
        }
        return matter_val; /* unmapped enum member: pass through (defensive) */
#endif
#if RM_MIRROR_USES_XFORM_SCALE
    case RM_MIRROR_XFORM_SCALE:
        return matter_val / __scale_factor(xform);
#endif
    default:
        return matter_val;
    }
}

int32_t rm_mirror_xform_to_matter_f(const rm_mirror_xform_t *xform, float rmng_val)
{
    /* SCALE is the transform float params exist for: rounding after scaling is what keeps the
     * fractional part (5.25 W -> 5250 mW). Everything else rounds first and reuses the int path. */
    if (xform->type == RM_MIRROR_XFORM_SCALE) {
        return (int32_t)lroundf(rmng_val * (float)__scale_factor(xform));
    }
    return rm_mirror_xform_to_matter(xform, (int32_t)lroundf(rmng_val));
}

float rm_mirror_xform_to_rmng_f(const rm_mirror_xform_t *xform, int32_t matter_val)
{
    if (xform->type == RM_MIRROR_XFORM_SCALE) {
        return (float)matter_val / (float)__scale_factor(xform);
    }
    return (float)rm_mirror_xform_to_rmng(xform, matter_val);
}

/* Composite conversions *******************************************************/

void rm_mirror_composite_to_matter(const rm_mirror_composite_t *comp, const int32_t *rmng_vals,
                                   int32_t *matter_vals)
{
    switch (comp->conversion) {
#if RM_MIRROR_USES_CONV_HSV_XY
    case RM_MIRROR_COMPOSITE_CONV_HSV_XY:
        rm_mirror_color_hs_to_xy(rmng_vals[0], rmng_vals[1], &matter_vals[0], &matter_vals[1]);
        break;
#endif
    default:
        break;
    }
}

void rm_mirror_composite_to_rmng(const rm_mirror_composite_t *comp, const int32_t *matter_vals,
                                 int32_t *rmng_vals)
{
    switch (comp->conversion) {
#if RM_MIRROR_USES_CONV_HSV_XY
    case RM_MIRROR_COMPOSITE_CONV_HSV_XY:
        rm_mirror_color_xy_to_hs(matter_vals[0], matter_vals[1], &rmng_vals[0], &rmng_vals[1]);
        break;
#endif
    default:
        break;
    }
}
