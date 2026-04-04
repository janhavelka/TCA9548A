# Best Practices: I2C Devices on an I3C Shared Bus
**Source:** best_practices.pdf | **Doc #:** SCPA067 | **Pages:** 3

## Key Takeaways
- I3C is backwards-compatible with I2C at 400 kHz and 1 MHz, but I2C devices require a 50-ns deglitch filter on SDA/SCL to coexist on an I3C bus
- I3C buses have a strict 50 pF capacitive loading limit — each I2C device can add up to 10 pF, quickly consuming the budget
- Using an I2C switch or passive MUX to segment the bus into separate I2C and I3C lanes reduces capacitive loading on the I3C side when disabled
- TCA9548A can serve as a bus segmenter but contributes 20 pF off-capacitance, higher than alternatives like TMUX136 (1.5 pF) or TCA9800 (2 pF on A-side)

## Summary
The I3C protocol is designed to coexist with legacy I2C devices, but the system designer typically does not know whether a given I2C target includes the required 50-ns deglitch filter for safe coexistence. The recommended best practice is to use a switch or passive MUX to physically segment the bus into I3C-only and I2C-only lanes, preventing unfiltered I2C devices from interfering with I3C signaling.

Segmenting the bus also provides capacitance management benefits. When the switch or MUX channel is disabled, the I2C device capacitance is disconnected from the I3C bus. This is critical because I3C allows only 50 pF total bus capacitance — far less than the 400 pF limit for I2C Fast Mode.

## Technical Details
**Bus segmenting device comparison:**

| Device | Type | Off-Capacitance | Notes |
|---|---|---|---|
| TCA39306 / PCA9306 | Level translator with disable | 4 pF | EN pin LOW to disable; tri-state to enable |
| TCA9800 | I2C 400 kHz buffer/redriver | 2 pF (A-side) | EN pin LOW to disable; A-side faces I3C bus |
| TCA9548A | 8-ch I2C switch with level translation | 20 pF | I2C-controlled; 50-ns deglitch filter on SDA/SCL |
| TCA9546A | 4-ch I2C switch with level translation | 15 pF | I2C-controlled; 50-ns deglitch filter on SDA/SCL |
| TCA9543A | 2-ch I2C switch with level translation | 15 pF | I2C-controlled; 50-ns deglitch filter on SDA/SCL |
| TMUX136 | 2-ch 2:1 I3C passive MUX | 1.5 pF | 1.6 pF on-cap, 5.7 Ω Ron, 6 GHz BW |
| TMUX154E | 2-ch 2:1 I3C passive MUX | 2 pF | 7.5 pF on-cap, 6 Ω Ron, 900 MHz BW |

**Key design rules:**
- I3C capacitive loading limit: **50 pF** total
- I2C device typical capacitance: up to **10 pF** per device
- Disable all downstream I2C switch channels during I3C communication
- I2C-controlled switches (TCA954xA family) include 50-ns deglitch filters and are specification-compliant for shared buses

## Relevance to TCA9548A Implementation
The TCA9548A can be used to isolate I2C devices from an I3C bus when operating in a mixed I3C/I2C system. Its built-in 50-ns deglitch filter makes it safe on a shared bus. However, its 20 pF off-capacitance is relatively high — if the I3C bus capacitance budget is tight, a lower-capacitance alternative (TCA9800 or TMUX136) may be preferred for the segmenting role. In a TCA9548A driver, the firmware should ensure all downstream channels are disabled during I3C transactions to minimize capacitive loading on the shared bus.
