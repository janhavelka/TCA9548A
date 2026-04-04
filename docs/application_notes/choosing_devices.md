# Choosing the Correct I2C Device for New Designs
**Source:** choosing_devices.pdf | **Doc #:** SLVA787 | **Pages:** 12

## Key Takeaways
- I2C switches resolve address conflicts by isolating same-address slaves on separate channels; switches can enable multiple channels simultaneously (unlike a MUX which selects one)
- I2C buffers/repeaters solve bus capacitance overloading — the I2C spec limits bus capacitance to 400 pF; estimate ~10 pF per slave device
- TCA9548A is an 8-channel switch with 3 address pins (8 unique addresses), level shifting, hot insertion support, and a reset pin
- Buffers with static voltage offset (TCA9517/A, TCA9617/A/B) cannot be placed in series B-B due to low-signal recognition failure
- Pull-up resistor sizing: Rpu(min) = (VCC − VOL) / IOL; Rpu(max) = tr / (0.8473 × Cb)

## Summary
This application note provides a selection guide across three categories of I2C infrastructure devices: I/O expanders, I2C switches, and I2C buffers/repeaters, plus standalone voltage translators.

**I/O Expanders** (TCA9554, TCA6408A, TCA9555, TCA6416A, etc.) add GPIO ports via I2C when the processor lacks sufficient pins. Key selection criteria include channel count (4–24 bit), reset pin availability, internal pull-ups, address pin count (0–3), and level-shifting capability.

**I2C Switches** (TCA9543A, TCA9544A, TCA9545A, TCA9546A, TCA9548A) fan out the I2C bus into multiple downstream channels controlled via a single-byte control register. Each bit enables/disables a channel. Channels become active only after a STOP condition, preventing false bus conditions at connection time. All TI I2C switches support hot insertion — no downstream channels connect at power-up until the master explicitly enables them.

**I2C Buffers/Repeaters** (TCA9517/A, TCA9617A/B, TCA9509, P82B96, P82B715) isolate bus capacitance, splitting one overloaded bus into two segments each supporting up to 400 pF. Certain buffers support up to 4000 pF on the slave side (P82B96, P82B715). Buffers with static voltage offset on the B-side cannot be cascaded B-B.

## Technical Details
**I2C Switch selection matrix:**

| Device | Channels | Address Pins | Reset | Interrupt | Level Shift |
|---|---|---|---|---|---|
| TCA9543A | 2 | 1 | — | — | Yes |
| TCA9544A | 4 | 2 | — | Yes | Yes |
| TCA9545A | 4 | 2 | Yes | Yes | Yes |
| TCA9546A | 4 | 2 | Yes | — | Yes |
| TCA9548A | 8 | 3 | Yes | — | Yes |

**Switch control register operation:**
- Write a single command byte after the address byte; each bit maps to a channel (SCn/SDn)
- Bit = 1 → channel selected; multiple channels can be active simultaneously
- Channel activation occurs after STOP condition to avoid false start/stop conditions
- If multiple bytes are received, only the last byte is stored

**I2C buffer selection criteria:**
- 400 kHz support: TCA9517/A, TCA9509, TCA4311A, PCA9515A/B, PCA9518
- 1 MHz support: TCA9617A/B
- Series-connectable: PCA9515B, TCA4311A, TCA9517, TCA9509
- Slave-side capacitance: 400 pF (most), 3000 pF (P82B715), 4000 pF (P82B96)

**Pull-up resistor formulas:**
- Rpu(min) = (VCC − VOL) / IOL
- Rpu(max) = tr / (0.8473 × Cb)
- Approximate ~10 pF per slave device for bus capacitance estimation

**Level shifting:** Switches allow per-channel voltage translation (e.g., 1.8 V master to 3.3 V slave). VCC must equal the lowest bus supply voltage the switch will see. Standalone translators (PCA9306, TCA9406) provide level shifting without buffering.

## Relevance to TCA9548A Implementation
The TCA9548A is the highest-channel-count switch in TI's portfolio (8 channels) with 3 address pins providing 8 unique addresses. Its control register design (1 byte = 8 channel bits) makes driver implementation straightforward — a single I2C write selects any combination of channels. The hot-insertion behavior (no channels connected at power-up) means the driver's initialization routine does not need to explicitly disable channels, but should verify the control register state after reset. Level shifting between the upstream bus (VCC) and each per-channel VCCA voltage rail enables mixed-voltage system designs.
