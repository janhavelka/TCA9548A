# Validation Status

## Datasheet review

The driver protocol and constants were reviewed against the Texas Instruments
TCA9548A datasheet SCPS207H (Rev. H, September 2024):

- the strap-selected 7-bit address range is `0x70` through `0x77`;
- the device has one 8-bit control byte and no register-address phase;
- bit N enables channel N and any combination of the eight channels is valid;
- a control write is one data byte followed by STOP; a multi-byte write
  retains only the final byte;
- a read returns the current control byte;
- POR and RESET clear the control byte to `0x00`;
- RESET is active low, requires at least 6 ns low, permits the next START with
  zero recovery time, and releases SDA within 500 ns;
- Standard-mode and Fast-mode operation are supported through 400 kHz.

The device has no identity register, interrupt, general-call reset, or other
register. A successful control-byte read proves that something acknowledged
the configured address, not the exact chip identity.

Primary source: [TI TCA9548A datasheet SCPS207H](https://www.ti.com/lit/ds/symlink/tca9548a.pdf).

## Automated evidence

The native test suite in `test/test_driver.cpp` covers:

- exact transaction shape: address, one-byte write payload, read-only
  control-byte read with a null transmit buffer, timeout propagation;
- every strap address and every one of the 256 control masks;
- output assignment only on success and distinct mapping of every transport
  error;
- cache provenance after successful and ambiguous writes and reads, probe,
  RESET, and explicit invalidation;
- lifecycle: bound-but-failed `begin()`, rebind rejection, bus-silent `end()`,
  lifetime counters surviving rebind;
- passive health transitions, saturating counters, and timestamp wrap,
  including failures before the first tracked success keeping `UNINIT`, an
  `offlineThreshold` of 1 reaching `OFFLINE` without a `DEGRADED` step, and a
  missing `nowMs` hook leaving both timestamps at zero;
- `recover()` and `hardReset()` bounds: one callback each, no retry, no route
  restore, exact-zero verification, the callback result domain, and the tracked
  verification read that leaves `state()` at `READY` on a mismatch;
- allocation-free enum names, including invalid casts, and the bus-silent
  settings snapshot;
- the shared fixed-line CLI accumulator used by both examples.

CI additionally compiles the core with strict C++17 warnings and no Arduino
include paths, builds the Arduino example for ESP32-S2/S3, builds the native
ESP-IDF example for both targets, runs the CLI, ESP-IDF, and hygiene checkers
plus the HIL parser self-test, and builds the Doxygen documentation with
warnings as errors. The commands are listed in
[CONTRIBUTING.md](../CONTRIBUTING.md).

## Not validated

No hardware fixture backs the evidence above. Nothing here claims live device
identity, electrical timing, voltage translation, address straps, RESET wiring,
simultaneous-channel loading, downstream routing, hot insertion, or
long-duration stability. Firmware compilation, static CLI checks, HIL parser
self-tests, and HIL dry runs are not physical validation. Physical evidence
requires `tools/tca9548a_hil.py` against an attached fixture whose RESET pin is
wired and named in the example's `TCA_RESET` or `RESET_GPIO` constant; a run
against an unmodified checkout fails the RESET case by design.
