/* SPDX-License-Identifier: CC0-1.0 */
/**
 * ============================================================================
 * ARM Cortex-A7 NEON SIMD Vector Programming Reference
 * ============================================================================
 *
 * Source: ARM Architecture Reference Manual (ARMv7-A/R DEN0013) &
 *         ARM NEON Programmers Guide (DEN0018A)
 * URL: https://developer.arm.com/documentation/den0018/a/
 *
 * This reference documents the vector intrinsic / assembly patterns used in
 * OpenFTHD's BCM1570 Cortex-A7 coprocessor engine:
 * 1. YUYV 4:2:2 to RGB-8:8:8 Color Conversion (BT.601 / BT.709 Matrix)
 * 2. 3x3 Spatial Gradient Sobel Convolution (Edge Detection)
 * 3. 16-element Vector Multiply-Accumulate Dot Product (vmlal.s16)
 */

#include <stdint.h>
#include <stddef.h>

/**
 * Standard ITU-R BT.601 Fixed-Point Color Space Matrix:
 * R = Y + 1.402   * (V - 128)
 * G = Y - 0.34414 * (U - 128) - 0.71414 * (V - 128)
 * B = Y + 1.772   * (U - 128)
 *
 * Fixed-point Q8 scaling (multiply by 256):
 * C_RV = 359  (1.402 * 256)
 * C_GU = 88   (0.34414 * 256)
 * C_GV = 183  (0.71414 * 256)
 * C_BU = 454  (1.772 * 256)
 */
#define FIX_Q8_RV   359
#define FIX_Q8_GU    88
#define FIX_Q8_GV   183
#define FIX_Q8_BU   454

/**
 * Sobel 3x3 Horizontal and Vertical Convolution Kernels:
 *
 * Gx = [ -1  0  +1 ]       Gy = [ -1  -2  -1 ]
 *      [ -2  0  +2 ]            [  0   0   0 ]
 *      [ -1  0  +1 ]            [ +1  +2  +1 ]
 */
const int16_t SOBEL_GX[3][3] = {
    { -1,  0,  1 },
    { -2,  0,  2 },
    { -1,  0,  1 }
};

const int16_t SOBEL_GY[3][3] = {
    { -1, -2, -1 },
    {  0,  0,  0 },
    {  1,  2,  1 }
};
