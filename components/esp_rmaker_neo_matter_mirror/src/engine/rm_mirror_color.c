/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_color.c
 * @brief Pure color-space conversion for the hsv_xy composite: HSV (hue 0-360,
 *        saturation 0-100, chromaticity only - V normalized out) <-> CIE 1931
 *        xy in the Matter CurrentX/CurrentY encoding (value = x * 65536).
 *
 * The path goes through sRGB primaries (D65) INCLUDING the sRGB transfer
 * function: controllers derive xy from colors picked in gamma-encoded sRGB
 * UIs, and RainMaker Neo hue/saturation drive gamma-encoded RGB LEDs, so skipping
 * gamma would skew saturation badly (sRGB S=50% is linear S~78%). This
 * matches esp-matter's own reference conversion (color_format.c) and common
 * ecosystem practice.
 *
 * All math is integer Q15 fixed point (int64 intermediates, LUT + linear
 * interpolation for the transfer function): no floats, bit-identical between
 * POSIX unit tests and target builds.
 */

#include "rm_mirror_engine.h"

/* The whole file is the hsv_xy conversion: mappings without that composite do
 * not carry the colour maths or its lookup tables. */
#if RM_MIRROR_USES_CONV_HSV_XY

#define Q15_ONE 32768

/* Matter CurrentX/CurrentY valid range is 0..0xFEFF */
#define XY_MAX 0xFEFF

/* sRGB EOTF (decode, gamma -> linear), 33 entries at u = i/32, Q15 */
static const uint16_t __eotf_lut[33] = {
    0, 79, 169, 298, 470, 690, 962, 1286, 1667, 2107, 2608, 3172, 3802, 4499,
    5265, 6103, 7014, 7999, 9061, 10201, 11420, 12720, 14103, 15570, 17122,
    18761, 20488, 22304, 24210, 26209, 28301, 30487, 32768,
};

/* sRGB OETF (encode, linear -> gamma), 33 entries at v = i/32, Q15 */
static const uint16_t __oetf_lut[33] = {
    0, 6355, 9087, 11091, 12733, 14149, 15408, 16550, 17600, 18576, 19490,
    20353, 21171, 21950, 22695, 23409, 24096, 24759, 25399, 26019, 26620,
    27203, 27771, 28324, 28863, 29389, 29903, 30405, 30897, 31379, 31851,
    32314, 32768,
};

/* The OETF is steep near 0 (its first segment above spans 0 -> 6355), which a
 * single lerp segment cannot follow: fine sub-LUT over [0, 1024], 33 entries
 * at v = i * 32, Q15 */
static const uint16_t __oetf_lut_dark[33] = {
    0, 413, 827, 1240, 1628, 1962, 2259, 2528, 2776, 3006, 3222, 3426, 3619,
    3802, 3978, 4147, 4309, 4465, 4616, 4763, 4904, 5042, 5176, 5307, 5434,
    5558, 5679, 5798, 5914, 6027, 6139, 6248, 6355,
};

/* sRGB (linear) -> XYZ, D65, rows X/Y/Z, Q15 */
static const int32_t __rgb_to_xyz[3][3] = {
    {13515, 11717, 5913},
    {6969, 23434, 2365},
    {634, 3906, 31140},
};

/* XYZ -> sRGB (linear), rows R/G/B, Q15 (coefficients >1 exceed 16 bits) */
static const int32_t __xyz_to_rgb[3][3] = {
    {106183, -50369, -16336},
    {-31761, 61473, 1362},
    {1823, -6686, 34643},
};

static int32_t __clamp_i32(int32_t val, int32_t lo, int32_t hi)
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
 * @brief LUT + linear interpolation over [0, Q15_ONE].
 */
static int32_t __lut_interp(const uint16_t *lut, int32_t val)
{
    val = __clamp_i32(val, 0, Q15_ONE);
    int32_t idx = val >> 10;             /* / 1024: 32 segments */
    int32_t frac = val & 1023;
    if (idx >= 32) {
        return lut[32];
    }
    return lut[idx] + (((int32_t)(lut[idx + 1] - lut[idx]) * frac + 512) >> 10);
}

/**
 * @brief HSV (V=1) -> gamma-encoded RGB, all channels Q15.
 */
static void __hsv_to_rgb(int32_t hue_deg, int32_t sat_pct, int32_t rgb[3])
{
    hue_deg = ((hue_deg % 360) + 360) % 360;
    int32_t s = __clamp_i32(sat_pct, 0, 100) * Q15_ONE / 100;
    int32_t sector = hue_deg / 60;
    int32_t frac = (hue_deg - sector * 60) * Q15_ONE / 60;
    int32_t p = Q15_ONE - s;
    int32_t q = Q15_ONE - (int32_t)(((int64_t)s * frac) >> 15);
    int32_t t = Q15_ONE - (int32_t)(((int64_t)s * (Q15_ONE - frac)) >> 15);
    switch (sector) {
    case 0:
        rgb[0] = Q15_ONE;
        rgb[1] = t;
        rgb[2] = p;
        break;
    case 1:
        rgb[0] = q;
        rgb[1] = Q15_ONE;
        rgb[2] = p;
        break;
    case 2:
        rgb[0] = p;
        rgb[1] = Q15_ONE;
        rgb[2] = t;
        break;
    case 3:
        rgb[0] = p;
        rgb[1] = q;
        rgb[2] = Q15_ONE;
        break;
    case 4:
        rgb[0] = t;
        rgb[1] = p;
        rgb[2] = Q15_ONE;
        break;
    default:
        rgb[0] = Q15_ONE;
        rgb[1] = p;
        rgb[2] = q;
        break;
    }
}

/**
 * @brief Gamma-encoded RGB (Q15) -> HSV hue 0-360 / saturation 0-100 (V dropped).
 */
static void __rgb_to_hsv(const int32_t rgb[3], int32_t *hue_deg, int32_t *sat_pct)
{
    int32_t max = rgb[0], min = rgb[0];
    for (int i = 1; i < 3; i++) {
        if (rgb[i] > max) {
            max = rgb[i];
        }
        if (rgb[i] < min) {
            min = rgb[i];
        }
    }
    int32_t delta = max - min;
    if (max <= 0 || delta == 0) {
        *hue_deg = 0;
        *sat_pct = 0;
        return;
    }
    *sat_pct = (int32_t)(((int64_t)delta * 100 + max / 2) / max);
    int64_t h;
    if (max == rgb[0]) {
        h = (int64_t)60 * (rgb[1] - rgb[2]) / delta;
    } else if (max == rgb[1]) {
        h = 120 + (int64_t)60 * (rgb[2] - rgb[0]) / delta;
    } else {
        h = 240 + (int64_t)60 * (rgb[0] - rgb[1]) / delta;
    }
    if (h < 0) {
        h += 360;
    }
    *hue_deg = (int32_t)(h % 360);
}

void rm_mirror_color_hs_to_xy(int32_t hue_deg, int32_t sat_pct, int32_t *out_x, int32_t *out_y)
{
    int32_t rgb[3];
    __hsv_to_rgb(hue_deg, sat_pct, rgb);

    int32_t lin[3];
    for (int i = 0; i < 3; i++) {
        lin[i] = __lut_interp(__eotf_lut, rgb[i]);
    }

    int64_t xyz[3];
    for (int row = 0; row < 3; row++) {
        xyz[row] = ((int64_t)__rgb_to_xyz[row][0] * lin[0] +
                    (int64_t)__rgb_to_xyz[row][1] * lin[1] +
                    (int64_t)__rgb_to_xyz[row][2] * lin[2]) >> 15;
    }
    int64_t sum = xyz[0] + xyz[1] + xyz[2];
    if (sum <= 0) {
        /* Black has no chromaticity; report D65 white point */
        *out_x = 20517;
        *out_y = 21578;
        return;
    }
    *out_x = (int32_t)__clamp_i32((int32_t)((xyz[0] * 65536 + sum / 2) / sum), 0, XY_MAX);
    *out_y = (int32_t)__clamp_i32((int32_t)((xyz[1] * 65536 + sum / 2) / sum), 0, XY_MAX);
}

void rm_mirror_color_xy_to_hs(int32_t x, int32_t y, int32_t *out_hue, int32_t *out_sat)
{
    x = __clamp_i32(x, 0, XY_MAX);
    y = __clamp_i32(y, 0, XY_MAX);
    /* Treat (x, y, 1-x-y) as XYZ proportions (Y scale is irrelevant: it
     * cancels in the max-channel normalization below) */
    int64_t xyz[3] = {x, y, 65536 - x - y};
    if (xyz[2] < 0) {
        xyz[2] = 0;
    }

    int64_t lin[3];
    int64_t max = 0;
    for (int row = 0; row < 3; row++) {
        lin[row] = (__xyz_to_rgb[row][0] * xyz[0] +
                    __xyz_to_rgb[row][1] * xyz[1] +
                    __xyz_to_rgb[row][2] * xyz[2]) >> 15;
        if (lin[row] < 0) {
            lin[row] = 0; /* out-of-gamut: clip to the sRGB edge */
        }
        if (lin[row] > max) {
            max = lin[row];
        }
    }
    if (max == 0) {
        *out_hue = 0;
        *out_sat = 0;
        return;
    }

    int32_t rgb[3];
    for (int row = 0; row < 3; row++) {
        int32_t norm = (int32_t)((lin[row] * Q15_ONE + max / 2) / max);
        if (norm < 1024) {
            /* fine sub-LUT segment: index step 32 */
            int32_t idx = norm >> 5;
            int32_t frac = norm & 31;
            rgb[row] = __oetf_lut_dark[idx] +
                       (((int32_t)(__oetf_lut_dark[idx + 1] - __oetf_lut_dark[idx]) * frac + 16) >> 5);
        } else {
            rgb[row] = __lut_interp(__oetf_lut, norm);
        }
    }
    __rgb_to_hsv(rgb, out_hue, out_sat);
}

#endif /* RM_MIRROR_USES_CONV_HSV_XY */
