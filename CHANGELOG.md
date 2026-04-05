# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- `SettingsSnapshot`, `getSettings()`, `isInitialized()`, and `getConfig()` for repo-family runtime/config inspection
- Explicit `hardReset()` API and direct control-register aliases (`readControlRegister()` / `writeControlRegister()`)
- Missing example helper files `examples/common/CommandHandler.h` and `examples/common/TransportAdapter.h`

### Changed
- `recover()` now restores the last known channel mask after a successful probe/reset
- `probe()` now reports `NOT_INITIALIZED` before `begin()` and normalizes transport absence to `DEVICE_NOT_FOUND`
- The CLI example now exposes `read/dump`, `read reg` / `rreg`, `write reg` / `wreg`, `on/all`, `reset/hardreset`, and help entries for `health` / `state`
- The example `Wire` transport now validates buffers and maps transport failures into health-tracked I2C error codes consistently
- README now documents the internal implementation manual, protocol limits, recovery semantics, and the example/public boundary more clearly

### Fixed
- Native tests now include the Arduino/Wire stubs required by the `native` PlatformIO environment
- Native coverage now exercises settings snapshots, explicit hard reset, probe normalization, and recover backoff behavior

## [1.0.0] - 2026-07-07

### Added
- **First stable release**
- Complete TCA9548A 8-channel I2C switch driver
- Injected I2C transport architecture (no Wire dependency in library)
- Health monitoring with automatic state tracking (READY/DEGRADED/OFFLINE)
- Channel control: selectChannel, setChannelMask, enableChannels, disableChannels, disableAll
- Read-modify-write helpers with no-op optimization
- Channel readback and single-channel query
- Hardware reset callback support via Config
- Manual recovery with backoff enforcement
- Non-blocking tick-based lifecycle
- Address configurability (0x70–0x77)
- Basic CLI example (`01_basic_bringup_cli`)
- Comprehensive Doxygen documentation in public headers
- Unity-based native host tests
- MIT License

[Unreleased]: https://github.com/janhavelka/TCA9548A/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/janhavelka/TCA9548A/releases/tag/v1.0.0
