# Reverse Engineering Analysis & Ghidra Decompilation Report
**Toolchain**: Reverse Engineering Automation (`morluto/rea`) & NSA Ghidra 12.1.4 Backend  
**Evidence Identifier**: `ev_399af037b91ec90834825cc893d7857c725cb4a5f4d8b60c8c035049577d93a7`  
**Target Binary Analyzed**: `/lib/firmware/facetimehd/firmware.bin` (Apple Inc. Proprietary Blob)  
**Binary Size**: 1,425,412 bytes (~1.4 MB)  
**Architecture Target**: ARMv7 Little-Endian 32-bit (`ARM:LE:32:v7`)  

---

## 1. Executive Summary & Inventory

The automated binary analysis pipeline processed the complete Apple proprietary blob `firmware.bin` extracted from OSX/macOS install packages to deduce the physical hardware topology of the Broadcom BCM1570 PCIe coprocessor.

### Global Binary Statistics:
* **Memory Map**: Single linear RAM space mapped from `0x00000000` to `0x0015c004`.
* **Disassembled Procedures**: **3,267 distinct functions** identified by prologue signatures (`push {r4-r7, lr}`).
* **Extracted Symbols & Literals**: **1,343 unique string constants**.
* **Clean-Room Finding**: The binary contains debug assertions embedding original source file paths (`__FILE__`) and line numbers (`__LINE__`), revealing the internal class hierarchy without requiring decompiled logic copying.

---

## 2. Identified Apple Firmware Class Architecture

The proprietary firmware is structured around a C++ framework designated internally as **`ffw`** (FaceTime FirmWare) and **`H4ISPCD`** (H4 Image Signal Processor Camera Driver):

```
[ Embedded Operating System & Runtime ]
├── ./ffw/CEnvironment.cpp              -> Hardware abstraction layer, interrupt controllers, heap allocators
└── ./ffw/CFSM.cpp                      -> Hierarchical Finite State Machine for system transitions

[ Host PCIe Mailbox & Ringbuffer IPC ]
├── ./H4ISPCD/drivers/IPC/CDSControllerBase.cpp            -> Mailbox channel dispatcher
├── ./H4ISPCD/drivers/IPC/CDSControllerBaseChannelCmd.cpp  -> Host command processor (CISP_CMD_*)
└── ./H4ISPCD/drivers/IPC/CDualStreamControllerBase.cpp    -> Dual stream capture buffer multiplexer

[ Camera Sensor & Optical Actuator Peripherals ]
├── ./H4ISPCD/drivers/I2C/CSMBusController.cpp            -> Internal I2C / SMBus controller
├── ./H4ISPCD/drivers/Sensor/CImageSensorOV5650.cpp       -> Physical sensor driver for OmniVision OV5650
└── ./H4ISPCD/drivers/Actuator/CAutoFocusAD5816.cpp        -> Voice Coil Motor (VCM) DAC for lens focus

[ Hardware ISP Pipeline (3A & Image Processing) ]
├── ./H4ISPCD/isp/CAEAFE.cpp            -> Auto-Exposure & Analog Front-End sensor gain control
├── ./H4ISPCD/isp/CAWB.cpp              -> Auto-White-Balance statistical estimator
├── ./H4ISPCD/isp/CDRC.cpp              -> Dynamic Range Compression (HDR tone mapping)
├── ./H4ISPCD/isp/CColorMatrix.cpp      -> 3x3 Color Correction Matrix (CCM)
├── ./H4ISPCD/isp/CGradientFilter.cpp   -> Spatial edge sharpness and noise reduction
└── ./H4ISPCD/isp/CPhotometry.cpp       -> Auto-Exposure metering window aggregator
```

---

## 3. Key Reverse Engineering Findings & Offset Mapping

### 1. Physical Image Sensor Identification: OmniVision OV5650
* Identified in string table: `"./H4ISPCD/drivers/Sensor/CImageSensorOV5650.cpp"`.
* Physical Interface: MIPI CSI-2 with 2 data lanes and I2C control address `0x36` (8-bit address `0x6c`).
* Native Resolution: 5.0 Megapixel (2592 x 1944 active pixels).

### 2. Physical Focus Actuator: Analog Devices AD5816
* Identified in string table: `"./H4ISPCD/drivers/Actuator/CAutoFocusAD5816.cpp"`.
* Physical Interface: I2C DAC controlling voice-coil focus motor position.

### 3. Mailbox Channel Registration Routine (`0x00002d00`)
* Disassembly at offset `0x2d00` establishes the 7 memory ringbuffer descriptors (`BUF_H2T`, `BUF_T2H`, `IO`, `IO_T2H`, `TERMINAL`, `DEBUG`, `SHAREDMALLOC`).
* Publishes the ringbuffer table address to PCIe MMIO register `0xc3000` (`ISP_FW_CHAN_CTRL`) and queue control to `0xc3004` (`ISP_FW_QUEUE_CTRL`).

### 4. I2C Bus Driver Error Handler (`0x00095528`)
* String constant logged: `"./H4ISPCD/drivers/I2C/CSMBusController.cpp:291: %s: Asynchronous transaction failed!"`.
* Establishes the exact retry and timeout semantics required for sensor communication.

---

## 4. Clean-Room Reimplementation Strategy

OpenFTHD implements a **strict clean-room reimplementation**:
1. Zero Apple proprietary bytecode or decompiler output was copied or incorporated.
2. The physical sensor tables were taken from the open-source GPL Linux driver (`drivers/media/video/sunxi-vfe/device/ov5650.c`).
3. The IPC handshake protocol was implemented using Patrik Jakobsson's GPL driver definitions (`fthd_isp.h`).
4. Vector acceleration and cryptography were implemented using ARM NEON SIMD standards and NIST FIPS-197.
