# Changelog

All notable changes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.0.1] - 2026-07-19

### Changed

- Health success/failure totals now retain their documented object-lifetime
  meaning across `end()` and rebinding; binding-scoped state still resets.
- Public `Err` values are explicit and regression-tested to preserve their
  established append-only numeric mapping.
- The example owner forces verified all-off at startup and after HIL/stress
  diagnostics instead of restoring a possibly faulted prior route.
- The example Wire adapter applies each callback's requested timeout before the
  physical attempt; maintenance scan/stress loops yield after each transaction.
- `Doxyfile` project version is now checked and synchronized from
  `library.json` by the existing version generator.

### Fixed

- Live HIL requires RESET validation by default, requires scan completion rather
  than only scan start, treats silent soak commands as failures, and recognizes
  nonzero soak failure counts.
- HIL reports now label free-form change and verification text as unexecuted
  operator assertions instead of captured evidence.
- A failed example `Wire.begin()` no longer falls through into a device
  transaction on an unavailable controller.
- Invalid-parameter and raw-probe tests now lock all required no-health side
  effects, and lifecycle tests lock lifetime-counter continuity.

## [2.0.0] - 2026-07-19

### Added

- Typed `Channel` and one-byte `ChannelMask` value types with constexpr mask
  composition and address-pin helpers.
- `ChannelMaskObservation` with `UNKNOWN`, `WRITE_COMPLETED`, and
  `READBACK_OBSERVED` provenance, plus explicit invalidation for out-of-band
  RESET, POR, power, or controller recovery.
- Narrow `TransportStatus` callback result so transport code can return only
  address NACK, data NACK, timeout, bus, generic failure, or success.
- Independent `Config::resetTimeoutMs` and a timeout-aware hardware RESET
  callback.
- `Err::RESET_STATE_MISMATCH` for a completed RESET whose verification read is
  not exactly `0x00`.
- GitHub Actions coverage for native tests, framework-neutral compilation,
  strict host warnings, documentation, package creation, and ESP32-S2/S3
  example builds.

### Changed

- **Breaking:** transport callbacks now return `TransportStatus` rather than
  public driver `Status`.
- **Breaking:** channel operations now accept or return typed `Channel` and
  `ChannelMask` values.
- `begin()` validates and binds configuration, performs exactly one presence
  read, and preserves a valid binding after transport failure so later owner
  retries do not require reboot or rebind.
- `end()` is bus-silent. Applications that require all-off shutdown must call
  and check `disableAll()` first.
- `recover()` is one explicit all-off write. It never retries, asserts RESET,
  enforces backoff, probes, or restores an old mask.
- `hardReset()` invalidates mask evidence first, invokes RESET once with its
  explicit timeout, verifies an exact `0x00`, and never restores an old mask.
- Health state and counters are passive diagnostics only. `OFFLINE` no longer
  blocks transport operations or owns application admission/recovery policy.
- Probe preserves exact transport errors, performs one read, updates observed
  mask evidence on success, and remains excluded from health accounting.
- The example CLI now uses fixed command storage and the compact typed API.
- Live HIL returns failure when required cases are `NOT_RUN` unless the caller
  explicitly supplies `--allow-not-run`; FAIL and UNKNOWN always fail.

### Removed

- Library-owned recovery backoff and automatic hard-reset policy fields.
- Automatic restoration of a previously selected mask after recovery or RESET.
- Read-modify-write `enableChannels()`, `disableChannels()`, and
  `isChannelEnabled()` operations.
- Raw/register compatibility aliases and redundant control-register methods.
- Poll-chunked mux jobs, downstream callbacks, instruction accounting,
  cancellation, and their internal scheduler state.
- Obsolete example wrappers and health/CLI compatibility helpers.

### Fixed

- Failed or ambiguous writes can no longer present the requested mask as known
  applied hardware state.
- RESET success now requires the datasheet reset value instead of accepting any
  responding control byte.
- Address NACK, data NACK, timeout, bus, and generic transport failures retain
  their distinct causes.
- The example `Wire` adapter no longer mislabels Arduino's generic write error
  as a bus fault and documents that short reads expose no precise cause.
- A failed initial presence read no longer makes the driver unusable until
  application restart.
- Invalid or repeated binding cannot silently replace an existing valid
  configuration.

## 1.0.0 - 2026-07-07 (untagged baseline)

The original managed synchronous driver baseline introduced injected I2C,
channel-mask control, health tracking, RESET support, recovery helpers, an
Arduino CLI example, and native tests. No immutable `v1.0.0` tag or GitHub
release was published; later additions remained on `main` under the same
manifest version. Version 2.0.0 is the first release intended to have a verified
immutable tag.

[Unreleased]: https://github.com/janhavelka/TCA9548A/compare/v2.0.1...HEAD
[2.0.1]: https://github.com/janhavelka/TCA9548A/releases/tag/v2.0.1
[2.0.0]: https://github.com/janhavelka/TCA9548A/releases/tag/v2.0.0
