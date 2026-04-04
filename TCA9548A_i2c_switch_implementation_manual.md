# TCA9548A — Comprehensive Extraction

---

## 1. Source Documents

| # | Filename | Description | Pages | Role |
|---|----------|-------------|-------|------|
| 1 | `datasheet_TCA9548A.pdf` | TCA9548A Low-Voltage 8-Channel I2C Switch with Reset — Main Datasheet (SCPS207H, May 2012 – Revised September 2024) | 42 | PRIMARY |
| 2 | `best_practices.pdf` | Best Practices: I2C Devices on an I3C Shared Bus (SCPA067, March 2023) | 3 | SECONDARY |
| 3 | `dynamic_addressing.pdf` | I2C Dynamic Addressing (SCAA137, July 2019) | 8 | SECONDARY |
| 4 | `simplify_i2c_tree.pdf` | How to Simplify I2C Tree When Connecting Multiple Slaves to an I2C Master (SSZTC18, October 2015) | 5 | SECONDARY |
| 5 | `hot_swap.pdf` | I2C Solutions for Hot Swap Applications (SCPA058A, February 2020 – Revised January 2023) | 9 | SECONDARY |
| 6 | `choosing_devices.pdf` | Choosing the Correct I2C Device for New Designs (SLVA787, September 2016) | 12 | SECONDARY |
| 7 | `max_clock_repeater.pdf` | Maximum Clock Frequency of I2C Bus Using Repeaters (SLVA695, May 2015) | 7 | SECONDARY |
| 8 | `pass_fet.pdf` | PassFET Hang Time with TCA39306 I2C, I3C Level Translator (not directly about TCA9548A) | 9 | SECONDARY — peripheral reference only |
| 9 | `infographics.pdf` | I2C Infographics — Switching I2C Buses, GPIO Expansion, Extending I2C Buses (SLYT658) | 3 | SECONDARY |

---

## 2. Device Identity and Variants

- **Device name:** TCA9548A Low-Voltage 8-Channel I2C Switch with Reset. *(datasheet_TCA9548A.pdf, p. 1)*
- **Document number:** SCPS207H. *(datasheet_TCA9548A.pdf, p. 1)*
- **Manufacturer:** Texas Instruments. *(datasheet_TCA9548A.pdf, p. 1)*
- **Function:** 8-channel bidirectional translating I2C switch (1-to-8). *(datasheet_TCA9548A.pdf, p. 1)*
- **Automotive-qualified variant:** TCA9548A-Q1 (Q100 qualified for high-reliability automotive applications). *(datasheet_TCA9548A.pdf, p. 29)*
- **Note on TCA9548 (without "A"):** The datasheet is exclusively for the TCA9548A. No distinct TCA9548 (without "A") variant is described in any reviewed document. The POR section on page 22 references "PCA9548A" once in text — this appears to be an editorial error (the PCA9548A is an NXP device with a compatible address range). Both TCA9548A and PCA9548A share the same 0x70–0x77 address range.
- **Family context:** Part of the TI I2C switch family alongside TCA9543A (2-ch), TCA9544A (4-ch with interrupt), TCA9545A (4-ch with interrupt and reset), TCA9546A (4-ch with reset). *(choosing_devices.pdf, p. 8; infographics.pdf, p. 1)*

### Orderable Part Numbers

| Part Number | Package | Pins | Op Temp |
|-------------|---------|------|---------|
| TCA9548APWR / TCA9548APWRG4 | TSSOP (PW) | 24 | –40°C to 85°C |
| TCA9548ARGER / TCA9548ARGERG4 | VQFN (RGE) | 24 | –40°C to 85°C |
| TCA9548AMRGER / TCA9548AMRGERG4 | VQFN (RGE) | 24 | –40°C to 85°C |
| TCA9548ADGSR | VSSOP (DGS) | 24 | –40°C to 85°C |

*(datasheet_TCA9548A.pdf, pp. 1, 28–29)*

### Package Dimensions

| Package | Body Size (nom) |
|---------|-----------------|
| PW (TSSOP, 24) | 7.80 mm × 4.40 mm |
| RGE (VQFN, 24) | 4.00 mm × 4.00 mm |
| DGS (VSSOP, 24) | 6.10 mm × 3.00 mm |

*(datasheet_TCA9548A.pdf, p. 1)*

---

## 3. High-Level Functional Summary

The TCA9548A is an 8-channel, bidirectional, translating I2C/SMBus switch. A single upstream SCL/SDA pair fans out to eight downstream SC0/SD0–SC7/SD7 channel pairs. Any individual channel or any combination of channels can be simultaneously selected by writing to a single 8-bit control register via the I2C bus. *(datasheet_TCA9548A.pdf, pp. 1, 12)*

Primary use cases:
1. **Resolving I2C address conflicts** — e.g., connecting eight identical temperature sensors, each on its own channel. *(datasheet_TCA9548A.pdf, pp. 1, 12, 19)*
2. **Distributing bus capacitance** — spreading target devices across channels to keep per-channel capacitance within the 400 pF I2C limit. *(datasheet_TCA9548A.pdf, p. 19; simplify_i2c_tree.pdf, p. 2)*
3. **Voltage-level translation** — pass-gate structure allows different bus voltages (1.8 V, 2.5 V, 3.3 V, 5 V) on each channel pair using external pull-ups. *(datasheet_TCA9548A.pdf, pp. 1, 12, 21)*
4. **Bus isolation and recovery** — active-low RESET pin and POR allow recovery from stuck-bus conditions. *(datasheet_TCA9548A.pdf, pp. 1, 12, 14)*
5. **Hot insertion** — powers up with all channels deselected, no glitch on power-up, supports hot-swap insertion into live backplanes. *(datasheet_TCA9548A.pdf, p. 1; choosing_devices.pdf, p. 7; hot_swap.pdf, pp. 2–3)*
6. **I3C bus segmentation** — can be used to isolate I2C devices from an I3C bus; includes 50-ns deglitch filter on SDA and SCL. *(best_practices.pdf, pp. 1–2)*

---

## 4. Interface Summary

### I2C / SMBus Interface

| Parameter | Value | Source |
|-----------|-------|--------|
| Protocol | I2C and SMBus compatible | datasheet p. 1 |
| Bus lines | SCL (serial clock), SDA (serial data) — upstream pair | datasheet p. 3 |
| Downstream pairs | SC0/SD0 through SC7/SD7 (8 channels) | datasheet p. 3 |
| Supported modes | Standard-Mode (0–100 kHz), Fast-Mode (0–400 kHz) | datasheet pp. 1, 7, 13 |
| Fast-Mode Plus (FM+) | Not supported (max 400 kHz stated) | datasheet p. 7 |
| Address type | 7-bit I2C target address | datasheet p. 15 |
| Fixed address bits | `1110` (upper 4 bits) | datasheet p. 15 |
| Programmable address bits | A2, A1, A0 (3 hardware-selectable pins) | datasheet p. 15 |
| Address range | 0x70–0x77 (112–119 decimal) | datasheet p. 15 |
| Max devices on one bus | 8 TCA9548A devices (via 3 address pins) | datasheet p. 1 |
| Spike filter | 50-ns deglitch filter on SDA and SCL | best_practices.pdf p. 2 |
| 5-V tolerant inputs | Yes, all I/O pins | datasheet p. 1 |
| Input current (SCL, SDA, address, RESET) | –1 µA to +1 µA (leakage) | datasheet p. 6 |
| SDA output sink current (IOL) | 3 mA at VOL = 0.4 V; 6 mA at VOL = 0.6 V | datasheet p. 6 |

### Address Reference Table

| A2 | A1 | A0 | Decimal | Hex |
|----|----|----|---------|-----|
| L | L | L | 112 | 0x70 |
| L | L | H | 113 | 0x71 |
| L | H | L | 114 | 0x72 |
| L | H | H | 115 | 0x73 |
| H | L | L | 116 | 0x74 |
| H | L | H | 117 | 0x75 |
| H | H | L | 118 | 0x76 |
| H | H | H | 119 | 0x77 |

*(datasheet_TCA9548A.pdf, p. 15)*

---

## 5. Electrical and Timing Constraints Relevant to Software

### Recommended Operating Conditions

| Parameter | Condition | Min | Max | Unit |
|-----------|-----------|-----|-----|------|
| VCC | –40°C ≤ TA ≤ 85°C | 1.65 | 5.5 | V |
| VCC | 85°C < TA ≤ 125°C | 1.65 | 3.6 | V |
| VIH (SCL, SDA) | High-level input | 0.7 × VCC | 6 | V |
| VIH (A0–A2, RESET) | High-level input | 0.7 × VCC | VCC + 0.5 | V |
| VIL (SCL, SDA) | Low-level input | –0.5 | 0.3 × VCC | V |
| VIL (A0–A2, RESET) | Low-level input | –0.5 | 0.3 × VCC | V |
| TA | 3.6 V < VCC ≤ 5.5 V | –40 | 85 | °C |
| TA | 1.65 V ≤ VCC ≤ 3.6 V | –40 | 125 | °C |

*(datasheet_TCA9548A.pdf, pp. 4–5)*

### Addendum: Absolute Maximum Ratings

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| Supply voltage | VCC | –0.5 | 7 | V |
| Input voltage | VI | –0.5 | 7 | V |
| Input current | II | –20 | 20 | mA |
| Output current | IO | — | –25 | mA |
| Supply current | ICC | –100 | 100 | mA |
| Storage temperature | Tstg | –65 | 150 | °C |

*(datasheet_TCA9548A.pdf, p. 4)*

**Footnotes:**
- (1) Stresses beyond those listed under Absolute Maximum Ratings may cause permanent damage to the device. These are stress ratings only, which do not imply functional operation of the device at these or any other conditions beyond those indicated under Recommended Operating Conditions. Exposure to absolute-maximum-rated conditions for extended periods may affect device reliability.
- (2) The input negative-voltage and output voltage ratings may be exceeded if the input and output current ratings are observed.

*(datasheet_TCA9548A.pdf, p. 4)*

### Switch Characteristics

| Parameter | Condition | Min | Typ | Max | Unit |
|-----------|-----------|-----|-----|-----|------|
| RON | VCC = 4.5–5.5 V, IO = 15 mA | 4 | 10 | 20 | Ω |
| RON | VCC = 3.0–3.6 V, IO = 15 mA | 5 | 12 | 30 | Ω |
| RON | VCC = 2.3–2.7 V, IO = 10 mA | 7 | 15 | 45 | Ω |
| RON | VCC = 1.65–1.95 V, IO = 10 mA | 10 | 25 | 70 | Ω |
| Vo(sw) (pass voltage) | Vi = VCC, ISWout = –100 µA, VCC = 5 V | — | 3.6 | — | V |
| Vo(sw) | VCC = 4.5–5.5 V | 2.6 | — | 4.5 | V |
| Vo(sw) | VCC = 3.0–3.6 V | 1.6 | — | 2.8 | V |
| Vo(sw) | VCC = 2.3–2.7 V | 1.1 | — | 2.0 | V |
| Vo(sw) | VCC = 1.65–1.95 V | 0.6 | — | 1.25 | V |

*(datasheet_TCA9548A.pdf, pp. 6–7)*

### Addendum: Missing Switch Output Voltage and IOL Entries

The following individual-VCC Vo(sw) typical values were omitted from the switch table above:

| Parameter | Condition | Typ | Unit |
|-----------|-----------|-----|------|
| Vo(sw) | Vi = VCC, ISWout = –100 µA, VCC = 3.3 V | 1.9 | V |
| Vo(sw) | Vi = VCC, ISWout = –100 µA, VCC = 2.5 V | 1.5 | V |
| Vo(sw) | Vi = VCC, ISWout = –100 µA, VCC = 1.8 V | 1.1 | V |

*(datasheet_TCA9548A.pdf, p. 6)*

The IOL sink current specification has both MIN and MAX columns. The full specification is:

| Parameter | Condition | VCC | Min | Max | Unit |
|-----------|-----------|-----|-----|-----|------|
| IOL (SDA) | VOL = 0.4 V | 1.65–5.5 V | 3 | 6 | mA |
| IOL (SDA) | VOL = 0.6 V | 1.65–5.5 V | 6 | 9 | mA |

*(datasheet_TCA9548A.pdf, p. 6)*

**Critical firmware implication:** The pass-gate voltage (Vpass) is capped by VCC. VCC must be set to the lowest bus voltage in a level-translation scenario. For example, if downstream buses are 3.3 V and 2.7 V, VCC should be ≤ 2.7 V (practically set to the next lower standard, e.g., 2.5 V or match 2.7 V). *(datasheet_TCA9548A.pdf, pp. 21–22)*

### Capacitance

| Parameter | Condition | Typ | Max | Unit |
|-----------|-----------|-----|-----|------|
| Ci (A0–A2, RESET) | Input capacitance | 4 | 5 | pF |
| Cio(off) (SCL, SDA upstream) | Switch OFF | 20 | 28 | pF |
| Cio(off) (SC0–SC7, SD0–SD7) | Switch OFF, downstream | 5.5 | 7.5 | pF |

*(datasheet_TCA9548A.pdf, p. 6)*

**Note:** When channels are ON, Cio(ON) depends on internal capacitance plus external capacitance on the SCn lines. If multiple channels are enabled simultaneously, all downstream targets on all enabled channels contribute to total bus capacitance seen by the controller. *(datasheet_TCA9548A.pdf, pp. 6, 21)*

The TCA9548A off-capacitance on the I3C side is 20 pF (typical), which is significant relative to the I3C 50-pF capacitance limit. *(best_practices.pdf, p. 2)*

### Current Consumption

| Mode | fSCL | VCC | Typ | Max | Unit |
|------|------|-----|-----|-----|------|
| Operating | 400 kHz | 5.5 V | 50 | 80 | µA |
| Operating | 400 kHz | 3.6 V | 20 | 35 | µA |
| Operating | 400 kHz | 1.65 V | 6 | 10 | µA |
| Operating | 100 kHz | 5.5 V | 9 | 30 | µA |
| Operating | 100 kHz | 1.65 V | 2 | 4 | µA |
| Standby (low inputs) | — | 5.5 V | 0.2 | 2 | µA |
| Standby (low inputs) | — | 1.65 V | 0.1 | 1 | µA |
| Standby (hi/lo, 85–125°C) | — | 3.6 V | 1 | 2 | µA |

*(datasheet_TCA9548A.pdf, p. 6)*

### Addendum: Missing Current Consumption Entries

The following ICC values at VCC = 2.7 V and standby-with-high-inputs rows were omitted:

| Mode | Condition | fSCL | VCC | Typ | Max | Unit |
|------|-----------|------|-----|-----|-----|------|
| Operating | VI = VCC or GND, IO = 0 | 400 kHz | 2.7 V | 11 | 20 | µA |
| Operating | VI = VCC or GND, IO = 0 | 100 kHz | 5.5 V | — | — | µA |
| Operating | VI = VCC or GND, IO = 0 | 100 kHz | 3.6 V | 6 | 15 | µA |
| Operating | VI = VCC or GND, IO = 0 | 100 kHz | 2.7 V | 4 | 8 | µA |
| Standby (high inputs) | VI = VCC, IO = 0, –40°C ≤ TA ≤ 85°C | — | 5.5 V | 0.2 | 2 | µA |
| Standby (high inputs) | VI = VCC, IO = 0, –40°C ≤ TA ≤ 85°C | — | 3.6 V | 0.1 | 2 | µA |
| Standby (high inputs) | VI = VCC, IO = 0, –40°C ≤ TA ≤ 85°C | — | 2.7 V | 0.1 | 1 | µA |
| Standby (high inputs) | VI = VCC, IO = 0, –40°C ≤ TA ≤ 85°C | — | 1.65 V | 0.1 | 1 | µA |
| Standby (low inputs) | VI = GND, IO = 0, –40°C ≤ TA ≤ 85°C | — | 3.6 V | 0.1 | 2 | µA |
| Standby (low inputs) | VI = GND, IO = 0, –40°C ≤ TA ≤ 85°C | — | 2.7 V | 0.1 | 1 | µA |
| Standby (hi/lo, 85–125°C) | VI = VCC or GND, IO = 0 | — | 2.7 V | 0.7 | 1.5 | µA |
| Standby (hi/lo, 85–125°C) | VI = VCC or GND, IO = 0 | — | 1.65 V | 0.4 | 1 | µA |

*(datasheet_TCA9548A.pdf, p. 6)*

**Note on test conditions:** All electrical characteristics footnotes state: (1) For operation between specified voltage ranges, refer to the worst-case parameter in both applicable ranges. (2) All typical values are at nominal supply voltage (1.8-, 2.5-, 3.3-, or 5-V VCC), TA = 25°C. (3) RESET = VCC (held high) when all other input voltages, VI = GND. *(datasheet_TCA9548A.pdf, p. 7)*

### I2C Timing — Standard Mode (100 kHz)

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| Clock frequency | fSCL | 0 | 100 | kHz |
| Clock high time | tSCH | 4 | — | µs |
| Clock low time | tSCL | 4.7 | — | µs |
| Spike suppression time | tSP | — | 50 | ns |
| Data setup time | tSDS | 250 | — | ns |
| Data hold time | tSDH | 0 (1) | — | µs |
| Input rise time | tICR | — | 1000 | ns |
| Input fall time | tICF | — | 300 | ns |
| Output fall time (10–400 pF) | tOCF | — | 300 | ns |
| Bus free time (stop→start) | tBUF | 4.7 | — | µs |
| Start/repeated-start setup | tSTS | 4.7 | — | µs |
| Start/repeated-start hold | tSTH | 4 | — | µs |
| Stop condition setup | tSPS | 4 | — | µs |
| Valid-data time (H→L) | tvdL | — | 1 | µs |
| Valid-data time (L→H) | tvdH | — | 0.6 | µs |
| ACK valid time | tvd(ack) | — | 1 | µs |
| Bus capacitive load | Cb | — | 400 | pF |

*(1) Device internally provides a hold time of at least 300 ns for SDA (referred to VIH min of SCL) to bridge undefined region of falling SCL edge.*

*(datasheet_TCA9548A.pdf, pp. 7–8)*

### I2C Timing — Fast Mode (400 kHz)

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| Clock frequency | fSCL | 0 | 400 | kHz |
| Clock high time | tSCH | 0.6 | — | µs |
| Clock low time | tSCL | 1.3 | — | µs |
| Spike suppression time | tSP | — | 50 | ns |
| Data setup time | tSDS | 100 | — | ns |
| Data hold time | tSDH | 0 (1) | — | µs |
| Input rise time | tICR | 20 + 0.1×Cb | 300 | ns |
| Input fall time | tICF | 20 + 0.1×Cb | 300 | ns |
| Output fall time | tOCF | 20 + 0.1×Cb | 300 | ns |
| Bus free time (stop→start) | tBUF | 1.3 | — | µs |
| Start/repeated-start setup | tSTS | 0.6 | — | µs |
| Start/repeated-start hold | tSTH | 0.6 | — | µs |
| Stop condition setup | tSPS | 0.6 | — | µs |
| Valid-data time (H→L) | tvdL | — | 1 | µs |
| Valid-data time (L→H) | tvdH | — | 0.6 | µs |
| ACK valid time | tvd(ack) | — | 1 | µs |
| Bus capacitive load | Cb | — | 400 | pF |

*(datasheet_TCA9548A.pdf, pp. 7–8)*

### Addendum: I2C Timing Table Footnotes

- (2) Valid-data time (tvdL, tvdH) data taken using a 1-kΩ pull-up resistor and 50-pF load. *(datasheet_TCA9548A.pdf, p. 8)*
- (3) For Fast-Mode rise/fall time min formulas: Cb = total bus capacitance of one bus line in pF. *(datasheet_TCA9548A.pdf, p. 8)*

### Switching Characteristics

| Parameter | Condition | Min | Max | Unit |
|-----------|-----------|-----|-----|------|
| Propagation delay (tpd) | RON = 20 Ω, CL = 15 pF | — | 0.3 | ns |
| Propagation delay (tpd) | RON = 20 Ω, CL = 50 pF | — | 1 | ns |
| RESET time (SDA clear, trst) | RESET → SDA asserted high (stop condition) | — | 500 | ns |

*(datasheet_TCA9548A.pdf, p. 8)*

**Note:** The propagation delay is the calculated RC time constant of typical RON and specified load capacitance, driven by an ideal voltage source (zero output impedance). *(datasheet_TCA9548A.pdf, p. 8)*

### Addendum: Switching Characteristics Footnotes and Conditions

- Switching characteristics specified over recommended operating free-air temperature range, CL ≤ 100 pF. *(datasheet_TCA9548A.pdf, p. 8)*
- trst is the propagation delay measured from the time the RESET pin is first asserted low to the time the SDA pin is asserted high, signaling a stop condition. It must be a minimum of tW(L). *(datasheet_TCA9548A.pdf, p. 8)*

### Reset Timing

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| RESET low pulse duration | tW(L) | 6 | — | ns |
| Recovery time from RESET to start | tREC(STA) | 0 | — | ns |

*(datasheet_TCA9548A.pdf, p. 8)*

### Pull-Up Resistor Calculations

- **Minimum pull-up resistance:** Rp(min) = (VDPUX – VOL(max)) / IOL *(datasheet_TCA9548A.pdf, p. 21)*
- **Maximum pull-up resistance:** Rp(max) = tr / (0.8473 × Cb) *(datasheet_TCA9548A.pdf, p. 21)*
  - For fast-mode (tr = 300 ns), at Cb = 400 pF: Rp(max) ≈ 885 Ω
  - For standard-mode (tr = 1000 ns), at Cb = 400 pF: Rp(max) ≈ 2.95 kΩ
- **Rule of thumb for bus cap estimation:** ~10–15 pF per slave device on the bus. *(choosing_devices.pdf, p. 8; simplify_i2c_tree.pdf, p. 2; infographics.pdf, p. 1)*

### Addendum: Pull-Up Resistor Calculation Conditions by Voltage

The Rp(min) formula uses different VOL and IOL values depending on the pull-up reference voltage:

| Condition | VOL used | IOL used | Rp(min) Formula |
|-----------|----------|----------|------------------|
| VDPUX > 2 V | 0.4 V | 3 mA | (VDPUX – 0.4) / 3 mA |
| VDPUX ≤ 2 V | 0.2 × VDPUX | 2 mA | (0.8 × VDPUX) / 2 mA |

*(datasheet_TCA9548A.pdf, pp. 21–22, Figure 8-4 caption)*

**Bus capacitance budget formula (detailed):** Cb = Cio(OFF) of TCA9548A + capacitance of wires/connections/traces + capacitance of each individual target on a given channel. If multiple channels are activated simultaneously, each of the targets on all channels contribute to total bus capacitance. The maximum bus capacitance for fast-mode must not exceed 400 pF. *(datasheet_TCA9548A.pdf, p. 21)*

---

## 6. Power, Reset, Enable, and Startup Behavior

### Power-On Reset (POR)

- On first power-up (VCC ramping from 0 V), an internal POR circuit holds the device in reset until VCC reaches VPORR (rising threshold). *(datasheet_TCA9548A.pdf, pp. 14, 18)*
- **VPORR (rising):** Typ 1.2 V, Max 1.5 V. *(datasheet_TCA9548A.pdf, p. 6)*
- **VPORF (falling):** Typ 0.8 V, Max 1.0 V. *(datasheet_TCA9548A.pdf, p. 6)*
- When POR releases, the control register and I2C state machine initialize to default states: **all channels deselected** (control register = 0x00). *(datasheet_TCA9548A.pdf, pp. 14, 18)*
- After initial power-up, VCC must be lowered below VPORF to trigger another power-on reset cycle. *(datasheet_TCA9548A.pdf, pp. 14, 18)*
- The POR circuit resets the I2C bus logic when VCC < VPORF. *(datasheet_TCA9548A.pdf, p. 7 footnote 4)*

### Power-On Reset Supply Sequencing Requirements

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| VCC fall time | VCC_FT | 1 | 100 | ms |
| VCC rise time | VCC_RT | 0.1 | 100 | ms |
| Time to re-ramp (after VCC drops below VPORF(min) – 50 mV or to GND) | VCC_TRR | 40 | — | µs |
| VCC glitch-down level (no disruption if VCC_GW = 1 µs) | VCC_GH | 1.2 | — | V |
| Glitch width (no disruption if VCC_GH = 0.5 × VCC) | VCC_GW | — | 10 | µs |

*(datasheet_TCA9548A.pdf, p. 23)*

**Critical note:** The minimum time-to-re-ramp (VCC_TRR) is 40 µs. If VCC drops below VPORF and then ramps back up faster than 40 µs, the POR may not complete correctly. *(datasheet_TCA9548A.pdf, p. 23)*

### Startup State

- **All channels deselected** on power-up. No downstream channel is connected until the controller explicitly writes to the control register. *(datasheet_TCA9548A.pdf, pp. 1, 14, 18)*
- **No glitch on power up.** *(datasheet_TCA9548A.pdf, p. 1)*
- During hot insertion, the TCA9548A will not connect pins SD/SC[0–7] until explicitly commanded by the I2C master. This prevents downstream devices from interfering with the main SDA/SCL lines. *(choosing_devices.pdf, p. 7)*

### RESET Pin Behavior

- Active-low input. *(datasheet_TCA9548A.pdf, pp. 1, 14, 17)*
- When asserted low for minimum tW(L) = 6 ns, the device:
  1. Resets the control register to 0x00 (all channels deselected). *(datasheet_TCA9548A.pdf, pp. 14, 18)*
  2. Resets the I2C state machine. *(datasheet_TCA9548A.pdf, pp. 14, 18)*
  3. Asserts SDA high (generating a stop condition) within trst = 500 ns max. *(datasheet_TCA9548A.pdf, p. 8)*
- Recovery time from RESET deassertion to first I2C START: tREC(STA) = 0 ns minimum (i.e., can start immediately). *(datasheet_TCA9548A.pdf, p. 8)*
- RESET pin must be connected to VCC through a pull-up resistor if not used. *(datasheet_TCA9548A.pdf, pp. 3, 14)*
- RESET allows recovery if one of the downstream I2C buses gets stuck in a low state. *(datasheet_TCA9548A.pdf, pp. 1, 14)*
- The RESET function causes the same reset/initialization as POR without powering down the part. *(datasheet_TCA9548A.pdf, p. 1)*

### Power Supply Recommendations

- Operating VCC range: 1.65 V to 5.5 V. *(datasheet_TCA9548A.pdf, p. 4)*
- Bypass and decoupling capacitors recommended: a larger capacitor for short power supply glitches, and a smaller capacitor for high-frequency ripple filtering. *(datasheet_TCA9548A.pdf, p. 24)*
- For voltage translation applications, VCC must be set equal to or below the lowest bus voltage in the system. *(datasheet_TCA9548A.pdf, pp. 21–22)*

---

## 7. Pin Behavior Relevant to Firmware

### Pin Table (24-pin device)

| Pin Name | TSSOP (PW) | VQFN (RGE) | Type | Description |
|----------|------------|------------|------|-------------|
| A0 | 1 | 22 | I | Address input 0. Connect directly to VCC or GND. |
| A1 | 2 | 23 | I | Address input 1. Connect directly to VCC or GND. |
| A2 | 21 | 18 | I | Address input 2. Connect directly to VCC or GND. |
| RESET | 3 | 24 | I | Active-low reset. Pull up to VCC if unused. |
| SD0 | 4 | 1 | I/O | Serial data channel 0. Pull up to VDPU0. |
| SC0 | 5 | 2 | I/O | Serial clock channel 0. Pull up to VDPU0. |
| SD1 | 6 | 3 | I/O | Serial data channel 1. Pull up to VDPU1. |
| SC1 | 7 | 4 | I/O | Serial clock channel 1. Pull up to VDPU1. |
| SD2 | 8 | 5 | I/O | Serial data channel 2. Pull up to VDPU2. |
| SC2 | 9 | 6 | I/O | Serial clock channel 2. Pull up to VDPU2. |
| SD3 | 10 | 7 | I/O | Serial data channel 3. Pull up to VDPU3. |
| SC3 | 11 | 8 | I/O | Serial clock channel 3. Pull up to VDPU3. |
| GND | 12 | 9 | — | Ground. |
| SD4 | 13 | 10 | I/O | Serial data channel 4. Pull up to VDPU4. |
| SC4 | 14 | 11 | I/O | Serial clock channel 4. Pull up to VDPU4. |
| SD5 | 15 | 12 | I/O | Serial data channel 5. Pull up to VDPU5. |
| SC5 | 16 | 13 | I/O | Serial clock channel 5. Pull up to VDPU5. |
| SD6 | 17 | 14 | I/O | Serial data channel 6. Pull up to VDPU6. |
| SC6 | 18 | 15 | I/O | Serial clock channel 6. Pull up to VDPU6. |
| SD7 | 19 | 16 | I/O | Serial data channel 7. Pull up to VDPU7. |
| SC7 | 20 | 17 | I/O | Serial clock channel 7. Pull up to VDPU7. |
| VCC | 24 | 21 | Power | Supply voltage (1.65–5.5 V). |
| SCL | 22 | 19 | I/O | Upstream serial clock bus. Pull up to VDPUM. |
| SDA | 23 | 20 | I/O | Upstream serial data bus. Pull up to VDPUM. |

*(datasheet_TCA9548A.pdf, p. 3)*

### Key Notes on Pin Behavior

- **A0, A1, A2:** Must be connected directly to VCC or GND — no floating allowed. Input leakage is ±1 µA. Input capacitance is 4 pF typ, 5 pF max. *(datasheet_TCA9548A.pdf, pp. 3, 6)*
- **RESET:** Input leakage ±1 µA. Input capacitance 4 pF typ, 5 pF max. Must be pulled up if not actively driven. *(datasheet_TCA9548A.pdf, pp. 3, 6)*
- **All downstream SCn/SDn pins:** Each downstream channel pair must have its own pull-up resistors to VDPUX (the desired logic-high voltage for that channel). *(datasheet_TCA9548A.pdf, p. 3)*
- **VDPUX notation:** VDPUM = upstream controller reference voltage; VDPU0–VDPU7 = downstream target channel reference voltages. These may all be the same voltage or different voltages. *(datasheet_TCA9548A.pdf, p. 3)*
- **SDA sink current:** 3 mA at VOL = 0.4 V; 6 mA at VOL = 0.6 V. *(datasheet_TCA9548A.pdf, p. 6)*

---

## 8. Register Map Overview

The TCA9548A has a **single 8-bit control register** with no register address. There is no register pointer mechanism — data is written directly after the device address byte. *(datasheet_TCA9548A.pdf, pp. 16–17)*

| Offset | Name | Width | Reset Value | Access | Description |
|--------|------|-------|-------------|--------|-------------|
| (none — addressed directly) | Control Register | 8 bits | 0x00 | R/W | Each bit enables/disables the corresponding downstream channel |

*(datasheet_TCA9548A.pdf, pp. 17–18)*

---

## 9. Detailed Register and Bitfield Breakdown

### Control Register (8-bit, R/W)

| Bit | Name | Reset | Description |
|-----|------|-------|-------------|
| B0 | Channel 0 | 0 | 1 = Channel 0 (SC0/SD0) enabled; 0 = disabled |
| B1 | Channel 1 | 0 | 1 = Channel 1 (SC1/SD1) enabled; 0 = disabled |
| B2 | Channel 2 | 0 | 1 = Channel 2 (SC2/SD2) enabled; 0 = disabled |
| B3 | Channel 3 | 0 | 1 = Channel 3 (SC3/SD3) enabled; 0 = disabled |
| B4 | Channel 4 | 0 | 1 = Channel 4 (SC4/SD4) enabled; 0 = disabled |
| B5 | Channel 5 | 0 | 1 = Channel 5 (SC5/SD5) enabled; 0 = disabled |
| B6 | Channel 6 | 0 | 1 = Channel 6 (SC6/SD6) enabled; 0 = disabled |
| B7 | Channel 7 | 0 | 1 = Channel 7 (SC7/SD7) enabled; 0 = disabled |

*(datasheet_TCA9548A.pdf, pp. 17–18)*

**Key behaviors:**

1. **Multiple channels simultaneously:** Any combination of the 8 bits can be set to 1, enabling multiple channels at once. Writing 0xFF enables all 8 channels. Writing 0x00 disables all channels. *(datasheet_TCA9548A.pdf, pp. 1, 17)*
2. **Channel activation timing:** When a channel is selected, the channel becomes active **after a STOP condition** has been placed on the I2C bus. This ensures all SCn/SDn lines are in a high state at the moment of connection, so no false start/stop conditions are generated. *(datasheet_TCA9548A.pdf, p. 17; choosing_devices.pdf, p. 7)*
3. **Stop condition required:** A STOP condition must always occur immediately after the ACK cycle of the control register write. *(datasheet_TCA9548A.pdf, p. 17)*
4. **Multi-byte writes:** If multiple bytes are sent in a single write transaction, the TCA9548A saves only the **last byte received**. *(datasheet_TCA9548A.pdf, p. 17; choosing_devices.pdf, p. 7)*
5. **Power-up/reset default:** 0x00 (all channels deselected). *(datasheet_TCA9548A.pdf, pp. 14, 18)*

---

## 10. Commands and Transaction-Level Behaviors

### Write to Control Register

Transaction: `[S] [1110_A2_A1_A0_0] [ACK] [B7_B6_B5_B4_B3_B2_B1_B0] [ACK] [P]`

1. Controller sends START (S).
2. Controller sends 7-bit target address `1110 A2 A1 A0` + R/W = 0 (write).
3. TCA9548A responds with ACK.
4. Controller sends 8-bit control register value.
5. TCA9548A responds with ACK.
6. Controller sends STOP (P).
7. **After STOP, the new channel selection takes effect.**

*(datasheet_TCA9548A.pdf, pp. 16–17)*

**Note:** There is no register address byte. The first (and only) data byte after the device address is the control register value. *(datasheet_TCA9548A.pdf, p. 16)*

**Note:** There is no limit to the number of bytes sent in a write, but only the last byte is stored. *(datasheet_TCA9548A.pdf, p. 16)*

### Read Control Register

Transaction: `[S] [1110_A2_A1_A0_1] [ACK] [B7_B6_B5_B4_B3_B2_B1_B0] [NACK] [P]`

1. Controller sends START (S).
2. Controller sends 7-bit target address `1110 A2 A1 A0` + R/W = 1 (read).
3. TCA9548A responds with ACK.
4. TCA9548A transmits 8-bit control register value.
5. Controller sends NACK (to signal end of read).
6. Controller sends STOP (P).

*(datasheet_TCA9548A.pdf, p. 17)*

### Communicating with Downstream Targets

After writing the control register and issuing STOP:
1. The selected channel(s) become active.
2. Any subsequent I2C transaction on SCL/SDA is transparently passed through to all enabled SC/SD channel pairs.
3. The controller can then address downstream target devices at their native I2C addresses as if directly connected.
4. To switch channels, the controller addresses the TCA9548A again and writes a new control register value.

*(datasheet_TCA9548A.pdf, pp. 12, 16–17, 19)*

### Important: Address Conflict with TCA9548A Itself

When communicating with downstream devices, ensure that no downstream target device has an I2C address that conflicts with the TCA9548A's own address (0x70–0x77 depending on A0–A2 configuration). If a conflict exists, the TCA9548A will also respond to the transaction.

*(Implicit from datasheet_TCA9548A.pdf, p. 15; dynamic_addressing.pdf, p. 1)*

---

## 11. Initialization and Configuration Sequences

### Recommended Initialization Sequence

1. **Power-up:** Apply VCC within recommended ramp rate (0.1–100 ms rise time). Wait for VCC to reach VPORR (1.5 V max). POR automatically initializes the device with all channels deselected. *(datasheet_TCA9548A.pdf, pp. 14, 23)*

2. **Optional — Assert RESET:** If the circuit includes a RESET line, pulse it low for at least 6 ns (tW(L)) to guarantee known state. Recovery from RESET to first START is 0 ns. *(datasheet_TCA9548A.pdf, p. 8)*

3. **Verify device presence:** Read the control register (send START + address with R/W=1). Expect ACK. Returned value should be 0x00 (all channels off) after POR or RESET. *(datasheet_TCA9548A.pdf, pp. 17–18)*

4. **Enable desired channel(s):** Write the appropriate bitmask to the control register. Follow with STOP. The channel becomes active after the STOP. *(datasheet_TCA9548A.pdf, p. 17)*

5. **Communicate with downstream targets** on the selected channel(s).

### Channel Switching Sequence

1. Write new control register value to TCA9548A (can disable all first with 0x00, or directly set the new channel bitmask).
2. Send STOP.
3. New channel selection is now active.
4. Begin communication with downstream target(s) on the newly selected channel(s).

*(datasheet_TCA9548A.pdf, pp. 16–17)*

### Error Recovery Sequence

If a downstream I2C bus becomes stuck (SDA held low):

1. **Method 1 — RESET pin:** Assert RESET low for ≥ 6 ns. This resets the I2C state machine and deselects all channels. SDA is released high within 500 ns. Immediately re-initialize as needed. *(datasheet_TCA9548A.pdf, pp. 1, 8, 14)*

2. **Method 2 — Power cycle:** Lower VCC below VPORF (1.0 V max), wait minimum 40 µs (VCC_TRR), then ramp VCC back up. All channels will be deselected on POR. *(datasheet_TCA9548A.pdf, pp. 18, 23)*

*(hot_swap.pdf, p. 3: An I2C switch with reset can be used to disable all channels to regain I2C control if a stuck bus occurs after hot insertion.)*

---

## 12. Operating Modes and State Machine Behavior

### Operating Modes

The TCA9548A has two operational states:

1. **Active / Operating mode:** The I2C bus is being clocked and the device is responding to I2C transactions. Current consumption depends on fSCL and VCC. *(datasheet_TCA9548A.pdf, p. 6)*

2. **Standby mode:** No I2C activity. All inputs are static (high or low). Current consumption is very low (0.1–2 µA typical). *(datasheet_TCA9548A.pdf, p. 6)*

### I2C State Machine

- The TCA9548A contains an internal I2C state machine that monitors the upstream SCL/SDA lines. *(datasheet_TCA9548A.pdf, pp. 1, 12–14)*
- The state machine recognizes START, STOP, ACK/NACK conditions, and the 7-bit address + R/W bit pattern.
- The state machine is reset by:
  - Power-on reset (VCC cycling through VPORF). *(datasheet_TCA9548A.pdf, p. 14)*
  - Active-low RESET pin assertion (≥ 6 ns pulse). *(datasheet_TCA9548A.pdf, p. 14)*
- **Critical behavior:** Channel selection changes take effect **after the STOP condition**, not immediately upon writing the control register. This prevents false conditions on downstream buses. *(datasheet_TCA9548A.pdf, p. 17)*

### Channel Connection Model

The channels are implemented as **pass-gate FET switches** (not buffers or repeaters). This means:
- They introduce a series ON-resistance (RON = 4–70 Ω depending on VCC). *(datasheet_TCA9548A.pdf, pp. 6–7)*
- They are bidirectional — signals pass in both directions. *(datasheet_TCA9548A.pdf, p. 12)*
- They do **not** provide current drive or signal regeneration. *(implied from datasheet architecture; dynamic_addressing.pdf, p. 4)*
- The VCC pin limits the maximum voltage passed through the switch (Vpass clamping). *(datasheet_TCA9548A.pdf, pp. 1, 21)*
- **Switches are incompatible in series with current-source-type buffers** (e.g., TCA9509, TCA9800) because the buffer's current source cannot drive through the switch RON reliably. *(dynamic_addressing.pdf, p. 4)*

### Supply-Current Change at Mid-Level Inputs

When SCL or SDA is at a mid-range voltage (0.6 V or VCC – 0.6 V), additional supply current (ΔICC) of 3–20 µA is drawn. This is relevant during bus transitions. *(datasheet_TCA9548A.pdf, p. 6)*

---

## 13. Measurement / Data Path Behavior

Not applicable to this device. The TCA9548A is a bus switch with no ADC, DAC, sensor, or measurement capabilities. It transparently passes I2C signals between the upstream bus and selected downstream channels.

---

## 14. Interrupts, Alerts, Status, and Faults

### Interrupt Output

The TCA9548A **does not have an interrupt output pin**. *(datasheet_TCA9548A.pdf, p. 3 — pin table shows no INT pin)*

**Family context:** The TCA9544A and TCA9545A switch variants include interrupt inputs from downstream channels and an interrupt output. The TCA9548A does not have this feature. *(choosing_devices.pdf, p. 8)*

### Fault Recovery

- **Stuck bus (SDA held low by downstream device):** Use the RESET pin to reset the I2C state machine and release the bus. SDA is asserted high (stop condition) within trst = 500 ns max after RESET assertion. *(datasheet_TCA9548A.pdf, pp. 1, 8, 14)*
- **Hot insertion faults:** False clock edges during insertion can cause downstream device state machine corruption. The TCA9548A mitigates this by powering up with all channels deselected. The controller must explicitly enable channels after insertion. *(hot_swap.pdf, pp. 2–3; choosing_devices.pdf, p. 7)*
- **No hardware fault detection flags or alert mechanism.** Fault detection must be implemented in firmware (e.g., timeout-based detection of stuck bus via NACK monitoring). *(not stated in datasheet; inferred from absence of any fault register or interrupt)*

### SMBus Alert

Not applicable. The TCA9548A does not support SMBus Alert Response Address protocol.

---

## 15. Nonvolatile Memory / OTP / EEPROM Behavior

Not applicable to this device. The TCA9548A has no nonvolatile memory, OTP, or EEPROM. The single control register is volatile and resets to 0x00 on every power cycle or RESET assertion.

---

## 16. Special Behaviors, Caveats, and Footnotes

### Channel Activation After STOP

**Critical:** Channels do not activate immediately upon writing the control register. Activation occurs only after the STOP condition that terminates the write transaction. A STOP must occur immediately after the ACK cycle. *(datasheet_TCA9548A.pdf, p. 17)*

### Last-Byte-Wins on Multi-Byte Writes

If multiple data bytes are sent in a single write transaction (without intermediate STOP), only the **last byte** is stored in the control register. *(datasheet_TCA9548A.pdf, p. 17)*

### Simultaneous Multi-Channel Capacitance Accumulation

When multiple channels are enabled, the total bus capacitance seen by the controller is the sum of the upstream bus capacitance plus the capacitance of **all enabled downstream channels** and their attached targets. This can quickly exceed the 400 pF I2C limit. *(datasheet_TCA9548A.pdf, p. 21; simplify_i2c_tree.pdf, p. 2)*

If > ~25 slaves are on the I2C bus simultaneously (with multiple channels open), repeaters may be needed on downstream paths to maintain signal integrity. *(simplify_i2c_tree.pdf, pp. 2–3)*

### Level Translation Clamping via VCC

The pass-gate FETs limit the maximum high voltage passed to Vpass, which is determined by VCC. In voltage translation applications:
- VCC must be tied to the **lowest** bus voltage in the system.
- Each downstream bus pulls up to its own VDPUX via external resistors.
- The switch will not pass voltages above Vpass, thus clamping higher voltages. No additional protection is needed.

*(datasheet_TCA9548A.pdf, pp. 1, 21–22)*

### RON Impact on VOL

The pass-gate switches introduce RON in series with the I2C lines. This increases VOL seen by downstream devices: VOL_effective = VOL_driver + (IOL × RON). The resulting VOL must remain below VIL for proper logic-low recognition. At higher RON (low VCC), this becomes a concern, especially when combined with current-source buffers. *(dynamic_addressing.pdf, p. 4; datasheet_TCA9548A.pdf, pp. 6–7)*

### Incompatibility with Current-Source Buffers

I2C switches (including TCA9548A) are incompatible with buffers that drive via current sources (e.g., TCA9509, TCA9800) when placed on the same I2C segment. The current source cannot reliably drive logic-high through the switch's RON. Placing pull-up resistors past the switch may also interfere with the buffer's low-detection algorithm. *(dynamic_addressing.pdf, p. 4)*

### Hot Insertion Behavior

- The TCA9548A supports hot insertion into a live backplane. *(datasheet_TCA9548A.pdf, p. 1)*
- No downstream channels are connected during hot insertion until the controller explicitly enables them. *(choosing_devices.pdf, p. 7)*
- **Concerns mitigated:**
  - False clock edges from inrush current are not propagated to downstream channels (they are disconnected). *(hot_swap.pdf, p. 2; choosing_devices.pdf, p. 7)*
  - However, hot insertion can still cause glitches on the **upstream** main I2C bus.
- **Recommended hot-insertion architecture:** Place the TCA9548A on the backplane edge. Use a presence-detect signal (via GPIO expander like TCA9555) to detect card insertion, then enable the relevant channel. *(hot_swap.pdf, p. 3)*
- **Connector staggering:** GND should connect first, then VCC, then SDA/SCL (staggered male connector with 25 mil offsets). *(hot_swap.pdf, p. 4)*
- For enhanced hot-insertion protection on the external card side, use a dedicated hot-insertion buffer like TCA9511A. *(hot_swap.pdf, pp. 4–5)*

### I3C Shared Bus Usage

- The TCA9548A includes a 50-ns deglitch filter and is listed as compatible for segmenting I2C devices from an I3C bus. *(best_practices.pdf, pp. 1–2)*
- Set downstream channels to disabled during I3C communication to reduce capacitive loading. *(best_practices.pdf, p. 2)*
- Off-capacitance contribution to the I3C bus is 20 pF (significant vs. the 50-pF I3C limit). *(best_practices.pdf, p. 2)*

### ESD Ratings

| Model | Rating |
|-------|--------|
| HBM (Human Body Model) | ±2000 V |
| CDM (Charged Device Model) | ±1000 V |

*(datasheet_TCA9548A.pdf, pp. 1, 4)*

### Addendum: Missing ESD Rating and Footnotes

| Model | Rating |
|-------|--------|
| MM (Machine Model, A115-A) | 200 V |

*(datasheet_TCA9548A.pdf, p. 1)*

**ESD manufacturing safety footnotes:**
- JEDEC document JEP155 states that 500-V HBM allows safe manufacturing with a standard ESD control process. *(datasheet_TCA9548A.pdf, p. 4)*
- JEDEC document JEP157 states that 250-V CDM allows safe manufacturing with a standard ESD control process. *(datasheet_TCA9548A.pdf, p. 4)*

### Latch-Up

Latch-up performance exceeds 100 mA per JESD 78, Class II. *(datasheet_TCA9548A.pdf, p. 1)*

### Junction Temperature Limits

| Condition | Max TJ |
|-----------|--------|
| VCC ≤ 3.6 V | 130°C |
| VCC ≤ 5.5 V | 90°C |

*(datasheet_TCA9548A.pdf, p. 4)*

### Addendum: Thermal Information (Package Thermal Metrics)

| Thermal Metric | PW (TSSOP, 24) | RGE (VQFN, 24) | DGS (VSSOP, 24) | Unit |
|----------------|-----------------|-----------------|------------------|------|
| RθJA (junction-to-ambient) | 108.8 | 57.2 | 86.1 | °C/W |
| RθJC(top) (junction-to-case top) | 54.1 | 62.5 | 34.3 | °C/W |
| RθJB (junction-to-board) | 62.7 | 34.4 | 47.3 | °C/W |
| ψJT (junction-to-top characterization) | 10.9 | 3.8 | 1.5 | °C/W |
| ψJB (junction-to-board characterization) | 62.3 | 34.4 | 47.0 | °C/W |
| RθJC(bot) (junction-to-case bottom) | N/A | 15.5 | N/A | °C/W |

*(datasheet_TCA9548A.pdf, p. 5)*

### Addendum: Additional Orderable Part Information

The full orderable addendum includes additional detail on MSL rating, lead finish, and part marking:

| Part Number | Package | MSL Rating / Peak Reflow | Lead Finish | Part Marking |
|-------------|---------|--------------------------|-------------|---------------|
| TCA9548ADGSR | DGS (VSSOP, 24) | Level-1-260C-UNLIM | NIPDAU | 548A |
| TCA9548AMRGER | RGE (VQFN, 24) | Level-2-260C-1 YEAR | NIPDAU | PW548A |
| TCA9548APWR | PW (TSSOP, 24) | Level-1-260C-UNLIM | NIPDAU | PW548A |
| TCA9548ARGER | RGE (VQFN, 24) | Level-2-260C-1 YEAR | NIPDAU | PW548A |

All listed parts are RoHS compliant with NIPDAU lead finish. Variants with .B suffix and G4 suffix are also available with identical specifications. *(datasheet_TCA9548A.pdf, pp. 28–29)*

---

## 17. Recommended Polling and Control Strategy Hints from the Docs

1. **Single-channel-at-a-time is simplest:** For address-conflict resolution, enable only one channel at a time, read/write the downstream target, then switch. *(datasheet_TCA9548A.pdf, p. 19)*

2. **Distribute capacitance:** In applications with many non-conflicting targets, distribute them across channels to keep per-channel capacitance below 400 pF even when channels are enabled simultaneously. *(datasheet_TCA9548A.pdf, p. 19)*

3. **Read-before-switch not required but recommended:** Read the current control register to verify state if firmware robustness is desired. The device will always return the current channel selection. *(datasheet_TCA9548A.pdf, p. 17)*

4. **STOP between switch and communication:** Always send STOP after writing the control register, before starting communication with downstream targets. The channel is only active after STOP. *(datasheet_TCA9548A.pdf, p. 17)*

5. **RESET for stuck-bus recovery:** Use the RESET pin when a timeout-based stuck-bus condition is detected. This is faster and more reliable than power cycling. *(datasheet_TCA9548A.pdf, pp. 1, 14)*

6. **Disable all channels when idle:** Write 0x00 to the control register when no communication is needed. This minimizes bus capacitance and prevents unintended interactions between downstream devices. *(inferred from POR default behavior, datasheet_TCA9548A.pdf, p. 18)*

7. **Complex I2C tree management:** When cascading multiple switches (TCA954xA family), carefully manage software to avoid address conflicts between the switches themselves. Use different A0–A2 configurations for each switch. With 3 address pins, up to 8 TCA9548A devices can coexist on one bus = 64 downstream channels. *(dynamic_addressing.pdf, pp. 4–5; datasheet_TCA9548A.pdf, p. 1)*

8. **Presence detection for hot swap:** Use a GPIO expander (e.g., TCA9555) with interrupt to detect when an external card is inserted, then enable the corresponding channel. *(hot_swap.pdf, p. 3)*

9. **If using repeaters downstream:** Account for repeater propagation delay in maximum clock frequency calculations. At 400 kHz (Fast-Mode), adding a repeater reduces max achievable fSCL. At FM+ (1 MHz), repeaters may not allow full-speed operation. *(max_clock_repeater.pdf, pp. 1, 5–6)*

10. **Pull-up resistor selection:** Calculate Rp(min) and Rp(max) for each bus segment independently. Each downstream channel has its own pull-ups; the upstream bus has its own pull-ups. *(datasheet_TCA9548A.pdf, p. 21; infographics.pdf, p. 1)*

### Addendum: Maximum Clock Frequency with Downstream Repeaters

When using I2C repeaters (e.g., TCA9617B) downstream of the TCA9548A, the repeater propagation delay must be included in the timing budget. The maximum clock frequency formula with a repeater is:

$f_{SCL(max)} = \frac{1}{t_{VD;DAT(max)} + t_{PHL;BA} + t_{SU;DAT(min)} + t_{PHL;AB} + t_{HIGH(min)} + t_R + t_F}$

| Scenario | fSCL(max) |
|----------|----------|
| I2C FM spec without repeater | 400 kHz |
| I2C FM spec with TCA9617B propagation delays only | 402.6 kHz |
| I2C FM spec with TCA9617B propagation delays and rise/fall times | 503.6 kHz |
| I2C FM+ spec without repeater | 1000 kHz |
| I2C FM+ spec with TCA9617B propagation delays only | 778.8 kHz |
| I2C FM+ spec with TCA9617B propagation delays and rise/fall times | 872.8 kHz |

*(max_clock_repeater.pdf, pp. 5–6)*

**Key takeaway:** FM spec has timing margin for repeater propagation delays; FM+ spec does not. The 1000-kHz FM+ operation with repeaters is limited to certain loading conditions. *(max_clock_repeater.pdf, p. 6)*

### Addendum: Hot-Insertion Buffer (TCA9511A) Connection Conditions

When using a TCA9511A hot-insertion buffer on the external card side, the device checks three conditions before connecting IN-side to OUT-side:
1. Is the ENABLE pin high?
2. Are the SCLOUT or SDAOUT pins high (confirming downstream slaves powered up correctly)?
3. Has a stop condition or bus idle been detected on SCLIN or SDAIN?

All three must be met. This prevents connection during active communication and avoids propagating a stuck downstream bus to the main bus. The TCA9511A also features a 1-V pre-charge circuit to limit inrush current during hot insertion, and slew-rate-triggered rise time accelerators. *(hot_swap.pdf, pp. 4–5)*

### Addendum: Stuck Bus Recovery — Additional Details

Hot-insertion stuck bus scenarios from app notes:
- A false clock edge during insertion (when SCL is high) causes inrush current to create an extra clock pulse, putting the downstream slave out of sync. In the worst case, SDA becomes stuck low (slave waiting for final clock pulse to release SDA). *(hot_swap.pdf, p. 2)*
- A bad POR on the downstream slave (VCC ramp rate outside spec due to parasitic inductance on the hot-insertion power trace) can cause the slave to power up in an unknown state, potentially holding SDA low or clock-stretching SCL. *(hot_swap.pdf, p. 2)*
- Recovery: An I2C switch with reset (like TCA9548A) can disable all channels to regain bus control. This requires the processor to detect the stuck bus and toggle the RESET pin. *(hot_swap.pdf, p. 3)*

---

## 18. Ambiguities, Conflicts, and Missing Information

1. **PCA9548A vs TCA9548A naming confusion:** Page 22 of the datasheet references "PCA9548A" in the power-on reset section ("In the event of a glitch or data corruption, PCA9548A can be reset..."). This appears to be an editorial error. The entire document is otherwise about TCA9548A. The PCA9548A is NXP's equivalent part. *(datasheet_TCA9548A.pdf, p. 22)*

2. **Cio(ON) not specified:** The datasheet states (footnote 5, p. 6): "Cio(ON) depends on internal capacitance and external capacitance added to the SCn lines when channels(s) are ON." No explicit Cio(ON) value is provided. *(datasheet_TCA9548A.pdf, p. 6)*

3. **No explicit clock stretching mention:** The datasheet does not state whether the TCA9548A supports or performs clock stretching. Given this is a pass-gate switch (not a buffer), it likely does not stretch clocks, but this is not explicitly confirmed.

4. **No explicit general-call address behavior:** The datasheet does not state whether the TCA9548A responds to the I2C general-call address (0x00) or the I2C reset address (0x06).

5. **Repeated START behavior not explicit:** The datasheet write/read diagrams show START + STOP patterns. Whether a repeated START (Sr) can be used between a control register write and a downstream target read (without intervening STOP) is not clearly stated. Given the requirement that "a stop condition always must occur immediately after the acknowledge cycle" for channel activation, a repeated START would likely **not** activate the new channel selection. *(datasheet_TCA9548A.pdf, p. 17)*

6. **Hot insertion glitch on upstream bus:** While the TCA9548A prevents glitches on downstream buses via default-off channels, the datasheet does not explicitly address whether hot-inserting the TCA9548A itself creates a glitch on the upstream SCL/SDA bus. The "no glitch on power up" claim and the upstream off-capacitance (20–28 pF Cio(off)) are the only data points. The application note suggests additional protection (TCA9511A) may be needed for the upstream side. *(hot_swap.pdf, pp. 2–5)*

7. **Dynamic addressing of TCA9548A itself:** The datasheet does not state whether the A0/A1/A2 pins are latched at power-up or continuously sampled. The dynamic addressing app note (dynamic_addressing.pdf, p. 2) explains that latching behavior must be tested per-device, but does not specifically test the TCA9548A.

8. **No stated behavior for address NACK:** The datasheet does not describe what happens if the controller addresses the TCA9548A and the device NACKs (e.g., during POR or a bus fault).

9. **Thermal pad on VQFN:** The VQFN (RGE) package has a thermal pad visible in the pin diagram. The datasheet does not explicitly state what the thermal pad should be connected to (GND is typical). *(datasheet_TCA9548A.pdf, p. 3)*

10. **No minimum VCC ramp rate for reliable POR at first power-up:** VCC_RT max is 100 ms, min is 0.1 ms. But behavior for extremely slow ramp rates (e.g., >100 ms) is not stated.

---

## 19. Raw Implementation Checklist

### Hardware Design
- [ ] Select A0, A1, A2 pin strapping to assign a unique address (0x70–0x77) that does not conflict with any downstream target or other I2C device on the bus
- [ ] Connect A0, A1, A2 directly to VCC or GND (no floating)
- [ ] Connect RESET to VCC through pull-up resistor if not actively driven by MCU
- [ ] If RESET is MCU-driven, ensure MCU GPIO can assert low for ≥ 6 ns
- [ ] Set VCC ≤ lowest bus voltage if voltage translation is used
- [ ] Place bypass/decoupling capacitors on VCC (large + small)
- [ ] Place pull-up resistors on upstream SCL/SDA to VDPUM
- [ ] Place pull-up resistors on each downstream SCn/SDn pair to their respective VDPUn
- [ ] Calculate Rp(min) and Rp(max) for each bus segment
- [ ] Verify total bus capacitance < 400 pF per segment (including Cio(off) = 20–28 pF on upstream, 5.5–7.5 pF per downstream channel)
- [ ] If multiple channels will be enabled simultaneously, verify total accumulated capacitance is within budget
- [ ] Ensure VCC ramp rate is between 0.1 ms and 100 ms for reliable POR
- [ ] For hot-swap applications, stagger connector pins: GND first, then VCC, then SDA/SCL
- [ ] Keep I2C trace lengths short and trace widths minimal (5–10 mils) to reduce parasitic capacitance

### Firmware — Initialization
- [ ] After power-up, wait for VCC to stabilize above VPORR (1.5 V max)
- [ ] Optionally pulse RESET low for ≥ 6 ns to guarantee known state
- [ ] Read control register at device address to verify 0x00 (ACK confirms device present and initialized)
- [ ] Write desired channel selection bitmask, followed by STOP

### Firmware — Runtime
- [ ] Before communicating with a downstream target, write the appropriate channel enable bit(s)
- [ ] Always send STOP after writing the control register (channel activates only after STOP)
- [ ] If sending multiple bytes to TCA9548A in one transaction, only the last byte is stored
- [ ] Do not use repeated START between control register write and downstream target communication if expecting the new channel to be active — use STOP + START
- [ ] If multi-channel operation, account for each downstream device's capacitance contribution to the upstream bus
- [ ] Ensure no downstream target has an address conflicting with TCA9548A's own address

### Firmware — Error Handling
- [ ] Implement timeout-based stuck-bus detection (e.g., SDA held low beyond expected transaction time)
- [ ] On stuck bus: assert RESET low for ≥ 6 ns, then re-initialize
- [ ] On persistent failure: power-cycle VCC (lower below VPORF, wait ≥ 40 µs, ramp up)
- [ ] After RESET or POR, re-verify control register reads 0x00
- [ ] On hot insertion detection (via presence detect/GPIO), delay briefly, then enable the relevant channel

### Firmware — Multi-Switch Trees
- [ ] Assign unique addresses (A0–A2) to each TCA9548A in the tree
- [ ] When cascading switches, disable parent switch channels before reconfiguring child switches to avoid address conflicts
- [ ] Maximum addressable with 8 TCA9548A on one bus: 8 × 8 = 64 downstream channels
- [ ] Do not place current-source buffers (TCA9509, TCA9800) in series with switch channels

---

## 20. Source Citation Appendix

| Fact | Source | Page(s) |
|------|--------|---------|
| 8-channel bidirectional translating I2C switch | datasheet_TCA9548A.pdf | 1, 12 |
| I2C and SMBus compatible | datasheet_TCA9548A.pdf | 1 |
| Active-low RESET input | datasheet_TCA9548A.pdf | 1, 14, 17 |
| Three address pins A0/A1/A2, up to 8 devices | datasheet_TCA9548A.pdf | 1, 15 |
| Channel selection in any combination | datasheet_TCA9548A.pdf | 1, 17 |
| Power-up with all channels deselected | datasheet_TCA9548A.pdf | 1, 14, 18 |
| Level translation 1.8V/2.5V/3.3V/5V | datasheet_TCA9548A.pdf | 1, 12, 21 |
| No glitch on power up | datasheet_TCA9548A.pdf | 1 |
| Supports hot insertion | datasheet_TCA9548A.pdf | 1; choosing_devices.pdf | 7 |
| VCC range 1.65V–5.5V | datasheet_TCA9548A.pdf | 4 |
| 5V tolerant inputs | datasheet_TCA9548A.pdf | 1 |
| 0–400 kHz clock frequency | datasheet_TCA9548A.pdf | 1, 7 |
| TSSOP/VQFN/VSSOP packages | datasheet_TCA9548A.pdf | 1, 28 |
| Fixed address bits 1110 | datasheet_TCA9548A.pdf | 15 |
| Address range 0x70–0x77 | datasheet_TCA9548A.pdf | 15 |
| Control register: 8-bit, each bit = 1 channel | datasheet_TCA9548A.pdf | 17–18 |
| Channel activates after STOP | datasheet_TCA9548A.pdf | 17; choosing_devices.pdf | 7 |
| Last byte wins on multi-byte write | datasheet_TCA9548A.pdf | 17; choosing_devices.pdf | 7 |
| RESET pulse min 6 ns (tW(L)) | datasheet_TCA9548A.pdf | 8 |
| RESET recovery time 0 ns | datasheet_TCA9548A.pdf | 8 |
| RESET clears SDA within 500 ns (trst) | datasheet_TCA9548A.pdf | 8 |
| POR rising threshold VPORR typ 1.2V, max 1.5V | datasheet_TCA9548A.pdf | 6 |
| POR falling threshold VPORF typ 0.8V, max 1.0V | datasheet_TCA9548A.pdf | 6 |
| RON 4–70 Ω depending on VCC | datasheet_TCA9548A.pdf | 6–7 |
| IOL: 3 mA at 0.4V, 6 mA at 0.6V | datasheet_TCA9548A.pdf | 6 |
| Cio(off) upstream 20–28 pF | datasheet_TCA9548A.pdf | 6 |
| Cio(off) downstream 5.5–7.5 pF | datasheet_TCA9548A.pdf | 6 |
| Standby current 0.1–2 µA | datasheet_TCA9548A.pdf | 6 |
| Operating current 2–80 µA | datasheet_TCA9548A.pdf | 6 |
| VCC supply sequencing and ramp rates | datasheet_TCA9548A.pdf | 23 |
| Propagation delay 0.3–1 ns | datasheet_TCA9548A.pdf | 8 |
| Pull-up resistor formulas | datasheet_TCA9548A.pdf | 21 |
| 50-ns deglitch filter on SDA/SCL | best_practices.pdf | 2 |
| I3C bus segmentation use case | best_practices.pdf | 1–2 |
| 20 pF off-capacitance for I3C consideration | best_practices.pdf | 2 |
| Automotive variant TCA9548A-Q1 | datasheet_TCA9548A.pdf | 29 |
| Switch incompatible with current-source buffers | dynamic_addressing.pdf | 4 |
| I2C switch for address conflict resolution | dynamic_addressing.pdf | 3–4 |
| ~15 pF capacitance per slave device estimate | simplify_i2c_tree.pdf | 2 |
| Repeaters needed for >25 slaves on bus | simplify_i2c_tree.pdf | 2–3 |
| Hot insertion concerns (false clock edges, bad POR) | hot_swap.pdf | 2 |
| Discrete hot insertion with backplane switch | hot_swap.pdf | 3 |
| Presence detection with TCA9555 + INT | hot_swap.pdf | 3 |
| Staggered connector pins for hot swap | hot_swap.pdf | 4 |
| Switch vs MUX distinction | choosing_devices.pdf | 5 |
| Selection chart for TI I2C switches | choosing_devices.pdf | 8 |
| Max clock frequency calculations with repeaters | max_clock_repeater.pdf | 1–6 |
| I2C general reference / pull-up formulas | infographics.pdf | 1 |
| PCA9548A naming error in datasheet | datasheet_TCA9548A.pdf | 22 |
