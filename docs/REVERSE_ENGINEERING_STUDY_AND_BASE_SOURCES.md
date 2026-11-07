# Technical Dossier: Reverse Engineering Study, Provenance & Upstream Base Code

**Date of Publication**: November 2026  
**Primary Authors**: Carlos Alberto & OpenFTHD Project Contributors  
**Official Firmware Repository**: [https://github.com/programandosolucoes/open-fthd-firmware](https://github.com/programandosolucoes/open-fthd-firmware)  
**Linux Kernel Driver Repository**: [https://github.com/programandosolucoes/facetimehd](https://github.com/programandosolucoes/facetimehd) (Upstream PR #368: [patjak/bcwc_pcie#368](https://github.com/patjak/bcwc_pcie/pull/368))  
**Language Companion**: [Versão em Português (Portuguese Version)](ESTUDO_ENGENHARIA_REVERSA_E_FONTES_BASE.md)  
**License**: GNU General Public License v2.0 (GPL-2.0-only)

---

## 1. Introduction, Background & Motivation

Across Apple Mac platforms (MacBook Pro, MacBook Air, and iMac manufactured from 2013 through 2020), the integrated **FaceTime HD** camera does not operate as a standard USB Video Class (UVC) webcam. Instead, it relies on a proprietary coprocessor connected directly to the **PCI Express** bus: the **Broadcom BCM1570**.

In the open-source Linux community, the excellent [`bcwc_pcie`](https://github.com/patjak/bcwc_pcie) kernel driver maintained by Patrik Jakobsson provided foundational hardware support. However, it historically depended on an extraction script (`extract-firmware.sh`) to retrieve an Apple proprietary closed binary blob (`firmware.bin`, ~1.4 MB) from OSX install packages. This dependency introduced two critical barriers:
1. **Ineligibility for Direct In-Tree Linux Inclusion**: Being closed-source Apple intellectual property, the binary blob cannot be distributed in `linux-firmware` or the mainline Linux kernel tree.
2. **Artificial Hardware Constraints & Inability to Audit**: Apple's closed firmware restricted image capture to 1280x720 @ 30 FPS, omitted native multi-resolution profiles of the underlying 5.0 Megapixel sensor, lacked vector coprocessing offload capabilities, and precluded security or privacy auditing.

This document establishes the comprehensive clean-room reverse engineering study, the identification and integration of upstream open-source GPL base code, and the provenance trail that enabled OpenFTHD to deliver a 100% open-source alternative firmware in **Freestanding C** and **Embedded Rust `#![no_std]`**, accelerated with **128-bit ARM NEON SIMD** and a **NIST FIPS-197 AES-128** cryptographic engine.

---

## 2. Automated Clean-Room Reverse Engineering: REA & Ghidra 12.1.4

To determine the physical silicon topology without copying copyrighted proprietary routines, the original binary `/lib/firmware/facetimehd/firmware.bin` was processed through the automated **REA** (*Reverse Engineering Automation*, [`morluto/rea`](https://github.com/morluto/rea)) pipeline backed by the **NSA Ghidra v12.1.4** decompiler.

### Evidence Identifier:
`ev_399af037b91ec90834825cc893d7857c725cb4a5f4d8b60c8c035049577d93a7`

### Decompiler Inventory & Binary Statistics:
* **Target Architecture**: ARMv7 Little-Endian 32-bit (`ARM:LE:32:v7`) with VFPv4 / NEON SIMD unit.
* **Memory Map**: Linear single RAM space mapped from `0x00000000` to `0x0015c004` (1,425,412 bytes).
* **Disassembled Procedures**: 3,267 unique functions identified by standard ARM prologue sequences (`push {r4-r7, lr}`).
* **Extracted String Literals**: 1,343 unique string constants.

### Structural Topology Discovery: The 15 Apple C++ Classes
Apple's compiler retained debug assertion strings embedding original source file paths (`__FILE__`) and line numbers (`__LINE__`). This structural metadata enabled reconstructing the exact firmware module hierarchy without examining or duplicating proprietary algorithmic logic:

```
[ Embedded Operating System & Runtime Framework ffw ]
  ├── ./ffw/CEnvironment.cpp              -> Hardware abstraction layer, heap allocator, timers, IRQ
  └── ./ffw/CFSM.cpp                      -> Hierarchical Finite State Machine for system transitions

[ Host PCIe Mailbox & Ringbuffer IPC Interface ]
  ├── ./H4ISPCD/drivers/IPC/CDSControllerBase.cpp            -> Mailbox ringbuffer channel dispatcher
  ├── ./H4ISPCD/drivers/IPC/CDSControllerBaseChannelCmd.cpp  -> Host command processor (CISP_CMD_*)
  └── ./H4ISPCD/drivers/IPC/CDualStreamControllerBase.cpp    -> Dual stream capture buffer multiplexer

[ Camera Sensor & Optical Actuator Peripherals ]
  ├── ./H4ISPCD/drivers/I2C/CSMBusController.cpp            -> Internal I2C / SMBus controller
  ├── ./H4ISPCD/drivers/Sensor/CImageSensorOV5650.cpp       -> Physical driver for OmniVision OV5650
  └── ./H4ISPCD/drivers/Actuator/CAutoFocusAD5816.cpp        -> D/A driver for Analog Devices AD5816 VCM

[ Hardware ISP Pipeline (3A & Processing) ]
  ├── ./H4ISPCD/isp/CAEAFE.cpp            -> Auto-Exposure & Analog Front-End sensor gain control
  ├── ./H4ISPCD/isp/CAWB.cpp              -> Auto-White-Balance statistical estimator
  ├── ./H4ISPCD/isp/CDRC.cpp              -> Dynamic Range Compression (HDR tone curve mapping)
  ├── ./H4ISPCD/isp/CColorMatrix.cpp      -> 3x3 Color Correction Matrix (CCM)
  ├── ./H4ISPCD/isp/CGradientFilter.cpp   -> Spatial gradient filter for sharpness and noise reduction
  └── ./H4ISPCD/isp/CPhotometry.cpp       -> Auto-Exposure photometry metering (Spot, Center, Matrix)
```

### Critical Silicon Anchors & Offsets:
1. **Physical Image Sensor (OmniVision OV5650)**: Confirmed via `"./H4ISPCD/drivers/Sensor/CImageSensorOV5650.cpp"`. Physical interface: 2-lane MIPI CSI-2 and I2C 7-bit address `0x36` (8-bit write `0x6c`). Sensor matrix: 5.0 Megapixel (2592 x 1944 active pixels).
2. **Physical Focus Actuator (Analog Devices AD5816)**: Confirmed via `"./H4ISPCD/drivers/Actuator/CAutoFocusAD5816.cpp"`. I2C DAC driving the lens voice-coil motor (VCM).
3. **IPC Mailbox Registration Routine (`0x00002d00`)**: The routine at offset `0x2d00` establishes the memory ringbuffers (`0x000a3000`) and writes the channel table address to PCIe MMIO register `0xc3000` (`ISP_FW_CHAN_CTRL`).
4. **I2C Bus Error Handler (`0x00095528`)**: Recovered debug string:
   ```c
   LOG_ERR("./H4ISPCD/drivers/I2C/CSMBusController.cpp", 291, "%s: Asynchronous transaction failed!");
   ```

---

## 3. Upstream Open-Source Base Code & Reference Attribution

In response to upstream community inquiries regarding the availability of public drivers (e.g. Patrik Jakobsson's observation: *"ov5650 is not in mainline"*), we located, imported, and mirrored the public GPL base drivers directly under [`references/`](../references/README.md):

### A. OmniVision OV5650 Open-Source GPL Driver
* **Mainline Context**: While mainline Linux (`torvalds/linux`) includes OV5640, OV5645, OV5647, OV5670, and OV5695, the specific `OV5650` driver was maintained in embedded vendor Board Support Package (BSP) trees (such as Allwinner Sunxi and NVIDIA Tegra Android).
* **Source Repository**: [`allwinner-zh/linux-3.4-sunxi`](https://github.com/allwinner-zh/linux-3.4-sunxi)
* **Upstream Path**: `drivers/media/video/sunxi-vfe/device/ov5650.c` (1,708 lines, GPL-2.0) and `camera.h`
* **Local Reference Mirror**: [`references/01-sensor-ov5650/ov5650.c`](../references/01-sensor-ov5650/ov5650.c)
* **Technical Contribution**: Full 16-bit register initialization sequences for 720p@30fps, 720p@60fps, 1080p@30fps, VGA@60fps, and 5MP QSXGA, PLL multipliers, and MIPI CSI-2 timing.

### B. Linux Kernel Media Subsystem Actuators (GPL-2.0)
* **Source Repository**: [`torvalds/linux`](https://github.com/torvalds/linux)
* **Upstream Paths**:
  * `drivers/media/i2c/ad5820.c`: Driver for Analog Devices AD5816/AD5820 voice-coil focus DAC (Tuukka Toivonen, Sakari Ailus, Pavel Machek).
  * `drivers/media/i2c/dw9714.c`: Driver for linear slope current control on VCM focus actuators.
* **Local Reference Mirror**: [`references/02-actuator-vcm/`](../references/02-actuator-vcm/)

### C. PCIe Mailbox Protocol from `bcwc_pcie` (GPL-2.0)
* **Source Repository**: [`patjak/bcwc_pcie`](https://github.com/patjak/bcwc_pcie)
* **Author / Maintainer**: Patrik Jakobsson <patrik.r.jakobsson@gmail.com>
* **Files**: `fthd_isp.h`, `fthd_ringbuf.h`, `fthd_reg.h`
* **Local Reference Mirror**: [`references/03-pcie-mailbox-protocol/`](../references/03-pcie-mailbox-protocol/)
* **Technical Contribution**: Defines the ringbuffer IPC layout (`TERMINAL`, `DEBUG`, `SHAREDMALLOC`, `IO`, `BUF_H2T`, `BUF_T2H`, `IO_T2H`, `COMPUTE`) and CISP command opcodes.

### D. NIST FIPS-197 Standard for AES-128
* **Specification**: National Institute of Standards and Technology FIPS PUB 197 & SP 800-38A
* **URL**: [https://csrc.nist.gov/publications/detail/fips/197/final](https://csrc.nist.gov/publications/detail/fips/197/final)
* **Local Reference Mirror**: [`references/04-crypto-aes128/nist_fips197_aes_reference.c`](../references/04-crypto-aes128/nist_fips197_aes_reference.c)

### E. ARM Cortex-A7 NEON SIMD Architecture Standards
* **Specification**: ARM Architecture Reference Manual (DEN0013) & ARM NEON Programmers Guide (DEN0018A)
* **URL**: [https://developer.arm.com/documentation/den0018/a/](https://developer.arm.com/documentation/den0018/a/)
* **Local Reference Mirror**: [`references/05-simd-arm-neon/neon_kernels_reference.c`](../references/05-simd-arm-neon/neon_kernels_reference.c)

---

## 4. Implemented Clean-Room Firmware Architecture

Using these public references, OpenFTHD provides two independent firmware builds:

### A. Freestanding C Implementation (`src/`)
* **Toolchain**: `arm-none-eabi-gcc` (`-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=softfp -nostdlib -nostartfiles -O2`).
* **Components**:
  * `src/boot.S`: ARMv7 vector table, Supervisor/IRQ stack initialization, CP10/CP11 NEON/VFP coprocessor enablement.
  * `src/ipc.c`: 8-channel Mailbox initialization, active CISP command dispatcher (`CH_START`, `CH_STOP`, `CROP_SET`, `OUTPUT_CONFIG`, `AWB`, `DRC`, `LED`, `AE_METERING`).
  * `src/sensor.c`: OmniVision OV5650 I2C state machine with multi-resolution profile tables.
  * `src/neon_ops.c`: 128-bit NEON SIMD vector kernels (YUYV to RGB color conversion, 3x3 Sobel convolution, int16 dot product).
  * `src/aes.c`: NIST FIPS-197 AES-128 encryption and decryption engine (ECB and CBC modes).
  * `src/compute.c`: Channel 7 (`COMPUTE`) offload dispatcher.
* **Binary Footprint**: **7.2 KB** (`build/firmware.bin`).

### B. Bare-Metal Rust Implementation (`rust/`)
* **Target**: `armv7a-none-eabi` using `#![no_std]` and `#![no_main]`.
* **Components**:
  * `rust/src/entry.rs`: Vector table and assembly entry.
  * `rust/src/registers.rs`: Type-safe volatile MMIO access for Mailbox registers (`0xc3000`).
  * `rust/src/ipc.rs`: 8-channel ringbuffer manager with CISP dispatcher and Compute opcodes.
* **Binary Footprint**: **684 bytes** (`rust/target/firmware_rust.bin`).

---

## 5. Automated QEMU CI Verification Suite

All firmware builds pass 4 automated unit test suites (`make test`):
1. **C Reference QEMU Test** (`tests/test_qemu.py`): Validates that all 8 IPC mailbox channels (`TERMINAL`, `DEBUG`, `SHAREDMALLOC`, `IO`, `BUF_H2T`, `BUF_T2H`, `IO_T2H`, `COMPUTE`) initialize properly in memory. **PASS**.
2. **Rust Bare-Metal QEMU Test** (`tests/test_qemu_rust.py`): Validates that the Rust bare-metal binary boots and registers 8 channels to MMIO register `0xc3000`. **PASS**.
3. **Hardware Command Dispatcher Test** (`tests/test_commands.py`): Validates 100% of CISP hardware opcodes and state transitions. **PASS**.
4. **NEON SIMD & AES Cryptographic Test** (`tests/test_compute_neon_aes.py`): Validates NIST test vectors, CBC mode, 128-bit NEON YUYV-to-RGB, 3x3 Sobel convolution, and vector dot products. **PASS**.

---

## 6. Developer APIs & User-Space CLI Tools

* **Python SDK**: [`tools/fthd_camera_control.py`](../tools/fthd_camera_control.py) (ePTZ cropping, exposure metering, HDR, LED, and compute CLI).
* **C Library & CLI**: [`tools/c/libfthd.h`](../tools/c/libfthd.h) and [`tools/c/fthd_tool.c`](../tools/c/fthd_tool.c).
* **Rust Crate & CLI**: [`tools/rust/src/lib.rs`](../tools/rust/src/lib.rs) and [`tools/rust/src/main.rs`](../tools/rust/src/main.rs) (`fthd-ctl`).

---

## 7. Legal Compliance & Provenance Summary

| Subsystem | Upstream Reference | Origin Repository / Standard | License |
|---|---|---|---|
| **Sensor (OV5650)** | `references/01-sensor-ov5650/ov5650.c` | [allwinner-zh/linux-3.4-sunxi](https://github.com/allwinner-zh/linux-3.4-sunxi) | GPL-2.0 |
| **Actuators (AD5816/20)** | `references/02-actuator-vcm/ad5820.c` | [torvalds/linux](https://github.com/torvalds/linux) | GPL-2.0 |
| **Mailbox IPC** | `references/03-pcie-mailbox-protocol/fthd_isp.h` | [patjak/bcwc_pcie](https://github.com/patjak/bcwc_pcie) | GPL-2.0 |
| **Cryptography (AES-128)** | `references/04-crypto-aes128/nist_fips197_aes_reference.c` | [NIST FIPS-197](https://csrc.nist.gov/publications/detail/fips/197/final) | Public Domain |
| **SIMD (NEON)** | `references/05-simd-arm-neon/neon_kernels_reference.c` | [ARM DEN0018A](https://developer.arm.com/documentation/den0018/a/) | Public Standard |
| **Silicon Topology** | `references/06-reverse-engineering/rea_ghidra_decompilation_report.md` | `morluto/rea` + Ghidra 12.1.4 | Research Report |
