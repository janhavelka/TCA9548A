# How to Simplify I2C Tree When Connecting Multiple Slaves to an I2C Master
**Source:** simplify_i2c_tree.pdf | **Doc #:** SSZTC18 | **Pages:** 5

## Key Takeaways
- Complex I2C trees with many slaves degrade signal integrity due to cumulative bus capacitance (~15 pF per device), causing missed bits and communication errors
- I2C switches split the upstream bus into multiple downstream segments, isolating same-address slaves and reducing per-segment capacitance
- When multiple downstream channels are active simultaneously, adding I2C repeaters (TCA9617B) on each channel restores signal integrity by buffering and isolating capacitance
- Repeaters also increase pull-down strength, overcoming the problem where stronger pull-ups (lower resistance for faster rise times) exceed slave IOL sink capability

## Summary
As electronic systems grow in complexity — automotive ADAS, servers, base stations, industrial automation — the number of I2C sensors and peripherals on a single bus increases. When the I2C "tree" becomes complicated, signal integrity degrades because each device adds approximately 15 pF to the bus. When total capacitance approaches or exceeds the 400 pF I2C specification limit, rise times lengthen, edges degrade, and bit errors occur. Such errors are painful to debug and can lead to product recalls.

An I2C switch (e.g., TCA9548A) splits the upstream bus into multiple downstream segments. Each segment carries only its assigned slaves' capacitance. If only one channel is active at a time, signal integrity is maintained as long as fewer than ~15 slaves are on that channel. However, some applications require multiple channels active simultaneously, re-aggregating the capacitance problem.

Adding I2C repeaters (TCA9617B) on downstream channels after the switch solves this by buffering the I2C signal. The repeater isolates capacitance — each side independently supports up to 400 pF. Oscilloscope measurements show significant improvement: the SCL waveform through a switch alone (SCL_out1) with >25 slaves shows signal integrity issues, while the same signal through a switch + repeater (SCL_out2) shows clean edges. Not every channel needs a repeater — the designer should check waveforms on each channel and add repeaters only where needed.

An additional benefit: reducing pull-up resistance to improve rise times on heavily loaded buses can cause slaves to be unable to sink enough current (IOL) to pull the bus low. Repeaters increase pull-down strength, eliminating this trade-off.

## Technical Details
**Capacitance budgeting:**
- Estimate ~15 pF per slave device on the bus
- I2C spec maximum: 400 pF per bus segment (FM), 550 pF (FM+)
- With 25+ slaves: 25 × 15 pF = 375 pF → near the 400 pF limit on a single segment

**Architecture pattern: Switch + Repeater**
1. I2C switch splits upstream bus into N downstream channels
2. Each channel has its own pull-up resistors to its VCCA supply
3. Add TCA9617B repeater on channels with high slave count or long traces
4. Repeater isolates A-side and B-side capacitance — each can be up to 400 pF independently
5. Monitor waveforms with oscilloscope to determine which channels need repeaters

**Signal integrity improvement (observed):**
- SCL through switch only (>25 slaves): degraded waveform, potential missed bits at higher capacitance loads
- SCL through switch + TCA9617B repeater: clean waveform with proper rise/fall times

**Pull-up / pull-down trade-off:**
- Lowering Rpu improves rise time but increases IOL requirement for slaves
- Slave devices have a fixed maximum IOL (typically 3 mA @ VOL = 0.4 V for FM)
- Repeaters provide current amplification, allowing stronger pull-ups without exceeding slave IOL

**Target applications:** automotive (infotainment, ADAS, body), enterprise (servers, network switches, base stations), industrial (building/factory automation, smart grid).

## Relevance to TCA9548A Implementation
The TCA9548A is the recommended switch for simplifying complex I2C trees, providing 8 downstream channels from a single upstream bus. The driver design should consider: (1) Channel allocation strategy — distribute slaves across channels to keep per-channel capacitance well under 400 pF. (2) Multi-channel activation — when enabling multiple channels simultaneously, total downstream capacitance seen by the master is the sum of all active channels; consider adding TCA9617B repeaters if this exceeds budget. (3) Per-channel pull-up sizing — each downstream channel needs its own pull-up resistors sized for that channel's capacitance. (4) Diagnostic capability — the driver should support per-channel waveform validation during system bring-up to identify channels requiring repeaters.
