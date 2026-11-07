# Reference: Camera Voice Coil Motor (VCM) Actuator Drivers (GPL-2.0)

## 1. Provenance & Origin
* **Component 1**: Analog Devices AD5820 DAC Auto-Focus Driver (`ad5820.c`)
  * **Repository**: `https://github.com/torvalds/linux`
  * **Path in Upstream**: `drivers/media/i2c/ad5820.c`
  * **Authors**: Tuukka Toivonen, Sakari Ailus, Pavel Machek (Nokia Corporation / Texas Instruments)
  * **License**: GNU General Public License v2.0 (GPL-2.0-only)
* **Component 2**: Dongwoon Anatech DW9714 10-bit DAC VCM Driver (`dw9714.c`)
  * **Repository**: `https://github.com/torvalds/linux`
  * **Path in Upstream**: `drivers/media/i2c/dw9714.c`
  * **Authors**: Intel Corporation / Sakari Ailus
  * **License**: GNU General Public License v2.0 (GPL-2.0)

## 2. Technical Purpose & Usage in OpenFTHD
The FaceTime HD camera hardware contains an internal Analog Devices AD5816 voice-coil motor DAC (`CAutoFocusAD5816.cpp`) controlling the lens focus position over I2C. These Linux media drivers document the register layout, linear ramp timing, and position encoding utilized for focus actuators.
