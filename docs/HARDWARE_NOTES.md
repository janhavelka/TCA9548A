# Hardware Notes

These notes preserve the driver-relevant facts from the original chip
documentation extracts without keeping local copies of vendor PDFs. They were
rechecked against TI datasheet SCPS207H (Rev. H, September 2024); production
hardware must still be reviewed against the latest vendor revision and the
actual board voltage, pull-ups, wiring, and device population.

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
- The pass gates are transparent, so the mux answers at its own strapped
  address on every enabled channel as well. No downstream target visible on an
  enabled channel may use that address. Common occupants of `0x70` through
  `0x77` are other PCA954x/TCA954x muxes, HT16K33 LED drivers, SHTC3 (`0x70`),
  the PCA9685 All-Call address (`0x70`, enabled by default), and
  BMx280/BME680/MS56xx barometers (`0x76`/`0x77`).

## Control Register

- The chip has one 8-bit control register and no register address byte.
- Bit `N` enables channel `N`; any combination of bits can be enabled.
- A write transaction is just the target address plus one mask byte, followed by
  STOP.
- The new channel selection takes effect only after the STOP condition. A
  repeated START after the control byte does not switch the channels: TI
  SCPA063 section 4.1 documents that a repeated-START read after such a write
  returns the new byte while the switches have not moved. Write the byte, send
  STOP, then start a new transaction for the readback or the downstream target.
- If multiple bytes are written in one transaction, the chip stores only the
  last byte.
- A read transaction returns the current 8-bit control register value. Read
  exactly one byte; the content of any further byte is undocumented.
- POR and hardware RESET clear the control register to `0x00`, disabling all
  downstream channels.

## Reset And Recovery

- `RESET` is active low and must be pulled up to `VCC` (keep it at or below
  `VCC + 0.5 V`). Treat the device as unusable while `RESET` is held low: the
  datasheet specifies what happens when RESET is asserted, not how the part
  behaves while it stays low. A floating `RESET` picks up noise and causes
  spurious resets (TI SCPA063, section 2.4).
- Hardware RESET clears the control register and resets the internal I2C state
  machine without power-cycling the part.
- The datasheet specifies a minimum RESET-low pulse of 6 ns and zero minimum
  delay from RESET release to the next START condition.
- RESET releases SDA high within 500 ns maximum after assertion.
- When a downstream target holds SDA low, RESET (or a `0x00` write if the
  upstream bus is still usable) disconnects every channel and frees the
  upstream bus. It does not clear the downstream target. UM10204 section 3.1.16
  gives the generic remedy: for SDA stuck low the controller sends nine clock
  pulses, and if that fails, or if SCL is the stuck line, the target needs its
  own hardware reset or a power cycle. Because the switch is only a pass gate,
  those pulses reach the target only while its channel is enabled, so the
  channel must be re-selected first or the pulses driven from a per-channel
  GPIO. That routing requirement is this library's inference from the pass-gate
  topology, not a UM10204 statement.
- The library's explicit `hardReset()` callback is bounded by
  `Config::resetTimeoutMs`, invalidates cached mask state before RESET, verifies
  an exact `0x00` control byte afterward, and never reconnects a previous
  branch.
- A failed RESET callback leaves mask provenance unknown. A completed read that
  observes a nonzero byte is retained as verified evidence and reported as
  `RESET_STATE_MISMATCH`.
- A full power-cycle reset must drop `VCC` below the POR falling threshold and
  respect the datasheet's minimum time-to-reramp requirement.
- POR rising threshold is typically 1.2 V and 1.5 V maximum.
- POR falling threshold is 0.8 V minimum and 1.0 V typical.
- The `VCC_TRR` re-ramp specification requires at least 40 us between `VCC`
  dropping below `VPORF(min)` minus 50 mV (or to GND) and ramping `VCC` back
  up, measured at 25 C. The same table bounds the supply fall time to 1 ms to
  100 ms and the rise time to 0.1 ms to 100 ms.

## Bus Electrical Rules

- Supported bus modes are Standard-mode and Fast-mode only, 0 kHz to 400 kHz.
  There is no minimum clock and no internal transaction time-out, so the device
  will not free a hung transaction by itself; RESET and POR are the documented
  recovery paths.
- The datasheet specifies no SCL output driver for the switch. Its only
  switching characteristics are the SDA/SCL-to-`SCn`/`SDn` propagation delay and
  the RESET-to-SDA release, and UM10204 section 3.1.9 notes that most targets
  have no SCL driver and so cannot stretch. Treat "the switch itself does not
  stretch SCL" as an expectation rather than a guarantee. A downstream target on
  an enabled channel can stretch SCL, and because the channel is a transparent
  pass gate that stretch is seen upstream.
- `VCC` range is 1.65 V to 5.5 V up to 85 C and 1.65 V to 3.6 V above 85 C.
- For level translation, choose `VCC` so the datasheet's resulting
  `Vpass(max)` is at or below the lowest bus pull-up voltage. `VCC` itself is
  not the clamp voltage; TI's example uses 3.3 V `VCC` with a 2.7 V lowest
  bus. Verify the current `Vpass` curve for the selected supply and temperature.
- Each upstream and downstream bus segment needs its own pull-up resistors.
- Size pull-ups per segment, where `tr` is the maximum rise time for the bus
  mode (300 ns for Fast-mode, 1000 ns for Standard-mode) and `VOL(max)`/`IOL`
  are conventionally 0.4 V at 3 mA:
  - `Rp(min) = (Vpullup - VOL(max)) / IOL`
  - `Rp(max) = tr / (0.8473 x Cb)`
- Keep each active I2C segment within 400 pF, which the datasheet specifies for
  both Standard-mode and Fast-mode.
- Enabling multiple channels at the same time combines the capacitance of all
  enabled downstream buses as seen by the upstream controller.
- Enabling several channels also parallels their pull-ups: the total `IOL` the
  controller must sink is the sum of the currents through every enabled
  segment's `Rp`, so size `Rp(min)` for the worst-case combination rather than
  for one channel.
- A rough early estimate is 10 pF to 15 pF per attached I2C target, plus wiring,
  connector, trace, and switch capacitance.
- Datasheet off-capacitance is 20 pF typical and 28 pF maximum on upstream
  `SCL`/`SDA`, and 5.5 pF typical and 7.5 pF maximum on each disabled
  downstream `SCn`/`SDn` pin.
- The datasheet does not give a fixed `Cio(ON)` value; it depends on internal
  capacitance plus the external capacitance connected to the enabled channel.
- The pass-FET path is not a buffer and does not regenerate edges or add drive
  strength. Its on-resistance raises the low level seen on the far side of an
  enabled channel above the driving device's own `VOL`, in whichever direction
  the current flows.

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

The reviewed TI documentation does not define these cases, so the library does
not depend on them:

- General-call or software-reset address handling.
- Dynamic runtime re-sampling of `A0`, `A1`, or `A2`.
- Whether the device acknowledges its address while `RESET` is held low or `VCC`
  is below `VPORR`.

The documentation is explicit that the device has none of the following, so the
library does not expose them: interrupt or alert outputs, fault registers, ADC,
DAC, sensor, EEPROM, or any persistent or secondary register. The single 8-bit
control register is the entire programmable state.

## Source References

- [TI TCA9548A datasheet, SCPS207H (Rev. H)](https://www.ti.com/lit/ds/symlink/tca9548a.pdf).
- [TI SCPA063, "How to Debug I2C"](https://www.ti.com/lit/pdf/SCPA063) (STOP
  requirement for TI I2C switches, RESET biasing).
- [NXP UM10204, "I2C-bus specification and user manual"](https://www.nxp.com/docs/en/user-guide/UM10204.pdf)
  (reserved addresses, bus clear).
- [TI SCPA067, "Best Practices: I2C Devices on an I3C Shared Bus"](https://www.ti.com/lit/pdf/SCPA067).
- [TI SCAA137, "I2C Dynamic Addressing"](https://www.ti.com/lit/pdf/SCAA137).
- [TI SSZTC18, "How to Simplify I2C Tree When Connecting Multiple Slaves to an I2C Master"](https://www.ti.com/lit/pdf/SSZTC18).
- [TI SCPA058A, "I2C Solutions for Hot Swap Applications"](https://www.ti.com/lit/pdf/SCPA058A).
- [TI SLVA695, "Maximum Clock Frequency of I2C Bus Using Repeaters"](https://www.ti.com/lit/pdf/SLVA695).
- [TI SLUAAY3, "PassFET Hang Time with TCA39306 I2C/I3C Level Translator"](https://www.ti.com/lit/pdf/SLUAAY3).
- [TI SLYT658, "I2C Infographics"](https://www.ti.com/lit/pdf/SLYT658).
