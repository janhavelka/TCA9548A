# I2C Dynamic Addressing
**Source:** dynamic_addressing.pdf | **Doc #:** SCAA137 | **Pages:** 8

## Key Takeaways
- I2C switches are the conventional and recommended method for resolving address conflicts between same-address slaves
- Dynamic addressing (changing address pins at runtime) is an alternative when switches cannot be used, but requires verifying the slave does not latch its address at power-up
- Switches introduce Rdson series resistance which increases VOL seen by downstream devices; this is incompatible with current-source-type buffers (TCA9509, TCA9800)
- In complex I2C trees, cascaded switches (e.g., 8× TCA9544A off one root TCA9544A) can address 32+ same-address slaves but require careful address allocation and software sequencing

## Summary
When an I2C bus contains multiple slaves sharing the same fixed address, conflicts must be resolved. The conventional approach uses I2C switches — each same-address slave is placed on a separate downstream channel, and the master selects channels via the switch's control register before communicating. A single TCA9544A (1:4 switch) at address 0x70 can isolate four slaves with identical addresses (e.g., all at 0x41). Cascading switches expands this capacity; a complex tree of 8 TCA9544A switches at addresses 0x70–0x77 can support up to 32 identical slaves.

However, switches have limitations: Rdson creates a voltage drop making them incompatible with current-source buffers. In very complex trees, address exhaustion for the switches themselves becomes a concern. As an alternative, dynamic addressing — driving slave address pins directly from GPOs at runtime — can be used if the slave does not latch its address at power-up.

The GPO-to-address-pin method uses one GPO per slave; to communicate with a target slave, its GPO is set to a unique value while all others are set to the opposite. For more slaves than available GPOs, a DEMUX (e.g., TMUX1204, a 1:4 analog MUX) driven by GPOs expands the number of dynamically addressed slaves by 2^n where n = number of GPOs.

## Technical Details
**Address latch test procedure (per slave):**
1. Set hardware address pins to known value, power up device
2. Perform I2C write — confirm ACK
3. Flip address pins to opposite logic level (without power cycling)
4. Write to old address — expect NACK
5. Write to new address — if ACK, the slave supports dynamic addressing

**Example (TCA6408A):**
- Address: 0b010_000_A0 → A0=GND gives 0x20
- After flipping A0 to VCC: NACK at 0x20, ACK at 0x21 → confirmed dynamically addressable

**I2C switch method:**
- Switch control register: write channel-enable byte after address byte
- Each bit corresponds to one downstream channel
- TCA9544A: 4 channels, 2 address pins → addresses 0x70–0x73
- Cascaded tree example: root TCA9544A (0x70) → 4× downstream TCA9544A (0x72–0x75) → 16 slaves at 0x40

**GPO method constraints:**
- Requires 1 GPO per slave device
- Saves board space and BOM vs. switch approach
- Limited by available GPO count

**DEMUX method (TMUX1204):**
- 1:4 multiplexing with 2 GPOs → 4 slave addresses
- Fixed 1.8 V logic thresholds on control pins
- Supply current: 0.01 µA typical, 1.3 µA max (−40°C to 125°C)
- Leakage: < ±750 nA max
- Package: 2.5 mm × 1.00 mm µSON
- Can use I2C I/O expander (e.g., TCA9555) to drive select pins if no GPOs available

**Switch limitations:**
- Rdson creates VOL increase on downstream side; VOL must remain below VIL for valid logic low
- Incompatible with current-source buffers (TCA9509, TCA9800) — pull-ups past the switch interfere with buffer's low-detection algorithm
- Address space: only 3 bits (8 addresses) available for switches in a tree

## Relevance to TCA9548A Implementation
The TCA9548A is the primary tool for address conflict resolution in I2C systems. With 8 downstream channels and 3 address pins (addresses 0x70–0x77), it provides the highest channel density among TI switches. When designing the TCA9548A driver, the software must implement a channel-selection protocol: write the channel mask to the switch before communicating with downstream devices, and disable channels afterward to avoid unintended bus conflicts. In cascaded switch topologies, the driver must traverse the tree top-down, enabling one path at a time. The Rdson consideration means the driver should not place current-source buffers on downstream channels.
