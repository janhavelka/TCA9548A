# Changelog

All notable changes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Entries describe changes relative to the previous release.

## [Unreleased]

### Added

- Live route-isolation checks (`--route CH:ADDR`), optional 256-mask readback
  sweep, bounded stress/soak runs, and host HIL regressions. Both firmware
  frameworks use the same runner; hardware execution is still pending.
- Host tests for Wire transaction shape, timeout bounds, short transfers,
  error fidelity, numeric CLI input, embedded control bytes, copied driver
  configuration, and probe isolation across health states.
- Standalone CMake target with transitive C++17/include requirements and a
  consumer build test; unrestricted PlatformIO framework/platform metadata
  and ESP-IDF chip targets for the portable core.
- General transport guidance, PCB bring-up provisions, and a fixture/evidence
  procedure in the maintained guides. A shared-owner test verifies independent
  state for multiple mux instances.
- Owner-adapter regressions for deferred initialization, 64-bit deadlines,
  per-call caps, late transfers, and separately budgeted cleanup; a concrete
  integration recipe keeps routing policy in the application.
- Package validation now checks archive contents and builds/runs an isolated
  native consumer with strict dependency compatibility. CI also tests native
  ESP-IDF 5.4.4 and 5.5.5 on both ESP32 targets and checks for unexpected files.

### Changed

- Reduced the default I2C timeout from 50 ms to 20 ms. Both firmware examples
  now inherit the library default. Explicit timeout settings remain supported
  from 1 to 60000 ms; the RESET callback default remains 10 ms.
- HIL configuration checks require and record the firmware's reported I2C
  timeout. Host serial-command deadlines remain separate from transfer budgets.
- Doxygen includes the native example guide and uses GitHub-style heading
  links. Contributor instructions separate checkout-only checks from the
  files available in a distributed library archive.

### Fixed

- RESET HIL now verifies a nonzero seed before the pulse, preventing an
  unwired RESET from passing on an already-disabled mux. Mixed stress checks
  mask values as well as transport success.
- HIL result parsing requires complete prompt-framed responses, consistent
  counts, explicit success, READY health, and verified cleanup. Serial storage
  and waits are bounded; optional transcripts stream to disk.
- Oversized HIL counters fail validation without crashing the runner. Saved
  masks, scan results, and health counts come from their exact response records.
  Transcript errors remain visible in the report and do not prevent bounded
  cleanup while the serial connection remains usable.
- Scans exclude reserved addresses and distinguish bus faults from absent
  targets; failed topology reads abort probing. CLI argument parsing rejects
  signed wraparound and overflow, and control bytes discard the whole line.
- Wire's phase-ambiguous NACK maps to `OTHER`, and callbacks reject unsupported
  shapes and truncating timeouts. Native device registration occurs before
  timed callbacks. Bring-up owners refuse controller reuse after a receive
  fault until restart, guarding the pinned SDK's stale receive-state path.
- Clarified total connected-bus capacitance, RESET completion timing, minimum
  STOP-to-START bus-free time, simultaneous address visibility, backend
  timing/error limits, 100 kHz SDK scan probes, and hardware evidence boundaries.
- Remove previously tracked Doxygen output and ignore its legacy
  `docs/doxygen/` location, keeping generated files out of source control and
  preserving the existing repository-hygiene gate.
- Keep native ESP-IDF build output out of source control and library packages;
  include the Windows PlatformIO wrapper with packaged examples.

## [1.1.0] - 2026-09-07

### Added

- Native ESP-IDF 5.4/5.5 component and an ESP32-S2/S3 `app_main` CLI with
  command parity with the Arduino example. The native example uses bounded
  I2C transfers with STOP, retains ownership after failed device removal,
  requires external pull-ups, and yields to the idle task during console and
  stress operations. The installed library includes the component manifest
  and needs no configure-time Python version check.
- Allocation-free enum names: `errorName()`, `driverStateName()`,
  `maskProvenanceName()`, and matching `toString()` overloads.
- `Err::RESET_ERROR`, appended without changing existing error values, for
  non-timeout RESET callback failures. `hardReset()` accepts only `OK`,
  `TIMEOUT`, and `RESET_ERROR` from the callback and preserves valid failures.
  Another callback code returns `INVALID_CONFIG` with the original code in
  `Status::detail` and no verification I2C.
- A shared fixed-size line accumulator for both example CLIs. Commands run
  only after CR or LF, overlong lines are discarded whole, and numeric input
  follows the same range-error policy on both frameworks.
- Separate native driver and example CLI test suites, CLI and ESP-IDF contract
  checkers, a repository hygiene guard, and a Windows wrapper for the existing
  VS Code-managed PlatformIO installation. CI builds both frameworks on both
  ESP32 targets and checks lifecycle, health, RESET, and no-op `tick()` behavior.
- A feature matrix and validation-status guide distinguishing automated checks
  from validation that requires a hardware fixture.

### Changed

- Simplified the private transport implementation and made `recover()` an
  alias of `disableAll()`, preserving the existing single tracked `0x00` write.
  Published 1.x compatibility helpers and constants remain available.
- Example builds pin pioarduino `55.03.311` (Arduino-ESP32 `3.3.11`, ESP-IDF
  `5.5.5`), with explicit ESP32-S3 QSPI PSRAM configuration. The `version`
  command reports MCU, flash, PSRAM, Arduino, and ESP-IDF identity.
- Both CLIs test all eight one-hot channels and restore the entry mask in
  `selftest`, print the active mask before `scan`, and show the bound driver
  snapshot in `cfg`. The Arduino adapter sets the bus frequency in `begin()`.
- Version generation synchronizes the public version header, Doxygen project
  number, and ESP-IDF component manifest from `library.json`.
- Documented lifecycle and health edge cases, RESET bounds, retained
  compatibility APIs, and conservative adapter error mapping. The contribution
  guide owns the validation commands and release procedure.

### Fixed

- Arduino CLI console output, including style and scan helpers, honors
  `LOG_SERIAL`, matching console input and logging. HIL protocol messages
  remain visible regardless of `LOG_LEVEL`.
- Example RESET setup deasserts the pin before enabling its output, avoiding
  a spurious boot pulse. Unconfigured RESET callbacks report `RESET_ERROR`.
- HIL stress results reject `safe_off=FAILED` and nonzero failure counts;
  historical health errors no longer fail subsequent successful soak steps.
  Parser self-tests also work with `--skip-reset`.
- Hardware guidance now uses TI's `Vpass(max)` voltage-translation criterion
  and corrects STOP versus repeated START behavior, mux-address collisions,
  RESET's effect on stuck downstream targets, the one-byte read limit, POR
  thresholds, high-temperature supply limits, capacitance specifications, and
  aggregate sink current. Expectations without vendor guarantees are identified.

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

[Unreleased]: https://github.com/janhavelka/TCA9548A/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/janhavelka/TCA9548A/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/janhavelka/TCA9548A/tree/v1.0.0
