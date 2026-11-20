# Reference: PCIe Mailbox Protocol & Ringbuffer IPC (GPL-2.0)

## 1. Provenance & Origin
* **Component**: Broadcom BCM1570 PCIe Host-to-Device Mailbox Interface
* **Source Repository**: `https://github.com/patjak/bcwc_pcie`
* **Files**: `fthd_isp.h`, `fthd_ringbuf.h`, `fthd_reg.h`
* **Author / Maintainer**: Patrik Jakobsson <patrik.r.jakobsson@gmail.com>
* **License**: GNU General Public License v2.0 (GPL-2.0)

## 2. Technical Purpose & Usage in OpenFTHD
These files define the protocol contract between the host Linux kernel driver and the coprocessor firmware:
* **Mailbox Registers**: `ISP_FW_CHAN_CTRL (0xc3000)`, `ISP_FW_QUEUE_CTRL (0xc3004)`, `ISP_FW_SIZE (0xc3008)`, `ISP_FW_HEAP_SIZE (0xc300c)`
* **Channel Names**: `TERMINAL`, `DEBUG`, `SHAREDMALLOC`, `IO`, `BUF_H2T`, `BUF_T2H`, `IO_T2H`, `COMPUTE`
* **Command Dispatcher Opcodes**:
  * `0x0100`: `CISP_CMD_CH_START`
  * `0x0101`: `CISP_CMD_CH_STOP`
  * `0x0801`: `CISP_CMD_CH_CROP_SET` (Lossless ePTZ Digital Zoom)
  * `0x0b01`: `CISP_CMD_CH_OUTPUT_CONFIG_SET`
  * `0x0802`: `CISP_CMD_COLOR_TUNING_SET`
  * `0x0803`: `CISP_CMD_DRC_SET` (Dynamic Range Compression / HDR)
  * `0x0804`: `CISP_CMD_LED_SET` (Status Indicator LED control)
  * `0x0805`: `CISP_CMD_AE_METERING_SET` (Auto-Exposure Photometry)
