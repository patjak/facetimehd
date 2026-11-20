# Reference: Reverse Engineering & Binary Decompilation Dossier

## 1. Provenance & Origin
* **Tools**:
  * Reverse Engineering Automation (`morluto/rea`): [https://github.com/morluto/rea](https://github.com/morluto/rea)
  * NSA Ghidra Software Reverse Engineering Suite (v12.1.4): [https://ghidra-sre.org](https://ghidra-sre.org)
* **Analyzed Target**: `/lib/firmware/facetimehd/firmware.bin` (Apple Inc. proprietary blob)
* **Evidence Identifier**: `ev_399af037b91ec90834825cc893d7857c725cb4a5f4d8b60c8c035049577d93a7`

## 2. Technical Purpose & Usage in OpenFTHD
Provides the complete hardware topology map derived from binary inspection:
* Maps the 15 internal C++ classes (`CEnvironment`, `CFSM`, `CDSControllerBase`, `CImageSensorOV5650`, `CAutoFocusAD5816`).
* Identifies physical peripherals: OmniVision OV5650 sensor (I2C address `0x36`) and Analog Devices AD5816 lens actuator.
* Pinpoints memory offsets: IPC initialization routine (`0x00002d00`) and I2C error handler (`0x00095528`).
* Guarantees clean-room boundaries by replacing all proprietary logic with open GPL implementations.
