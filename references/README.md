# OpenFTHD - Reference Code Base & Reverse Engineering Repository

This directory contains the **reference code base, upstream source files, cryptographic standards, vector specifications, and reverse-engineering evidence** used to design, build, and verify OpenFTHD under strict clean-room principles.

```
references/
├── 01-sensor-ov5650/
│   ├── ov5650.c               <- 1,708-line GPL Linux driver from allwinner-zh/linux-3.4-sunxi
│   ├── camera.h               <- Sensor timing and format definitions
│   └── README.md              <- Full origin, URLs, and attribution
├── 02-actuator-vcm/
│   ├── ad5820.c               <- Linux kernel media driver for Analog Devices VCM focus DAC
│   ├── dw9714.c               <- Linux kernel media driver for autofocus voice-coil motor
│   └── README.md              <- Full origin, URLs, and attribution
├── 03-pcie-mailbox-protocol/
│   ├── fthd_isp.h             <- Patrik Jakobsson's bcwc_pcie mailbox protocol & opcodes
│   ├── fthd_ringbuf.h         <- Ringbuffer memory descriptors and queues
│   ├── fthd_reg.h             <- BCM1570 PCIe MMIO register definitions
│   └── README.md              <- Full origin, URLs, and attribution
├── 04-crypto-aes128/
│   ├── nist_fips197_aes_reference.c <- NIST FIPS-197 official test vectors and Rcon tables
│   └── README.md              <- NIST FIPS-197 / SP 800-38A standard reference
├── 05-simd-arm-neon/
│   ├── neon_kernels_reference.c     <- ARM NEON SIMD vector color space & Sobel convolution
│   └── README.md              <- ARMv7-A Architecture & DEN0018A documentation
├── 06-reverse-engineering/
│   ├── rea_ghidra_decompilation_report.md <- Ghidra 12.1.4 analysis (Evidence ev_399af037...)
│   └── README.md              <- Hardware topology and 15 Apple C++ classes inventory
└── 07-isp-3a-algorithms/
    ├── isp_3a_reference.c     <- Standard Linux V4L2 & libcamera 3A algorithms (AEC, AWB, CDAF, DRC)
    └── README.md              <- Mathematical formulations and provenance
```

---

## Provenance Summary Table

| Subsystem | Upstream Reference File | Original Repository / Source | License |
|---|---|---|---|
| **Sensor (OV5650)** | `references/01-sensor-ov5650/ov5650.c` | [allwinner-zh/linux-3.4-sunxi](https://github.com/allwinner-zh/linux-3.4-sunxi) | GPL-2.0 |
| **Actuator (AD5816/20)** | `references/02-actuator-vcm/ad5820.c` | [torvalds/linux](https://github.com/torvalds/linux/blob/master/drivers/media/i2c/ad5820.c) | GPL-2.0 |
| **Mailbox IPC** | `references/03-pcie-mailbox-protocol/fthd_isp.h` | [patjak/bcwc_pcie](https://github.com/patjak/bcwc_pcie) | GPL-2.0 |
| **Crypto (AES-128)** | `references/04-crypto-aes128/nist_fips197_aes_reference.c` | [NIST FIPS-197](https://csrc.nist.gov/publications/detail/fips/197/final) | Public Domain |
| **SIMD (NEON)** | `references/05-simd-arm-neon/neon_kernels_reference.c` | [ARM DEN0018A](https://developer.arm.com/documentation/den0018/a/) | Public Standard |
| **Binary Topology** | `references/06-reverse-engineering/rea_ghidra_decompilation_report.md` | `morluto/rea` + Ghidra 12.1.4 | Research Report |
| **ISP 3A Algorithms** | `references/07-isp-3a-algorithms/isp_3a_reference.c` | Linux V4L2 3A & `libcamera` IPA models | GPL-2.0+ |
