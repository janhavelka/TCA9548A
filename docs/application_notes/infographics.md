# I2C System Management Infographics
**Source:** infographics.pdf | **Doc #:** SLYT658 | **Pages:** 3

## Key Takeaways
- I2C bus capacitance must stay below 400 pF per segment; estimate ~15 pF per slave device
- Pull-up resistor bounds: Rpu(min) = (VCC − VOL) / IOL; Rpu(max) = tr / (0.8473 × Cb)
- Four main I2C infrastructure categories: repeaters/buffers, I/O expanders, switches, and special function devices
- I2C switches (TCA9548A, TCA9546A, TCA9543A) solve address conflicts and provide level shifting; repeaters (TCA9517, TCA9617B, P82B715) extend bus capacitance beyond 400 pF

## Summary
This infographic provides a quick-reference overview of the I2C ecosystem and TI's device portfolio for system management and control. The I2C bus uses two lines (SDA and SCL) with pull-up resistors to VCC. Key electrical parameters vary between Standard Mode (100 kHz) and Fast Mode (400 kHz).

The four infrastructure device categories address distinct needs: **Repeaters/buffers** (static offset buffer, hot-swappable buffer, bus extender, level shifter) extend bus reach. **I/O expanders** (4/8/16/24-bit, level-shifting, open-drain or push-pull) add GPIO capacity. **Switches** (1:2, 1:4, 1:8 with level shifting and interrupt switching) isolate bus segments and resolve address conflicts. **Special function** devices (LED drivers, keyboard scanners) provide application-specific I2C endpoints.

## Technical Details
**I2C electrical specifications:**

| Parameter | Standard Mode | Fast Mode |
|---|---|---|
| VIL | −0.5 to 0.3×VCC | −0.5 to 0.3×VCC |
| VIH | 0.7×VCC to VCC+0.5 | 0.7×VCC to VCC+0.5 |
| VOL (VCC > 2 V) | 0 to 0.4 V | 0 to 0.4 V |
| VOL (VCC ≤ 2 V) | — | 0 to 0.2×VDD |
| IOL @ VOL=0.4 V | 3 mA | 3 mA |
| IOL @ VOL=0.6 V | — | 6 mA |
| fSCL | 0–100 kHz | 0–400 kHz |
| Rise time (tr) | ≤ 1000 ns | 20–300 ns |
| Fall time (tf) | ≤ 300 ns | 20×(VCC/5.5V)–300 ns |

**Pull-up resistor sizing:**
- Rpu(min) = (VCC − VOL) / IOL
- Rpu(max) = tr / (0.8473 × Cb(SDA/SCL))
- Bus capacitance estimation: ~15 pF per device added to the bus

**Key product families:**

| Category | Key Devices | Function |
|---|---|---|
| Repeaters | PCA9306, TCA9617A, P82B96 | Bus extension, level shifting |
| I/O Expanders | TCA6408A, TCA6416A, TCA6424A | GPIO expansion via I2C |
| Switches | TCA9548A, TCA9546A, TCA9543A | Bus segmentation, address isolation |
| Special Function | TCA8418E, TCA8424, TCA6507 | Keyboard scanner, LED driver |

**Target applications:** servers, base stations, PLCs, switches/routers, automotive, medical imaging, industrial automation, control panels.

## Relevance to TCA9548A Implementation
The TCA9548A is listed as a key switch device for I2C bus segmentation. The infographic's capacitance budgeting guidelines (15 pF per device, 400 pF max) directly inform how many devices can be placed on each TCA9548A downstream channel. The pull-up resistor formulas should be applied per-channel to ensure rise times remain within spec. When designing a multi-device I2C tree, the TCA9548A's 1:8 fan-out provides the highest channel density, allowing the system designer to distribute slaves across channels while keeping per-channel capacitance well under 400 pF.
