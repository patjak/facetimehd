/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * OpenFTHD Reference Implementation: 3A (AEC, AWB, CDAF) & ISP Tone Mapping
 *
 * Clean-room reference baseline synthesized from standard open-source
 * image processing algorithms (Linux V4L2 3A helpers & libcamera IPA design).
 *
 * 1. Auto-Exposure Control (AEC/AGC) & Photometry Weighting
 * 2. Auto-White Balance (AWB) via Gray-World Assumption & CCT Kelvin Estimation
 * 3. Contrast Detection Auto-Focus (CDAF) Tenengrad Sharpness Metric
 * 4. Dynamic Range Compression (DRC) Non-Linear S-Curve Tone Mapping
 * 5. 16.16 Fixed-Point Bilinear Scaler Resampling Core
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ========================================================================= */
/* 1. Auto-Exposure Control (AEC) & Photometry Weighting Matrix               */
/* ========================================================================= */

#define AEC_TARGET_LUMA 128
#define AEC_DEADBAND     4

/* Standard 4x4 Photometry Weights */
static const uint8_t aec_weights_matrix[4][4] = {
    { 1, 2, 2, 1 },
    { 2, 4, 4, 2 },
    { 2, 4, 4, 2 },
    { 1, 2, 2, 1 }
};

static const uint8_t aec_weights_center[4][4] = {
    { 1,  1,  1, 1 },
    { 1,  8,  8, 1 },
    { 1,  8,  8, 1 },
    { 1,  1,  1, 1 }
};

static const uint8_t aec_weights_spot[4][4] = {
    { 0,  0,  0, 0 },
    { 0, 16, 16, 0 },
    { 0, 16, 16, 0 },
    { 0,  0,  0, 0 }
};

uint8_t aec_compute_weighted_luma(const uint8_t luma_grid[4][4], int mode)
{
    uint32_t sum_luma = 0;
    uint32_t sum_weight = 0;

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            uint8_t w = 1;
            if (mode == 1) w = aec_weights_center[r][c];
            else if (mode == 2) w = aec_weights_spot[r][c];
            else if (mode == 3) w = aec_weights_matrix[r][c];

            sum_luma += (uint32_t)luma_grid[r][c] * w;
            sum_weight += w;
        }
    }
    return (uint8_t)(sum_luma / sum_weight);
}

void aec_update_step(uint8_t current_luma, uint32_t *exposure, uint16_t *gain,
                     uint32_t exp_min, uint32_t exp_max, uint16_t gain_max)
{
    int32_t diff = (int32_t)AEC_TARGET_LUMA - (int32_t)current_luma;
    if (diff > -AEC_DEADBAND && diff < AEC_DEADBAND)
        return; /* Within stable deadband */

    /* Proportional feedback control */
    int32_t step = diff * 8;

    if (diff > 0) {
        /* Underexposed: Increase exposure first, then analog gain */
        if (*exposure + step <= exp_max) {
            *exposure += step;
        } else {
            *exposure = exp_max;
            if (*gain + 4 <= gain_max) *gain += 4;
            else *gain = gain_max;
        }
    } else {
        /* Overexposed: Decrease analog gain first, then exposure */
        if (*gain > 16) {
            if (*gain >= 16 + 4) *gain -= 4;
            else *gain = 16;
        } else {
            if ((int32_t)*exposure + step >= (int32_t)exp_min) *exposure += step;
            else *exposure = exp_min;
        }
    }
}

/* ========================================================================= */
/* 2. Auto-White Balance (AWB) Gray-World & CCT Kelvin Estimation            */
/* ========================================================================= */

void awb_gray_world(uint64_t sum_r, uint64_t sum_g, uint64_t sum_b, uint64_t num_pixels,
                    uint16_t *gain_r, uint16_t *gain_b)
{
    if (num_pixels == 0 || sum_r == 0 || sum_g == 0 || sum_b == 0) {
        *gain_r = 256; /* 1.0x in 8.8 fixed-point */
        *gain_b = 256;
        return;
    }

    uint32_t avg_r = (uint32_t)(sum_r / num_pixels);
    uint32_t avg_g = (uint32_t)(sum_g / num_pixels);
    uint32_t avg_b = (uint32_t)(sum_b / num_pixels);

    /* Normalization: Gain_R = Avg_G / Avg_R, Gain_B = Avg_G / Avg_B */
    *gain_r = (uint16_t)(((uint64_t)avg_g * 256) / avg_r);
    *gain_b = (uint16_t)(((uint64_t)avg_g * 256) / avg_b);

    /* Clamp gains between 0.5x (128) and 3.5x (896) */
    if (*gain_r < 128) *gain_r = 128;
    if (*gain_r > 896) *gain_r = 896;
    if (*gain_b < 128) *gain_b = 128;
    if (*gain_b > 896) *gain_b = 896;
}

uint32_t awb_estimate_cct_kelvin(uint16_t gain_r, uint16_t gain_b)
{
    /*
     * Empirical Correlated Color Temperature approximation:
     * Tungsten (~2800K): High R gain needed (~1.8x - 2.5x), low B gain.
     * Daylight (~5500K - 6500K): Balanced gains (~1.0x - 1.4x).
     * Blue Sky / Shade (>7000K): Low R gain (<1.0x), high B gain.
     */
    if (gain_r == 0) return 5500;
    int32_t ratio = ((int32_t)gain_b * 1000) / (int32_t)gain_r;

    if (ratio < 600) return 2800;       /* Incandescent / Tungsten */
    if (ratio < 850) return 4000;       /* Warm Fluorescent */
    if (ratio < 1150) return 5500;      /* Standard Daylight */
    if (ratio < 1400) return 6500;      /* Overcast Sky */
    return 7500;                        /* Open Shade */
}

/* ========================================================================= */
/* 3. Contrast Detection Auto-Focus (CDAF) Tenengrad Sharpness Metric        */
/* ========================================================================= */

uint32_t cdaf_tenengrad_score(const uint8_t *gray_plane, int width, int height, int stride)
{
    /*
     * Tenengrad Focus Measure:
     * Computes the sum of squared Sobel gradient magnitudes over the central active window.
     */
    uint64_t focus_metric = 0;
    int r_start = height / 4;
    int r_end = (height * 3) / 4;
    int c_start = width / 4;
    int c_end = (width * 3) / 4;

    for (int y = r_start; y < r_end; y++) {
        const uint8_t *prev = gray_plane + (y - 1) * stride;
        const uint8_t *curr = gray_plane + y * stride;
        const uint8_t *next = gray_plane + (y + 1) * stride;

        for (int x = c_start; x < c_end; x++) {
            /* Horizontal Sobel: Gx = (TR + 2*R + BR) - (TL + 2*L + BL) */
            int32_t gx = ((int32_t)prev[x+1] + 2*(int32_t)curr[x+1] + (int32_t)next[x+1])
                       - ((int32_t)prev[x-1] + 2*(int32_t)curr[x-1] + (int32_t)next[x-1]);

            /* Vertical Sobel: Gy = (BL + 2*B + BR) - (TL + 2*T + TR) */
            int32_t gy = ((int32_t)next[x-1] + 2*(int32_t)next[x] + (int32_t)next[x+1])
                       - ((int32_t)prev[x-1] + 2*(int32_t)prev[x] + (int32_t)prev[x+1]);

            uint32_t g_sq = (uint32_t)(gx * gx + gy * gy);
            /* Thresholding to reject sensor readout noise */
            if (g_sq > 256) {
                focus_metric += g_sq;
            }
        }
    }
    return (uint32_t)(focus_metric >> 12);
}

/* ========================================================================= */
/* 4. Dynamic Range Compression (DRC) Tone Mapping LUT                       */
/* ========================================================================= */

void drc_generate_s_curve_lut(uint8_t lut[256], uint8_t knee_point, uint8_t shadow_lift)
{
    /*
     * Generates a 256-entry monotonic S-curve:
     * - Lifts deep shadows to reveal facial features in back-lit environments.
     * - Retains linearity across midtones (knee compression).
     * - Compresses highlights smoothly to avoid washed-out white clipping.
     */
    for (int i = 0; i < 256; i++) {
        uint32_t val;
        if (i < knee_point) {
            /* Shadow region: shadow_lift + non-linear boost */
            val = shadow_lift + ((uint32_t)i * (knee_point - shadow_lift)) / knee_point;
        } else {
            /* Highlight compression: smooth roll-off towards 255 */
            uint32_t remaining = 255 - knee_point;
            uint32_t x = i - knee_point;
            val = knee_point + (x * remaining * 3) / (remaining * 2 + x);
        }
        if (val > 255) val = 255;
        lut[i] = (uint8_t)val;
    }
}

/* ========================================================================= */
/* 5. 16.16 Fixed-Point Bilinear Scaler Coordinate Transformation Core       */
/* ========================================================================= */

void scaler_compute_fixed_point_ratios(uint32_t in_w, uint32_t in_h,
                                       uint32_t out_w, uint32_t out_h,
                                       uint32_t *ratio_h, uint32_t *ratio_v)
{
    /* Fixed-point 16.16 scale factor calculation */
    *ratio_h = (out_w > 0) ? ((in_w << 16) / out_w) : (1 << 16);
    *ratio_v = (out_h > 0) ? ((in_h << 16) / out_h) : (1 << 16);
}
