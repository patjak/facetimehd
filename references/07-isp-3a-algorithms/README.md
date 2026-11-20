# Reference Image Signal Processor (ISP) 3A Pipeline & Tone-Mapping Algorithms

**Directory**: `references/07-isp-3a-algorithms`  
**License**: GNU General Public License v2.0 or later (GPL-2.0-or-later)  
**Origin / Standard**: Linux V4L2 3A helpers, `libcamera` Image Processing Algorithms (IPA), and ISO/IEC 15444 / FIPS reference imaging models.

---

## 1. Overview & Provenance

When operating direct PCIe camera bridges such as the **Broadcom BCM1570**, raw Bayer/YUV pixel streams from the sensor (OmniVision OV5650) require real-time processing by an on-chip Image Signal Processor (ISP). In contrast to consumer USB UVC cameras that embed closed microcontrollers, OpenFTHD implements an open, mathematically sound 3A and tone-mapping pipeline.

This reference module establishes the clean-room algorithmic baseline used to engineer:
- Freestanding C firmware: [`src/isp_pipeline.c`](../../src/isp_pipeline.c)
- Bare-Metal Rust firmware: [`rust/src/isp.rs`](../../rust/src/isp.rs)
- Driver integration: [`bcwc_pcie/fthd_isp.c`](https://github.com/patjak/bcwc_pcie)

---

## 2. Mathematical Formulations

### A. Auto-Exposure Control (AEC & AGC) & Photometry Weighting
The target exposure is determined through weighted luminance feedback:

$$Y_{weighted} = \frac{\sum_{r=0}^{3} \sum_{c=0}^{3} Y(r,c) \cdot W(r,c)}{\sum_{r=0}^{3} \sum_{c=0}^{3} W(r,c)}$$

Where $W(r,c)$ represents the selected photometry weighting matrix:
1. **Average**: Uniform $1.0\times$ weight across all tiles.
2. **Center-Weighted**: Central $2\times 2$ tiles receive $8\times$ higher weighting.
3. **Spot Metering**: Only the inner $2\times 2$ region is evaluated, discarding peripheral backlighting.
4. **Matrix**: Multi-zone 4x4 evaluative distribution.

### B. Auto-White Balance (AWB) & Correlated Color Temperature (CCT)
Under the **Gray-World Assumption**, the average reflectance of a natural scene is achromatic:

$$G_R = \frac{\mu_G}{\mu_R}, \quad G_B = \frac{\mu_G}{\mu_B}$$

Temporal stability is enforced via a first-order Infinite Impulse Response (IIR) filter ($\alpha = 0.25$):

$$G_{t} = (1 - \alpha) G_{t-1} + \alpha G_{measured}$$

Correlated Color Temperature (Kelvin) is estimated from gain ratios to select between **Tungsten (~2800K)** and **Daylight (~5500K)** $3\times 3$ Color Correction Matrices (CCM).

### C. Contrast Detection Auto-Focus (CDAF) Tenengrad Sharpness Metric
Focus metric $F$ is calculated over the active focal zone by squaring the Sobel gradient operators $G_x$ and $G_y$:

$$F = \sum_{x, y} \left( G_x(x,y)^2 + G_y(x,y)^2 \right) \quad \text{for } (G_x^2 + G_y^2) > T$$

This feedback drives the **Analog Devices AD5816/AD5820** Voice Coil Motor (VCM) 10-bit DAC through coarse, fine, and locked focal positions.

### D. Dynamic Range Compression (DRC) Tone Mapping
A 256-entry monotonic non-linear S-curve lifts deep shadow detail while smoothly rolling off highlights to avoid harsh white clipping:

$$V_{out}(i) = \begin{cases} 
S_{lift} + \frac{i \cdot (K_{knee} - S_{lift})}{K_{knee}}, & i < K_{knee} \\
K_{knee} + \frac{(i - K_{knee}) \cdot (255 - K_{knee}) \cdot 3}{2 \cdot (255 - K_{knee}) + (i - K_{knee})}, & i \ge K_{knee}
\end{cases}$$

### E. Fixed-Point Bilinear Scaler (ePTZ)
Hardware cropping and electronic pan-tilt-zoom (ePTZ) are driven by 16.16 fixed-point scaling ratios:

$$R_H = \frac{W_{in} \ll 16}{W_{out}}, \quad R_V = \frac{H_{in} \ll 16}{H_{out}}$$

All line strides are aligned to **64-byte boundaries** to maximize PCIe DMA burst throughput.

---

## 3. Included Reference Code

* [`isp_3a_reference.c`](isp_3a_reference.c): ANSI C reference implementation containing all mathematical routines for AEC, AWB, Tenengrad CDAF, DRC S-curves, and bilinear scaler ratio calculations.
