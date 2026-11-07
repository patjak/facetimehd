# FacetimeHD (`bcwc_pcie`) - Modern Linux Kernel Driver for Apple FaceTime HD (Broadcom BCM1570 PCIe)

A modern, high-performance Linux kernel driver for the **Broadcom BCM1570 (PCI ID `14e4:1570`)** Image Signal Processor (ISP) PCIe webcam found in Apple MacBook Pro, MacBook Air, and iMac machines.

This updated branch brings full compatibility with modern kernels (**Linux 6.12+, 6.13+, and 7.0+**), adds first-class in-tree kernel build support (`Kconfig`), unlocks discrete multi-resolution capture, exposes on-chip hardware image processing features, and introduces direct driver tuning parameters.

---

## Highlights & New Features

### 1. Modern Kernel Compatibility (Linux 6.12+ through 7.0+)
- **Upstream VB2 Modernization**: Resolved compilation breakage caused by the removal of `.wait_prepare` and `.wait_finish` hooks from `struct vb2_ops` in Linux 6.12+. Lock synchronization is now managed safely and transparently by the videobuf2 core.
- **DKMS & In-Tree Kernel Integration**:
  - Fully integrated `Kconfig` (`CONFIG_VIDEO_FACETIMEHD`) and `Makefile` for compiling directly as part of an in-tree kernel build.
  - Modernized `dkms.conf` with dynamic parallel compilation (`-j$(nproc)`).

### 2. Multi-Resolution Enumeration & WebRTC Compatibility
- Added discrete frame size enumeration (`VIDIOC_ENUM_FRAMESIZES`) and frame interval enumeration (`VIDIOC_ENUM_FRAMEINTERVALS`).
- Direct out-of-the-box compatibility with Chromium, Firefox, OBS Studio, Zoom, and WebRTC applications at multiple standard resolutions:
  - **1080p Full HD**: 1920 x 1080 @ 30 fps
  - **720p HD**: 1280 x 720 @ 30 fps & 25 fps
  - **VGA**: 640 x 480 @ 30 fps
  - **QVGA**: 320 x 240 @ 30 fps

### 3. Lossless Hardware ePTZ (Electronic Pan-Tilt-Zoom)
- Implemented V4L2 Selection API (`VIDIOC_G_SELECTION`, `VIDIOC_S_SELECTION` for target `V4L2_SEL_TGT_CROP`).
- Applications can dynamically zoom (1.0x to 3.0x) and pan across the image sensor with **zero host CPU overhead**; the cropping and re-scaling are executed directly by the Broadcom ISP coprocessor hardware pipeline.

### 4. Advanced Hardware Exposure & HDR Controls
- **Auto-Exposure Photometry Metering (`V4L2_CID_EXPOSURE_METERING`)**:
  - `0`: Average (evaluates scene illumination evenly)
  - `1`: Center-Weighted (prioritizes center subject)
  - `2`: Spot (tight spot metering for faces against bright backgrounds)
  - `3`: Matrix (multi-zone evaluative grid - default)
- **Wide Dynamic Range / HDR (`V4L2_CID_WIDE_DYNAMIC_RANGE`)**:
  - Enables on-chip Dynamic Range Compression (DRC) and hardware tone curve adaptation to preserve highlight detail and shadow clarity.

### 5. PipeWire & Flatpak Realtime Streaming (In-Page USERPTR Support)
- **Zero-Stall PipeWire Support**: Fully resolves stream freezing in GNOME Cheese, GNOME Snapshot, OBS, and browsers running under Flatpak / PipeWire.
- **In-Page Buffer Offsets**: PipeWire allocates shared `memfd` buffers with non-zero byte offsets (0x40 to 0x100). The driver properly maps all physical pages touched by the buffer into the Apple S2 IOMMU table and communicates the byte offset directly to the ISP DMA hardware descriptors (`addr0/1/2`).
- **Chained Scatterlist Traversal**: High-definition frames (such as 720p YUYV requiring 450 pages) use chained scatterlists, which are safely iterated using `for_each_sg()`.
- **Buffer Recycling & Unwind**: Fixed buffer queue starvation in `fthd_start_streaming()` and implemented clean buffer completion on stream stop, eliminating hangs when closing the camera.
- **Low Latency Preroll**: Reduced channel start settling latency from a busy `mdelay(1000)` to non-blocking `msleep(200)`, preventing GStreamer and PipeWire pipeline preroll timeouts.

### 6. Natural Color Fidelity & Precise Macroblock Alignment
- **Native YUYV Pipeline**: Eliminates the green/magenta chroma inversion bug by advertising clean `V4L2_PIX_FMT_YUYV` and coercing requests safely.
- **Macroblock Alignment**: Applied power-of-two 8-pixel macroblock stride alignment (`ALIGN(pix->width, 8)`), matching the hardware DMA rasterizer.
- **Immediate Auto White Balance (AWB)**: Explicitly triggers hardware Auto White Balance during ISP channel setup for crisp, balanced skin tones from frame 1.

### 7. Camera Status LED Control & Energy Saving Profiles
- **Privacy Indicator Light (`V4L2_CID_ILLUMINATORS_1` / `led_mode`)**:
  - `0`: Auto (illuminated only during active video capture)
  - `1`: Force Off (stealth mode / suppresses indicator light)
  - `2`: Force On (continuously illuminated)
- **Power Saving Profiles (`power_save`)**:
  - `0`: High Performance (minimal wake-up latency, keeps ISP clocks active)
  - `1`: Balanced (default: standard clock gating and standby on idle)
  - `2`: Aggressive Low Power (deep sensor sleep `CISP_CMD_CH_SENSOR_POWER_OFF` on stream stop)

---

## Driver Module Parameters

The driver exposes runtime parameters configurable via `/etc/modprobe.d/facetimehd.conf` or dynamically via `/sys/module/facetimehd/parameters/`:

| Parameter | Type | Default | Description |
|---|---|---|---|
| `power_save` | int | `1` | Power saving profile (`0`=High Perf, `1`=Balanced, `2`=Aggressive Low Power) |
| `led_mode` | int | `0` | Camera LED indicator (`0`=Auto, `1`=Force Off, `2`=Force On) |
| `default_width` | int | `1280` | Default capture width in pixels |
| `default_height` | int | `720` | Default capture height in pixels |
| `drc_enable` | int | `0` | Default Wide Dynamic Range / HDR state (`0`=Disabled, `1`=Enabled) |
| `ae_metering` | int | `3` | Default Auto-Exposure photometry (`0`=Avg, `1`=Center, `2`=Spot, `3`=Matrix) |

### Permanent Configuration Example:
Create `/etc/modprobe.d/facetimehd.conf`:
```ini
# /etc/modprobe.d/facetimehd.conf
options facetimehd power_save=1 led_mode=0 default_width=1280 default_height=720 ae_metering=3
```

---

## Installation & Compilation

### Option A: DKMS (Recommended for Distribution Kernels)
```bash
# Clone the repository
git clone https://github.com/patjak/bcwc_pcie.git
cd bcwc_pcie

# Install via DKMS
sudo dkms add .
sudo dkms build facetimehd/0.6.14
sudo dkms install facetimehd/0.6.14

# Reload driver
sudo modprobe facetimehd
```

### Option B: Manual In-Tree / Out-of-Tree Build
```bash
# Build module for currently running kernel
make clean && make

# Install module
sudo make modules_install
sudo depmod -a
sudo modprobe facetimehd
```

---

## Usage & Verification

### 1. Inspecting Device & Controls with `v4l2-ctl`
```bash
# Query camera details and controls
v4l2-ctl -d /dev/video0 --all

# Set exposure metering to spot
v4l2-ctl -d /dev/video0 -c exposure_metering_mode=2

# Enable hardware HDR
v4l2-ctl -d /dev/video0 -c wide_dynamic_range=1

# Perform 1.5x ePTZ digital zoom
v4l2-ctl -d /dev/video0 --set-selection-target=crop,top=120,left=216,width=848,height=480

# Reset zoom to full frame
v4l2-ctl -d /dev/video0 --set-selection-target=crop,top=0,left=0,width=1280,height=720
```

### 2. Physical Hardware Validation Traces
Validated on physical **Apple MacBook Pro (Retina, 15-inch, Mid 2014 - MacBookPro11,2)** running **Linux 7.0.0-14-generic**:

```text
03:00.0 Multimedia controller [0480]: Broadcom Inc. and subsidiaries 720p FaceTime HD Camera [14e4:1570]

Driver Info:
    Driver name      : facetimehd
    Card type        : Apple Facetime HD
    Bus info         : PCI:0000:03:00.0
    Driver version   : 7.0.0
    Device Caps      : 0x05200001 (Video Capture, Read/Write, Streaming)

Crop Capability Video Capture:
    Bounds      : Left 0, Top 0, Width 1920, Height 1080
    Default     : Left 0, Top 0, Width 1920, Height 1080
    Pixel Aspect: 1/1

User Controls:
    illuminator_1 (bool) : default=0 value=0 (Camera LED indicator)

Camera Controls:
    wide_dynamic_range (bool) : default=0 value=0 (On-chip DRC / HDR)
    exposure_metering_mode (menu) : min=0 max=3 default=3 value=3 (Matrix)

Streaming & Realtime Pipeline Verification:
    # 1. Direct V4L2 MMAP Continuous Stream (90 frames @ 30 fps)
    v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=90
    <<< [90 frames captured cleanly at 30 fps, zero buffer drops]

    # 2. PipeWire & Flatpak Streaming (90 frames @ 30 fps)
    gst-launch-1.0 pipewiresrc num-buffers=90 ! videoconvert ! fakesink
    <<< [Execution ended after 0:00:03.33, 30.0 fps steady, zero stalls]

    # 3. GNOME Cheese / Snapshot Realtime Video
    flatpak run org.gnome.Cheese
    <<< [Stream active, realtime 30 fps video rendering, accurate skin tones]
```

---

## Clean-Room Reverse Engineering, Study Dossier & Upstream Base Sources

To ensure 100% legal compliance, scientific reproducibility, and provide the Linux kernel community with verifiable hardware documentation, all reverse engineering artifacts, base source codes, register maps, and clean-room firmware implementations are tracked directly in this repository and in the companion [OpenFTHD project](https://github.com/programandosolucoes/open-fthd-firmware):

- **Technical Study Dossier & Base Sources (English)**: [`docs/REVERSE_ENGINEERING_STUDY_AND_BASE_SOURCES.md`](docs/REVERSE_ENGINEERING_STUDY_AND_BASE_SOURCES.md)
- **Dossiê Técnico de Engenharia Reversa e Códigos-Base (Português)**: [`docs/ESTUDO_ENGENHARIA_REVERSA_E_FONTES_BASE.md`](docs/ESTUDO_ENGENHARIA_REVERSA_E_FONTES_BASE.md)
- **Upstream Reference Source Directory (`references/`)**: [`references/README.md`](references/README.md)
  - `references/01-sensor-ov5650/`: Open-source 1,708-line GPL OV5650 sensor driver and register tables from `allwinner-zh/linux-3.4-sunxi`.
  - `references/02-actuator-vcm/`: Upstream Linux kernel media drivers for focus actuators (`ad5820.c`, `dw9714.c`).
  - `references/03-pcie-mailbox-protocol/`: Mailbox ringbuffers and CISP command opcodes (`fthd_isp.h`).
  - `references/04-crypto-aes128/`: NIST FIPS-197 AES-128 specification and test vectors.
  - `references/05-simd-arm-neon/`: ARM NEON SIMD vector programming reference (`neon_kernels_reference.c`).
  - `references/06-reverse-engineering/`: REA & Ghidra 12.1.4 decompilation analysis report (`ev_399af037b91ec90834825cc893d7857c725cb4a5f4d8b60c8c035049577d93a7`).
- **Comprehensive Hardware Datasheet & Provenance (English)**: [`docs/DATASHEET_AND_PROVENANCE_REPORT.md`](docs/DATASHEET_AND_PROVENANCE_REPORT.md)
- **Relatório Técnico de Proveniência (Português)**: [`docs/RELATORIO_TECNICO_E_PROVENIENCIA_PT.md`](docs/RELATORIO_TECNICO_E_PROVENIENCIA_PT.md)
- **Public Domain & Clean-Room Sources Register**: [`SOURCES.md`](SOURCES.md)
- **Upstream Submission Package**: Complete kernel staging patch series and linux-firmware patch are available in [`upstream-submission/`](upstream-submission/).

---

## Developer APIs & Companion Tools

Companion developer SDKs, CLI tools, and QEMU test harnesses are provided in the [OpenFTHD Repository](https://github.com/programandosolucoes/open-fthd-firmware):
- **Python**: [`tools/fthd_camera_control.py`](https://github.com/programandosolucoes/open-fthd-firmware/blob/main/tools/fthd_camera_control.py) (Full V4L2 Control & ePTZ CLI/library)
- **C Library**: [`tools/c/libfthd.h`](https://github.com/programandosolucoes/open-fthd-firmware/blob/main/tools/c/libfthd.h) and `fthd-tool`
- **Rust Crate**: [`tools/rust/`](https://github.com/programandosolucoes/open-fthd-firmware/tree/main/tools/rust) (`fthd-ctl`)
- **Firmware Implementations**: Both Freestanding C (`src/`) and Embedded Rust `#![no_std]` (`rust/`) implementations with QEMU integration tests.

---

## License
GNU General Public License v2.0 (GPL-2.0-only).
See [COPYING](COPYING) for complete details.
