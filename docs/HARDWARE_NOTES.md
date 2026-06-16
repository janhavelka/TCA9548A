# Hardware Notes

These notes preserve the driver-relevant facts from the original chip
documentation extracts without keeping local copies of vendor PDFs.

## Device Model

- TCA9548A is an 8-channel bidirectional I2C/SMBus switch.
- The upstream bus is connected to zero or more downstream `SCn`/`SDn` channel
  pairs through pass-FET switches.
- The 7-bit address range is `0x70` through `0x77`, selected by `A0`, `A1`, and
  `A2`.
- Up to eight TCA9548A devices can share one upstream bus if their address pins
  are unique.
- Address pins must be tied directly to `VCC` or `GND`; do not leave them
  floating.
- No downstream target visible on an enabled channel should use the active
  TCA9548A address; the mux itself will also respond on `0x70` through `0x77`.

## Control Register

- The chip has one 8-bit control register and no register address byte.
- Bit `N` enables channel `N`; any combination of bits can be enabled.
- A write transaction is just the target address plus one mask byte, followed by
  STOP.
- The new channel selection takes effect only after the STOP condition.
- Do not use repeated START between a control-register write and downstream
  target traffic when expecting the new channel selection to be active.
- If multiple bytes are written in one transaction, the chip stores only the
  last byte.
- A read transaction returns the current 8-bit control register value.
- POR and hardware RESET clear the control register to `0x00`, disabling all
  downstream channels.

## Reset And Recovery

- `RESET` is active low. Pull it up to `VCC` if the design does not actively
  drive it.
- Hardware RESET clears the control register and resets the internal I2C state
  machine without power-cycling the part.
- The datasheet specifies a minimum RESET-low pulse of 6 ns and zero minimum
  delay from RESET release to the next START condition.
- RESET releases SDA high within 500 ns maximum after assertion.
- RESET is the preferred hardware recovery path when a downstream target holds
  SDA low and the mux reset pin is available.
- A full power-cycle reset must drop `VCC` below the POR falling threshold and
  respect the datasheet's minimum time-to-reramp requirement.
- POR rising threshold is typically 1.2 V and 1.5 V maximum.
- POR falling threshold is typically 0.8 V and 1.0 V maximum.
- After `VCC` drops below the POR falling threshold, allow at least 40 us before
  reramping `VCC`.

## Bus Electrical Rules

- Supported bus modes are Standard-mode and Fast-mode only, up to 400 kHz.
- `VCC` range is 1.65 V to 5.5 V.
- For level translation, set TCA9548A `VCC` at or below the lowest bus voltage
  that must pass through the switch.
- Each upstream and downstream bus segment needs its own pull-up resistors.
- Size pull-ups per segment:
  - `Rp(min) = (Vpullup - VOL(max)) / IOL`
  - `Rp(max) = tr / (0.8473 x Cb)`
- Keep each active I2C segment within the I2C capacitance budget. For Fast-mode,
  use 400 pF as the practical limit.
- Enabling multiple channels at the same time combines the capacitance of all
  enabled downstream buses as seen by the upstream controller.
- A rough early estimate is 10 pF to 15 pF per attached I2C target, plus wiring,
  connector, trace, and switch capacitance.
- Datasheet off-capacitance values are 20 pF to 28 pF on upstream `SCL`/`SDA`
  and 5.5 pF to 7.5 pF on disabled downstream `SCn`/`SDn` pins.
- The datasheet does not give a fixed `Cio(ON)` value; it depends on internal
  capacitance plus the external capacitance connected to the enabled channel.
- The pass-FET path is not a buffer and does not regenerate edges or add drive
  strength. Switch resistance increases the effective low-level voltage seen by
  downstream devices.

## Topology Guidance

- Use one channel at a time when isolating identical downstream target
  addresses.
- Disable all channels when idle if the application does not need a persistent
  downstream connection.
- In cascaded mux trees, enable paths top-down and keep TCA9548A addresses
  unique across the visible bus segment.
- Do not place current-source I2C buffers such as `TCA9509` or `TCA9800` in
  series with TCA9548A switch channels. TI app notes call this topology
  incompatible with pass-FET switch resistance and buffer low-level detection.
- If a downstream branch has many devices or long wiring, use repeaters only
  after checking capacitance, rise time, and repeater propagation delay for the
  chosen clock speed.

## Hot Swap And I3C Caveats

- The part powers up with all channels disabled and supports hot insertion into
  a live backplane, but upstream bus disturbance is still a board-level concern.
- False clock edges during insertion and bad downstream POR can still leave a
  downstream target holding SDA or SCL low.
- For hot-swap systems, prefer presence detection before enabling a channel, and
  stagger connectors so ground mates first, then power, then SDA/SCL.
- TCA9548A can segment legacy I2C devices away from a mixed I3C bus, but its
  off-capacitance is significant against the tighter I3C capacitance budget.
  Keep downstream channels disabled during I3C traffic unless the design has
  explicitly budgeted the loading.
- TI app notes call out a 50 ns deglitch filter on TCA954xA switches for
  mixed-bus use, but I3C has a much tighter 50 pF total capacitance budget; the
  TCA9548A upstream off-capacitance alone is about 20 pF.

## Behaviors Not Promised By This Library

The reviewed TI documentation does not clearly define these cases, so the
library does not depend on them:

- Channel activation after repeated START without an intervening STOP.
- General-call or software-reset address handling.
- Any clock-stretching behavior by the switch itself.
- Dynamic runtime re-sampling of `A0`, `A1`, or `A2`.
- Any interrupt, alert, fault-register, ADC, DAC, sensor, EEPROM, or persistent
  register behavior.

## Source References

- TI TCA9548A datasheet, SCPS207H.
- TI SCPA067, "Best Practices: I2C Devices on an I3C Shared Bus".
- TI SCAA137, "I2C Dynamic Addressing".
- TI SSZTC18, "How to Simplify I2C Tree When Connecting Multiple Slaves to an
  I2C Master".
- TI SCPA058A, "I2C Solutions for Hot Swap Applications".
- TI SLVA695, "Maximum Clock Frequency of I2C Bus Using Repeaters".
- TI SLUAAY3, "PassFET Hang Time with TCA39306 I2C/I3C Level Translator".
- TI SLYT658, "I2C Infographics".
