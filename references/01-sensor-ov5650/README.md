# Reference: OmniVision OV5650 Sensor Driver (GPL-2.0)

## 1. Provenance & Origin
* **Component**: OmniVision OV5650 5-Megapixel CMOS Camera Sensor Driver
* **Source Repository**: `https://github.com/allwinner-zh/linux-3.4-sunxi`
* **File Path in Upstream**: `drivers/media/video/sunxi-vfe/device/ov5650.c` & `camera.h`
* **Author / Contributor**: `raymonxiu` / Allwinner Technology Co., Ltd.
* **License**: GNU General Public License v2.0 (GPL-2.0)
* **Direct Raw Link**: [https://raw.githubusercontent.com/allwinner-zh/linux-3.4-sunxi/master/drivers/media/video/sunxi-vfe/device/ov5650.c](https://raw.githubusercontent.com/allwinner-zh/linux-3.4-sunxi/master/drivers/media/video/sunxi-vfe/device/ov5650.c)

## 2. Technical Purpose & Usage in OpenFTHD
This open-source GPL implementation provides:
1. Full 16-bit register address map for the OmniVision OV5650 sensor (`OV5650_I2C_ADDR = 0x36`).
2. Exact sensor PLL clock multipliers, MIPI CSI-2 lane configuration (`0x3017`, `0x3018`).
3. Resolution configuration tables for 720p@30fps, 720p@60fps, 1080p@30fps, VGA@60fps, and full 5MP QSXGA.
4. Auto-Exposure Control (AEC) and Auto-Gain Control (AGC) register addresses (`0x3500`..`0x350a`).

OpenFTHD uses these verified open-source tables in `include/ov5650.h` and `src/sensor.c`.
