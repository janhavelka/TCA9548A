# Changelog

All notable changes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Entries describe the change relative to the last release, v1.0.0.

## [Unreleased]

### Added

- `Err::RESET_ERROR` (append-only) for non-timeout RESET callback failures.
- Allocation-free enum names: `errorName()`, `driverStateName()`,
  `maskProvenanceName()`, and matching `toString()` overloads.
- Native ESP-IDF 5.4/5.5 component (`CMakeLists.txt`, `idf_component.yml`) and
  the `examples/espidf_basic` `app_main` CLI, with the same command set as the
  Arduino CLI, for ESP32-S2/S3.
- Shared fixed-size line accumulator `examples/common/CliLineBuffer.h` used by
  both CLIs: a command is accepted only after CR or LF, and an overlong line is
  discarded whole.
- Repository checkers `tools/check_cli_contract.py`,
  `tools/check_idf_example_contract.py`, and
  `tools/check_repository_hygiene.py`, plus the Windows PlatformIO wrapper
  `scripts/pio.cmd`.
- `docs/FEATURE_MATRIX.md` and `docs/VALIDATION_STATUS.md`.

### Changed

- `hardReset()` enforces the callback result domain: `OK`, `TIMEOUT`, and
  `RESET_ERROR` pass through unchanged; any other code is reported as
  `INVALID_CONFIG` with the original code in `Status::detail`.
- Collapsed the private transport layers. Seven raw/tracked wrappers and
  control-byte helpers became `_writeControlByte(mask)` and
  `_readControlByte(mask, tracked)`, and `_recordMask()` was inlined at all
  three of its call sites, which the read merge collapses to two assignments. `src/TCA9548A.cpp` went from 340 to 272 lines. The removed
  argument checks were unreachable, so public behavior is unchanged.
- `recover()` is now an alias of `disableAll()`; both always performed the same
  single tracked `0x00` write.
- Documented behavior that was previously only implied, each now covered by a
  test: `DriverState` stays `UNINIT` while failures accumulate before the first
  tracked success, an `offlineThreshold` of 1 reaches `OFFLINE` with no
  `DEGRADED` step, a failed read including `probe()` invalidates the cached
  observation, the `hardReset()` verification read is tracked while
  `RESET_STATE_MISMATCH` leaves `state()` at `READY`, and `resetTimeoutMs` is
  range-checked even when no RESET callback is configured.
- Corrected the datasheet guidance in `docs/HARDWARE_NOTES.md`: a repeated START
  does not switch channels, the mux answers at its own address on every enabled
  channel, RESET frees the upstream bus but does not clear a stuck downstream
  target, only one byte may be read, the POR falling threshold was stated
  backwards, `VCC` is limited to 3.6 V above 85 C, the re-ramp threshold is
  `VPORF(min)` minus 50 mV, off-capacitance figures are typical/maximum rather
  than a range, 400 pF applies to both bus modes, and sink current sums across
  simultaneously enabled channels. Statements that rest on forum posts rather
  than vendor documents are now marked as expectations.
- Example builds pin pioarduino `platform-espressif32` `55.03.311`
  (Arduino-ESP32 `3.3.11`, ESP-IDF `5.5.5`); the ESP32-S3 QSPI PSRAM
  configuration is explicit, and the `version` command reports MCU, flash,
  PSRAM, Arduino, and ESP-IDF identity.
- The example `Wire` adapter sets the bus frequency in `begin()`.
- Both CLIs: `selftest` checks all eight one-hot channels and restores the entry
  mask, `scan` prints the active mask first, and `cfg` prints the bound driver
  snapshot.
- `scripts/generate_version.py` also synchronizes `Doxyfile` and
  `idf_component.yml`; Doxygen output moved to the ignored `.doxygen/`.
- `library.json` now exports `idf_component.yml`, so an installed copy carries
  the whole ESP-IDF component, and no longer exports the repository checkers,
  which are maintainer tooling.
- `CMakeLists.txt` no longer runs a Python version check at configure time, so
  an ESP-IDF consumer's build does not depend on a Python interpreter. CI checks
  that the generated files are up to date instead.
- `tools/check_repository_hygiene.py` checks durable properties only: no tracked
  generated or one-time artifacts, resolvable Markdown links, valid UTF-8, a
  framework-neutral library core, examples that use the core enum-name helpers
  rather than private string tables, and that CI still runs the guard.
- Documentation cleanup: the validation command list now lives only in
  `CONTRIBUTING.md`, `SECURITY.md` no longer names a specific staged version,
  and the validation status and feature matrix describe the product rather than
  the history of the reviews that produced them.

### Fixed

- Native ESP-IDF CLI main loop no longer starves the idle task. Its delay was
  `pdMS_TO_TICKS(1)`, which is zero ticks at the ESP-IDF default 100 Hz tick
  rate, so `vTaskDelay` only yielded and never let the priority-0 idle task run.
  With a non-blocking `getchar()` console it spun from first boot and tripped
  the idle task watchdog.
- The native ESP-IDF I2C adapter no longer offers a combined write-read
  transfer. It put a repeated START on the mux, which returns the new control
  byte while the switches have not moved. The driver never requested that shape;
  the adapter now refuses it explicitly.
- Both examples now deassert RESET before the pin becomes an output, so no
  spurious reset pulse is emitted at boot. The native example presets the level
  before `gpio_config()`, and the Arduino example registers the pin as an input
  pull-up first because Arduino-ESP32 3.x ignores `digitalWrite()` on a pin that
  `pinMode()` has not yet claimed.
- The native example disables the ESP32 internal pull-ups. At about 45 kOhm they
  cannot drive the 400 kHz bus the example configures, and every active segment
  needs external pull-ups anyway.
- The native stress command prints the same unverified-safe-off failure line as
  the Arduino one, and the HIL runner now treats `safe_off=FAILED` and a nonzero
  `failure=` count as failures. An unverified all-off after stress previously
  reported as a pass.
- The HIL runner no longer fails every soak iteration after a single historical
  fault. It judged `health` output with the status patterns, and `health` prints
  a sticky last-error field.
- Messages the HIL runner matches no longer depend on `LOG_LEVEL`. The Arduino
  bus-scan banner and unknown-command error are printed unconditionally instead
  of through `LOGI`/`LOGE`.
- The native stress loop yields periodically rather than every iteration, which
  previously added at least ten seconds per thousand operations at the default
  tick rate and overran the HIL runner's per-command timeout.
- The Arduino `Wire` result mapping documents that Arduino-ESP32 3.x collapses
  every NACK into one code, and drops a case that duplicated the default.
- Both CLIs use the same `strtoul` range-error policy.
- The ESP-IDF contract checker matches word-anchored patterns instead of bare
  substrings, so `i2c_master_receive` can no longer be satisfied by
  `i2c_master_transmit_receive` and prose may mention USB-Serial-JTAG.
- The HIL parser self-test looks steps up by test id instead of list position,
  so it no longer fails when combined with `--skip-reset`.
- Native ESP-IDF CLI no longer dispatches partial nonblocking `fgets()` input.
- Voltage-translation guidance uses TI's `Vpass(max)` criterion instead of
  treating `VCC` as the clamp voltage.
- Example RESET callbacks return `RESET_ERROR`, inside the documented callback
  result domain, when the RESET pin is not configured.

## [1.0.0] - 2026-07-22

### Added

- First production release of the TCA9548A 8-channel I2C switch driver for the
  Arduino framework, with ESP32-S2 and ESP32-S3 example targets.
- Framework-neutral driver core with an injected, timeout-aware transport; the
  library never owns, configures, or accesses `Wire` directly.
- Configurable addresses from `0x70` through `0x77`, typed channels and masks,
  exact one-byte control-register reads and writes, multi-channel selection,
  verified safe-off, diagnostic probe, and explicit mask-evidence invalidation.
- Managed synchronous lifecycle with bounded `begin()`, no-op `tick()`,
  bus-silent `end()`, manual one-write recovery, and optional timeout-aware
  hardware RESET with exact `0x00` readback verification.
- Distinct transport and public error identities for address NACK, data NACK,
  timeout, bus failure, generic I2C failure, invalid configuration or parameter,
  unsupported operations, and RESET state mismatch.
- Passive READY, DEGRADED, OFFLINE, and UNINIT health diagnostics with
  saturating object-lifetime counters, timestamps, last-error reporting, and no
  hidden retry or application-policy ownership.
- Fixed-storage Arduino bring-up CLI with bounded scan, stress, health, mask,
  recovery, RESET, and safe hardware-in-the-loop commands.
- Native contract tests, framework-neutral and strict-warning compilation,
  ESP32-S2/S3 builds, HIL parser tests, strict Doxygen generation, package
  validation, and pinned GitHub Actions CI.
- Complete public Doxygen comments, integration and hardware guides, security
  and contribution policies, release procedure, and an explicit library-package
  export list.

[Unreleased]: https://github.com/janhavelka/TCA9548A/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/janhavelka/TCA9548A/tree/v1.0.0
