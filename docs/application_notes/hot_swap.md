# I2C Solutions for Hot Swap Applications
**Source:** hot_swap.pdf | **Doc #:** SCPA058A | **Pages:** 9

## Key Takeaways
- Hot insertion into a live I2C backplane risks false clock edges (from inrush current when SCL is high) and bad power-on-reset for downstream slaves, either of which can cause a stuck bus
- Backplane-side solution: use an I2C switch (TCA954x) with presence detection (TCA9555 GPIO expander + interrupt) to enable channels only after card insertion is detected
- Card-side solution: use a hot-insertion buffer (TCA9511A) with 1-V pre-charge, stop/idle detection, and rise time accelerator
- Connector design must stagger pins: GND first, then VCC (~25 mil shorter), then SDA/SCL (~50 mil shorter than GND)

## Summary
Hot swap (hot insertion) is the ability to plug an external card into a live backplane without powering down the system — critical in servers, base stations, and telecom equipment. Two primary failure modes exist: false clock edge generation and bad power-on-reset of downstream slaves.

When an external card connects to a live bus with SCL high, inrush current flowing into the card's parasitic capacitance creates a momentary dip on SCL. Downstream slaves may interpret this as an extra clock edge, becoming out of sync with the master. In the worst case, a slave holds SDA low waiting for a clock pulse that never comes — a stuck bus condition.

A bad power-on-reset occurs when the VCC ramp rate during hot insertion (affected by parasitic inductance causing ringing) falls outside the downstream slave's specified range. The slave may power up in an unknown state, potentially holding SDA or SCL low.

Two architectural solutions exist depending on whether the designer controls the backplane or the external card.

## Technical Details
**Backplane-side approach (I2C switch):**
- Place TCA954x switch at backplane edge with downstream channels disabled by default
- Use TCA9555 I/O expander as presence detector: GPIx pin has internal 100 kΩ pull-up; external card connects GND to that pin, pulling it LOW and generating an interrupt
- On interrupt, processor identifies which input changed and enables the corresponding switch channel
- Switch with reset can recover from stuck bus by toggling RESET to disable all channels

**Connection sequence (critical):**
1. GND connects first (prevents back-biasing via SDA/SCL)
2. VCC connects second
3. SDA/SCL connect last
- Requires staggered male connector: GND at full length, VCC ~25 mil shorter, SDA/SCL ~50 mil shorter

**Card-side approach (TCA9511A hot-insertion buffer):**
- 1-V pre-charge circuit limits inrush current during insertion
- 'IN-side' faces backplane; 'OUT-side' faces external card slaves
- No pull-up resistors on IN-side (would create false idle condition and disable pre-charge)
- Three conditions for connection:
  1. EN pin is HIGH (processor control)
  2. SCLOUT/SDAOUT are HIGH (downstream slaves powered correctly, no stuck bus)
  3. Stop condition or bus idle detected on SCLIN/SDAIN (safe to connect without mid-transaction glitch)
- READY pin goes HIGH when all three conditions met

**Rise Time Accelerator (TCA9511A):**
- I2C spec rise time limits: < 1000 ns (standard mode 100 kHz), < 300 ns (fast mode 400 kHz)
- Rise time = 30% VCC to 70% VCC transition time
- Example: 360 pF load, 10 kΩ pull-up, 5 V VCC → 3307 ns without RTA (spec violation), 214.7 ns with RTA (within spec)
- RTA trigger conditions: signal > 0.6 V AND slew rate > 1.25 V/µs

## Relevance to TCA9548A Implementation
The TCA9548A is directly applicable as the backplane-side hot-swap isolation device. Its channels default to disabled at power-up, so hot-inserting the switch itself does not disturb the bus. The driver should implement a hot-swap protocol: (1) detect card presence via interrupt or polling, (2) enable the corresponding switch channel only after the card is powered and stable, (3) implement stuck-bus recovery by toggling the TCA9548A RESET pin to disable all channels if SDA or SCL is held low for an anomalous duration. For external card designs, combining TCA9548A on the backplane with TCA9511A on the card provides defense-in-depth.
