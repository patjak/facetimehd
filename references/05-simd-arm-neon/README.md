# Reference: ARM Cortex-A7 NEON SIMD Vector Acceleration

## 1. Provenance & Origin
* **Specification**: ARM Architecture Reference Manual (ARMv7-A/R DEN0013) & ARM NEON Programmers Guide (DEN0018A)
* **Publisher**: ARM Holdings plc
* **URL**: [https://developer.arm.com/documentation/den0018/a/](https://developer.arm.com/documentation/den0018/a/)
* **Hardware Unit**: BCM1570 embedded ARM Cortex-A7 with 128-bit NEON media engine & VFPv4

## 2. Technical Purpose & Usage in OpenFTHD
Documents the SIMD vector processing kernels implemented in `src/neon_ops.c`:
1. `neon_yuyv_to_rgb`: 8-pixel vectorized unzipping (`vuzp`) and fixed-point ITU-R BT.601 color conversion with saturated narrowing (`vqmovun`).
2. `neon_sobel_3x3`: 3x3 convolution spatial gradient calculation with horizontal and vertical kernel accumulation.
3. `neon_dot_product_s16`: Vectorized int16 multiply-accumulate (`vmlal.s16`) for tensor dot products.
