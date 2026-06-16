# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- `SettingsSnapshot`, `getSettings()`, `isInitialized()`, and `getConfig()` for repo-family runtime/config inspection
- Explicit `hardReset()` API and direct control-register aliases (`readControlRegister()` / `writeControlRegister()`)
- `Config::resetUser` as a separate context pointer for the hard-reset callback
- `readRegister()` and `writeRegister()` register-address-based access methods for sibling-library uniformity
- `Status::is(Err)` method for type-safe error code comparison
- `Status::inProgress()` method for in-progress checks
- `Status::operator bool()` explicit conversion for concise success checks
- `Err::BUSY` and `Err::IN_PROGRESS` error codes
- Missing example helper files `examples/common/CommandHandler.h` and `examples/common/TransportAdapter.h`
- Native core compile environment that builds the library without Arduino/Wire include paths
- Poll-chunked job API with `pollJob(nowMs, maxInstructions, result)` instruction accounting
- Budgeted jobs for raw mask read/write, read-modify-write enable/disable, select/downstream/restore, and recovery

### Changed
- Core driver code is framework-neutral and no longer includes Arduino or falls back to `millis()`
- Without `Config::nowMs`, timestamps remain `0` and recovery backoff is not enforced
- `OFFLINE` now latches normal channel/control APIs, returning `BUSY` without bus I/O until explicit recovery
- `recover()` now keeps/reasserts `OFFLINE` if a recovery attempt that started offline fails partway
- `recover()` now restores the last known channel mask after a successful probe/reset
- `probe()` now reports `NOT_INITIALIZED` before `begin()` and normalizes transport absence to `DEVICE_NOT_FOUND`
- `enableChannels()`, `disableChannels()`, and `isChannelEnabled()` are documented as compound convenience helpers; raw-mask APIs remain the preferred integration surface
- Synchronous bus-touching APIs now return `BUSY` while a poll-chunked job is active
- The CLI example now exposes `read/dump`, `read reg` / `rreg`, `write reg` / `wreg`, `on/all`, `reset/hardreset`, and help entries for `health` / `state`
- The example `Wire` transport now validates buffers and maps transport failures into health-tracked I2C error codes consistently
- README now documents the internal implementation manual, protocol limits, recovery semantics, and the example/public boundary more clearly

### Fixed
- `end()` skips best-effort all-off bus I/O while `OFFLINE`
- `recover()` no longer falls through to a normal probe path after hard-reset callback failure
- Hard reset callbacks no longer reuse the transport `i2cUser` context
- The example `Wire` write-read adapter now checks write byte counts and read availability
- Native tests now include the Arduino/Wire stubs required by the `native` PlatformIO environment
- Native coverage now exercises settings snapshots, explicit hard reset, probe normalization, recover backoff behavior, and poll-job instruction budgets
- Previously defined `test_status_helpers()` and `test_register_helpers()` are now registered in the native test runner

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
