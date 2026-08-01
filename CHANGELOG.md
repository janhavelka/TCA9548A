# Changelog

All notable changes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

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
