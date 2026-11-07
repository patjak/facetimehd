# OpenFTHD Firmware - Clean-Room Provenance & Source Attribution

## 1. Project Mission & Methodology
The goal of this project is to provide a 100% open-source, clean-room alternative firmware for the Broadcom BCM1570 PCIe Image Signal Processor (FaceTime HD camera found in Apple MacBook Pro/Air models), eliminating the requirement for proprietary extracted binary blobs (`firmware.bin`).

This project follows strict clean-room engineering principles:
- **Zero Proprietary Apple Code**: No binary code, decompiled routines, or copyrighted symbols from Apple Inc. are copied into this codebase.
- **Open Standards & Public Interfaces**: All algorithms, register addresses, and protocols implemented here are synthesized from publicly available documentation, open-source Linux kernel drivers, and interoperability interface definitions.

---

## 2. Public Code & Knowledge Attribution

### A. OmniVision OV5650 Sensor Driver (GPL-2.0)
- **Repository**: [`allwinner-zh/linux-3.4-sunxi`](https://github.com/allwinner-zh/linux-3.4-sunxi)
- **Upstream Path**: `drivers/media/video/sunxi-vfe/device/ov5650.c` (1,708 lines) & `camera.h`
- **Author**: Allwinner Technology / raymonxiu
- **Local Reference Mirror**: [`references/01-sensor-ov5650/ov5650.c`](references/01-sensor-ov5650/ov5650.c)
- **Contribution**: Complete 16-bit register sequences for 720p@30fps, 720p@60fps, 1080p@30fps, VGA@60fps, and 5MP QSXGA; PLL clock formulas and MIPI CSI-2 lane configuration.

### B. Linux Kernel Media Subsystem Actuators (GPL-2.0)
- **Repository**: [`torvalds/linux`](https://github.com/torvalds/linux)
- **Upstream Paths**:
  - `drivers/media/i2c/ad5820.c`: Analog Devices VCM DAC focus motor driver (Tuukka Toivonen, Sakari Ailus, Pavel Machek).
  - `drivers/media/i2c/dw9714.c`: Voice-coil motor lens actuator control (Intel Corporation / Sakari Ailus).
- **Local Reference Mirror**: [`references/02-actuator-vcm/`](references/02-actuator-vcm/)

### C. Linux bcwc_pcie Driver Protocol (GPL-2.0)
- **Repository**: [`patjak/bcwc_pcie`](https://github.com/patjak/bcwc_pcie)
- **Author**: Patrik Jakobsson <patrik.r.jakobsson@gmail.com>
- **Files**: `fthd_isp.h`, `fthd_ringbuf.h`, `fthd_reg.h`
- **Local Reference Mirror**: [`references/03-pcie-mailbox-protocol/`](references/03-pcie-mailbox-protocol/)
- **Protocol Elements**:
  - IPC Channels: `TERMINAL`, `DEBUG`, `SHAREDMALLOC`, `IO`, `BUF_H2T`, `BUF_T2H`, `IO_T2H`, `COMPUTE`
  - Command Opcodes: `CISP_CMD_CH_START (0x100)`, `CISP_CMD_CH_STOP (0x101)`, `CISP_CMD_CH_CROP_SET (0x801)`, `CISP_CMD_CH_OUTPUT_CONFIG_SET (0xb01)`, `CISP_CMD_DRC_SET (0x803)`, `CISP_CMD_LED_SET (0x804)`, `CISP_CMD_AE_METERING_SET (0x805)`
  - Mailbox Registers: `ISP_FW_CHAN_CTRL (0xc3000)`, `ISP_FW_QUEUE_CTRL (0xc3004)`, `ISP_FW_SIZE (0xc3008)`, `ISP_FW_HEAP_SIZE (0xc300c)`

### D. NIST FIPS-197 AES-128 Cryptographic Standard
- **Specification**: National Institute of Standards and Technology FIPS PUB 197 & SP 800-38A
- **URL**: [https://csrc.nist.gov/publications/detail/fips/197/final](https://csrc.nist.gov/publications/detail/fips/197/final)
- **Local Reference Mirror**: [`references/04-crypto-aes128/nist_fips197_aes_reference.c`](references/04-crypto-aes128/nist_fips197_aes_reference.c)

### E. ARM Cortex-A7 NEON SIMD Specification
- **Specification**: ARM Architecture Reference Manual (DEN0013) & ARM NEON Programmers Guide (DEN0018A)
- **URL**: [https://developer.arm.com/documentation/den0018/a/](https://developer.arm.com/documentation/den0018/a/)
- **Local Reference Mirror**: [`references/05-simd-arm-neon/neon_kernels_reference.c`](references/05-simd-arm-neon/neon_kernels_reference.c)

### F. Reverse Engineering & Binary Decompilation Report
- **Tools**: `morluto/rea` ([https://github.com/morluto/rea](https://github.com/morluto/rea)) + NSA Ghidra 12.1.4 ([https://ghidra-sre.org](https://ghidra-sre.org))
- **Evidence Identifier**: `ev_399af037b91ec90834825cc893d7857c725cb4a5f4d8b60c8c035049577d93a7`
- **Local Report**: [`references/06-reverse-engineering/rea_ghidra_decompilation_report.md`](references/06-reverse-engineering/rea_ghidra_decompilation_report.md)


---

## 3. License
All original firmware source code authored in this repository is licensed under the **GNU General Public License v2.0 (GPL-2.0-only)** to ensure 100% legal compatibility with the Linux kernel and `bcwc_pcie`.
