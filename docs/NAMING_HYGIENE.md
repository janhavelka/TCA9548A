# TCA9548A Naming And Repository-Hygiene Audit

Audit date: 2026-08-08
Target version: 1.1.4

This audit compares TCA9548A read-only with six clean, mature local I2C
libraries: PCA9555 3.0.2, INA228 3.0.3, INA3221 3.1.0, MB85RC 4.1.0,
RV3032-C7 3.0.1, and LDC1614 3.1.0. The completed OPT4001 and SCD41 naming
audits were also used as consistency evidence. Device-specific behavior and
public source compatibility take precedence over cosmetic uniformity.

## Compatibility Rubric

| Concern | Mature-peer pattern | TCA9548A decision |
| --- | --- | --- |
| Fallible result | Append-only `Err` plus `Status { code, detail, msg }` and a static name helper | Preserve every value and spelling. `errorName(Err)` and `toString(Err)` are already exhaustive and allocation-free. |
| Passive state | `DriverState::{UNINIT, READY, DEGRADED, OFFLINE}` plus a core-owned name | Preserve `DriverState`, `driverStateName()`, and `toString(DriverState)`. |
| Lifecycle aliases | `state()` and often `driverState()`; binding and verified-I2C state are explicit when they differ | Preserve `state()`, `driverState()`, `isBound()`, `isInitialized()`, and `isOnline()` with their current distinct meanings. |
| Health access | Last-success/error timestamps, last error, consecutive failures, and saturating lifetime totals | Preserve the complete direct accessor set. Binding-local telemetry resets at `end()`/rebind; lifetime totals intentionally do not. No peer evidence justifies a second reset API. |
| Lifecycle operations | `begin()`, bus-silent `end()`, diagnostic `probe()`, and explicit `recover()` | Preserve all names and the TCA-specific one-read/one-safe-off behavior. |
| Cache evidence | Typed value plus explicit hardware-evidence provenance | Preserve `MaskProvenance`, `maskProvenanceName()`, `toString()`, `ChannelMaskObservation`, and `channelMaskObservation()`. |
| Transport internals | `_i2c*Raw`, `_i2c*Tracked`, and `_updateHealth` layered below protocol helpers | Preserve the existing layers. `_readControlByteRaw()` is correctly raw with respect to health, while its documented observation update remains intentional. |
| Device helpers | Protocol-specific register/cache helpers should name the actual silicon operation | Preserve `_writeControlByte`, `_readControlByte`, `_recordMask`, and `_resetBindingState`; this one-register switch needs no generic register abstraction. |

No public type, enum, value, field, method, callback, CLI command, or private
core helper was renamed, removed, reordered, or aliased in this pass. Adding a
second name for an already clear operation would increase the compatibility
surface without adding a real shared capability.

## Proven Cleanup

Repository-wide symbol searches established that these example-only symbols
had no caller outside their own declaration:

- `BoardConfig::LED`;
- the legacy boolean `CliShell::readLine()` wrapper;
- `log_bool_str`, `LOG_COLOR_RESULT`, and `LOG_COLOR_STATE`;
- unused debug, trace, and runtime-verbose logging macros and their blue color.

They were removed rather than retained as parallel or speculative interfaces.
The active fixed-line parser, CLI style functions, I2C adapter, scanner, and
error/info/warning logging paths remain unchanged.

## Repository Hygiene

- Current Windows documentation and generated HIL PowerShell commands use
  `scripts\pio.cmd`; Linux CI keeps its separately installed pinned
  `python -m platformio` invocation.
- HIL metadata now queries PlatformIO through the repository wrapper on
  Windows. Clean Git state is recorded as `clean` instead of the previously
  unreachable `unknown`, and parser fixtures no longer embed an obsolete
  library version.
- The package description no longer implies physical production readiness.
  `SECURITY.md` distinguishes the published 1.0.x support line from the staged
  1.1.4 development manifest.
- ASCII punctuation in `AGENTS.md` avoids mojibake in legacy Windows readers
  without changing its engineering rules.
- Generated Doxygen output now has one ignored root-local owner, `.doxygen/`,
  instead of living under the durable `docs/` source tree. No completed prompt,
  NOT-RUN-only report, serial transcript, generated documentation, bytecode, or
  editor database is tracked. HIL reports and transcripts remain explicit,
  opt-in evidence outputs rather than default checkout residue.
- CI runs the durable repository-hygiene guard and pins all checkout steps to
  the audited `actions/checkout` v7.0.1 commit.

The guard rejects drift in the public naming/accessor contract, private
transport layers, duplicate CLI enum mappers, removed zero-call helpers,
Windows PlatformIO entry points, staged metadata, local Markdown links,
tracked generated/one-time artifacts, encoding markers, and the exact checkout
pin count.

A follow-up repository-wide symbol scan found no additional uncalled core or
example helper: every remaining private transport, control-byte, cache, parser,
style, scanner, adapter, and logging path has a concrete caller or framework
entry point. Cleanup therefore stops at the previously proven zero-call paths
instead of deleting active code for cosmetic reasons.

## Evidence Boundary

This patch changes documentation, validation tooling, CI hygiene, and
example-only unused code. It does not change the TCA9548A control protocol,
transport ownership, transaction count, timeout behavior, health semantics, or
public API. Native tests, strict compilation, package consumers, Arduino S2/S3
builds, and native ESP-IDF builds provide source/build evidence only. Physical
routing, RESET wiring/timing, address straps, voltage translation, loading,
hot insertion, fault injection, and long-duration operation remain open until
captured on a reviewed fixture.
