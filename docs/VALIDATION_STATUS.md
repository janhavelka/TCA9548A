# Validation Status

## Datasheet review

The driver protocol and constants were re-reviewed on 2026-10-03 against the Texas Instruments
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
- Standard-mode and Fast-mode operation are supported through 400 kHz, with
  minimum STOP-to-START bus-free times of 4.7 us and 1.3 us respectively;
- eight distinct mux addresses may be simultaneously visible; this imposes no
  limit on driver objects or address reuse across isolated branches.

The device has no identity register, interrupt, general-call reset, or other
register. A successful control-byte read proves that something acknowledged
the configured address, not the exact chip identity.

Primary source: [TI TCA9548A datasheet SCPS207H](https://www.ti.com/lit/ds/symlink/tca9548a.pdf).

## Automated evidence

The native driver test suite in `test/test_driver/test_driver.cpp` covers:

- exact transaction shape: address, one-byte write payload, read-only
  control-byte read with a null transmit buffer, timeout propagation;
- every strap address and every one of the 256 control masks;
- output assignment only on success and distinct mapping of every transport
  error;
- cache provenance after successful and ambiguous writes and reads, probe,
  RESET, and explicit invalidation;
- lifecycle: bound-but-failed `begin()`, rebind rejection, bus-silent `end()`,
  lifetime counters surviving rebind;
- `tick()` performs no I/O or RESET and preserves settings, mask evidence,
  health counters, timestamps, and last error across lifecycle states,
  including a failed initial binding;
- passive health transitions, consecutive-failure saturation, and timestamp wrap,
  including failures before the first tracked success keeping `UNINIT`, an
  `offlineThreshold` of 1 reaching `OFFLINE` without a `DEGRADED` step, and a
  missing `nowMs` hook leaving both timestamps at zero;
- `recover()` and `hardReset()` bounds: one callback each, no retry, no route
  restore, exact-zero verification, the callback result domain, and the tracked
  verification read that leaves `state()` at `READY` on a mismatch;
- allocation-free enum names, including invalid casts, and the bus-silent
  settings snapshot.

The lifetime `uint32_t` counter saturation branches are code-reviewed; the
suite does not execute billions of transfers to reach them. The tests also
cover copied configuration with separate borrowed contexts, raw probe across
UNINIT/DEGRADED/OFFLINE, malformed transport enum values, and preserving caller
output when a failed read already modified its temporary receive byte.
Two mux instances sharing one serialized transport are tested for independent
addresses, health, mask observations, timestamps, and lifecycle.

The default 20 ms timeout is verified at every I2C entry point. A test-only
owner adapter also exercises deferred `begin()`, a separate 20 ms owner cap,
remaining-budget clipping, uptime beyond 32 bits and across 64-bit wrap,
expired work without physical I/O, late successful writes that become timeout
with unknown mask state, original backend errors, and failed cleanup under a
separately granted deadline. This proves compatibility with that callback
policy, not an internal driver scheduler or a real owner's implementation.

The separate `native_cli` suite in `test/test_cli_line_buffer/` tests the
shared fixed-line accumulator used by both examples without compiling driver
sources. It covers trimming, CRLF handling, capacity boundaries, discarded
overlong lines, invalid destination buffers, and recovery on the next command.
It additionally rejects embedded control bytes and signed/wrapping, overflowing,
or trailing numeric arguments through the same helper used by both CLIs.

The `native_transport` suite exercises the actual example Wire adapter against
a test-only recording double: exact one-byte shapes, explicit STOP, independent
bus context, clock/timeout bounds, ambiguous NACK mapping, short writes,
failed reads, and preserving output. It cannot validate the real Wire/SDK
implementation or electrical timing.

Configured CI additionally compiles the core with strict C++17 warnings and no Arduino
include paths, links/runs a standalone CMake consumer using only the exported
target's include path and C++17 requirement, builds the Arduino example for ESP32-S2/S3, builds the native
ESP-IDF example for both targets on SDK versions 5.4.4 and 5.5.5, runs the CLI, ESP-IDF, and hygiene checkers
plus the HIL parser self-test, and builds the Doxygen documentation with
warnings as errors. The package checker verifies exported file contents against
the checkout, rejects unexpected files, and builds and runs an isolated native
consumer of the archive. Each CI job checks for tracked and untracked changes
after validation. The commands are listed in
[CONTRIBUTING.md](../CONTRIBUTING.md).

`tools/test_tca9548a_hil.py` adds host regression tests for strict result parsing,
serial prompt/deadline handling, route isolation, mask sweep, cleanup, and
argument rejection. It also covers oversized numeric fields, extracting masks
and addresses only from validated records, transcript failures, and reporting
route checks that never ran. Configuration checks require exactly one valid reported
I2C timeout and record that setting in the result notes; this is not a timing
measurement. These use scripted responses and are not hardware evidence.

Local validation on 2026-10-03 passed 47 driver, 3 CLI, and 7 Wire-adapter
native tests, 49 Python HIL host regressions, parser checks, the framework-neutral
strict-warning build, standalone CMake library and consumer builds (including
CTest), both Arduino ESP32-S2/S3 builds, Doxygen, repository contracts, and
package creation and the automated archive checker. A native PlatformIO consumer also built
and ran from the packaged archive with strict dependency compatibility, without
an Arduino or ESP-IDF framework. Thirteen focused package-checker cases passed,
including source mismatches, missing files, unsafe paths, links, duplicate
members, and an invalid work directory.

[CI run 37146753916](https://github.com/janhavelka/TCA9548A/actions/runs/37146753916)
passed host checks, both Arduino builds, and both native ESP-IDF 5.5.5 builds
at commit `54965b51ef300f51c54ba5d8dfd99cf1f59eb5f6`. The expanded SDK matrix
and package checker are covered by subsequent workflow runs; check the run for
the exact commit being used. Native ESP-IDF builds were not run locally because
`idf.py` is unavailable. No live fixture was accessed and no physical
qualification result is implied.

For the Windows Arduino builds, the installed compiler's directory
`%USERPROFILE%\.platformio\packages\toolchain-xtensa-esp-elf\bin` had to be
added to the build process's PATH. The existing managed Core and toolchain
were used; no system-wide PATH change or new installation was made.

## Live HIL Procedure

1. Set the firmware's SDA/SCL, strapped mux address, and wired RESET pin for the
   actual fixture. Arduino uses `examples/common/BoardConfig.h` plus the address
   in `configureDriver()`; native IDF uses the constants at the top of its
   `main.cpp`. RESET defaults to disabled in both examples. Supply the mux and
   use reviewed external pull-ups; see [Hardware Notes](HARDWARE_NOTES.md).
2. Build and flash the appropriate example, close other serial monitors, and
   install `pyserial` into the Python environment used for live runs if absent.
   Confirm `cfg` and `version`. `--address` checks the flashed configuration;
   it does not change the firmware address. No runner mode flashes firmware.
3. On a fixture safe for the built-in one-hot and `0xA5` selections, run:

   ```powershell
   python tools/tca9548a_hil.py --port COM8 --address 0x70 --report .pio/hil/live.md --transcript .pio/hil/live.serial.txt --fixture-note "PCB revision, MCU, RESET GPIO, rails, pull-ups, targets and flashed binary identity"
   ```

   This checks complete command responses, healthy READY state, scan errors,
   every one-hot control mask, arbitrary mask `0xA5`, recovery, and verified
   restoration. RESET must clear a verified `0x01` seed, so an unwired pin
   cannot pass merely because the device was already all-off.
4. To check physical route visibility, declare every occurrence of the selected
   downstream fixture addresses, for example:

   ```powershell
   python tools/tca9548a_hil.py --port COM8 --route 0:0x48 --route 1:0x48 --report .pio/hil/routes.md --transcript .pio/hil/routes.serial.txt
   ```

   Declared addresses must be absent upstream with all channels off, visible
   on exactly their declared branches across all eight one-hot scans, then
   absent again all-off. Upstream devices must remain visible. Shared addresses
   across isolated branches are supported; this checks reachability, not target
   identity or payload correctness. The pinned SDK's probes run at 100 kHz.
5. On a fixture allowing **all channel combinations**, add `--sweep-masks` to
   write/read all 256 control bytes. The sweep and route checks capture and
   restore the entry mask, including on failures while serial framing remains
   usable. After a lost command boundary, cleanup is reported unverified; the
   runner does not send further blind commands.
6. Add `--sample-count 1000 --stress-count 1000 --soak-duration-s 3600` for
   bounded repetition on a suitable fixture. Counts are capped at 1,000 per
   firmware stress command; soak duration is capped at one day and 100,000
   host commands. Stress and soak require verified all-off cleanup. Mixed
   stress checks its readbacks against the preceding mask write.

The runner has separate finite command budgets: 5 s normally, 30 s for scan,
and 60 s for firmware stress by default, configurable through `--timeout-s`,
`--scan-timeout-s`, and `--stress-timeout-s`. Receive storage is capped at
64 KiB per response and optional transcripts stream to disk. Soak may extend
past its scheduling deadline by the in-flight command and bounded cleanup.
These host budgets do not establish the firmware's physical timing bound.
Incomplete responses, skipped required RESET checks, unknown results, scan
faults, mismatched readbacks, and incomplete stress all fail the run.

Reports record host checkout identity separately from runtime version output;
they do not prove the flashed binary came from that checkout. Record its hash,
board revision, wiring, voltage, pull-ups, target population, and measurements
in fixture evidence. `--skip-reset` and `--allow-not-run` are diagnostic
exceptions, not release qualification. `--dry-run` and `--parser-self-test`
never open a port.

Before field qualification, also measure STOP separation and bus-free time, final-byte NACK,
RESET waveform and bus release, rise times/low levels under worst permitted
loading, cold POR versus MCU-only reboot, and recovery from a deliberately
stuck downstream branch. Run real target payloads at the intended clock and
repeat on each supported board/framework. Power/reset fault injection and
analog measurements remain operator-controlled; the runner does not simulate
them or claim that mask readback proves the physical switches moved.

## Not validated

No hardware fixture backs the evidence above. Nothing here claims live device
identity, electrical timing, voltage translation, address straps, RESET wiring,
simultaneous-channel loading, downstream routing, hot insertion, or
long-duration stability. Firmware compilation, static CLI checks, HIL parser
self-tests, and HIL dry runs are not physical validation. Physical evidence
requires `tools/tca9548a_hil.py` against an attached fixture whose RESET pin is
wired and named in the example's `TCA_RESET` or `RESET_GPIO` constant; a run
against an unmodified checkout fails the RESET case by design.
