# Changelog

All notable changes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Added a durable naming/repository-hygiene audit and CI guard for the public
  naming contract, local Windows PlatformIO entry point, metadata claims,
  documentation links/encoding, generated residue, and obsolete example paths.
- Added a native ESP-IDF 5.4/5.5 component and fixed-buffer `app_main` CLI for
  ESP32-S2/S3 with full parity to the Arduino command surface and no Arduino
  compatibility layer.
- Added cross-framework CLI and ESP-IDF boundary contract checkers, plus a
  durable validation-status report that records the TI SCPS207H audit and
  explicit no-hardware limitations.
- Added an SCPS207H feature matrix mapping every chip behavior and board-only
  electrical constraint to core ownership, both CLIs, automated evidence, and
  open physical-validation gates.
- Added allocation-free public names for every `Err`, `DriverState`, and
  `MaskProvenance` value, including unknown-enum handling, and appended the
  `RESET_ERROR` status for non-timeout RESET callback failures.

### Changed

- Bumped the staged manifest/component version to `1.1.4` for a documentation,
  Doxygen-output, and repository-residue cleanup; public API and device behavior
  are unchanged and no release tag is created by this audit.
- Moved generated Doxygen output from the durable `docs/` source tree to the
  ignored root-local `.doxygen/` owner and documented HIL reports/transcripts as
  explicit evidence outputs rather than default repository artifacts.
- Bumped the staged manifest/component version to `1.1.3` for this compatible
  naming and repository-hygiene patch; no public API name changed and no
  release tag is created by this audit.
- Aligned current Windows validation and HIL report commands on
  `scripts\pio.cmd`, refreshed the pinned checkout action to v7.0.1, and
  removed an unqualified production-readiness claim from package metadata.
- Bumped the manifest/component API version to `1.1.2` for the backward-
  compatible ESP-IDF/status-name feature set, native CLI input fix, and RESET
  callback/diagnostic hardening; no release tag is created by this audit.
- Replaced the Arduino-only command handler with reusable fixed-buffer
  `CliShell` and `CliStyle` helpers, aligned status/state coloring, exposed
  explicit cache invalidation, and made configuration output report the actual
  driver snapshot rather than the desired global configuration.
- Synchronized `Version.h`, Doxygen, and ESP-IDF component versions from
  `library.json` in the version generator.
- Exact-pinned the ESP32-S2/S3 example and HIL builds to pioarduino
  `platform-espressif32` `55.03.311` (Arduino-ESP32 `3.3.11`, ESP-IDF `5.5.5`,
  GCC `14.2.0`) instead of PlatformIO Espressif32 `7.0.1` (Arduino-ESP32
  `2.0.17`, ESP-IDF `4.4.7`, GCC `8.4.0`).
- Made the example ESP32-S3 QSPI PSRAM configuration explicit, removed the
  legacy ESP32-only PSRAM cache workaround flag, stopped forcing Arduino 3.x
  below its packaged GNU C++20 dialect, updated the S2 reset-mode spelling for
  esptool 5, and added runtime MCU, flash, PSRAM, Arduino, and ESP-IDF identity
  to the example CLI's version report. Native builds continue to enforce the
  library's C++17 contract.
- Initialize the example `Wire` bus at the configured frequency in its bounded
  `begin()` call instead of applying the clock through a separate unchecked
  reconfiguration call.

### Fixed

- Strengthened repository hygiene checks for generated Doxygen trees, completed
  prompts, staged-version documentation, component readiness claims, and the
  exact generated-output contract. A fresh symbol/call-site audit found no
  further dead core or example code after the prior zero-call cleanup.
- Removed zero-call example-only LED, line-reader, logging, color, and
  debug/trace helpers, normalized AGENTS punctuation for Windows readers, and
  replaced the HIL parser's stale example version literal with neutral data.
- Replaced the native ESP-IDF CLI's direct `fgets()` loop with the same shared,
  fixed-storage CR/LF accumulator used by Arduino. The default nonblocking
  ESP-IDF console can no longer spam prompts or dispatch a partial command, and
  overlong lines are discarded completely before the next command is accepted.
- Strengthened the CLI contract checker to prove every command appears in both
  help and actual dispatch, enforce the bounded native input path, and preserve
  matching status/mask formatting across the two frameworks.
- Enforced the documented RESET callback result domain. Valid `TIMEOUT` and
  `RESET_ERROR` details remain intact, while arbitrary driver/lifecycle codes
  are reported as `INVALID_CONFIG` with the original numeric code in detail.
- Expanded both live self-tests from one channel to all eight one-hot masks and
  verified entry-mask restoration on every exit after capture. Bus scans now
  print the active topology, and native tests exercise all 256 control masks
  plus the SCPS207H protocol/timing constants.
- Corrected voltage-translation guidance to use TI's `Vpass(max)` criterion
  instead of treating `VCC` itself as the pass-gate clamp voltage.

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
