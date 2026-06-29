# TCA9548A I2C Uniformization Prompt

Repository: `TCA9548A`

Absolute path: `C:\Users\Honza\Documents\Projects\TCA9548A`

## Execution Rules

You are working inside this single repository. Implement this prompt directly;
do not repeat the cross-repository audit.

You may spawn subagents for read-only inspection of APIs, tests, I2C
transactions, docs, and diagnostics. Keep final judgment, edits, and
verification in the main agent.

Prefer simple, robust, readable code. Before adding code, inspect whether
existing code can be simplified, reused, tightened, or deleted.

Preserve dirty user changes. Do not commit unless explicitly asked.

## Common Uniformization Target

Apply this shared I2C library contract: injected non-owning transport, `Status` returns, cache-only `getSettings(SettingsSnapshot&) const`, active `probe()`/diagnostics named explicitly, `DriverState` with `state()` and `driverState()`, `isOnline()`, `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`, `totalFailures()`, and `totalSuccess()`.

Keep the common `Err` vocabulary append-only where missing: `OK`, `NOT_INITIALIZED`, `INVALID_CONFIG`, `INVALID_PARAM`, `I2C_ERROR`, `I2C_NACK_ADDR`, `I2C_NACK_DATA`, `I2C_TIMEOUT`, `I2C_BUS`, `DEVICE_NOT_FOUND`, `TIMEOUT`, `BUSY`, and `IN_PROGRESS`. Preserve TCA9548A-specific unsupported, channel-mask, reset, and backoff behavior.

Uniformization is not a new base class or framework. Make only local, source-compatible additions and tests.

## Current State

- Public lifecycle and health are in `include\TCA9548A\TCA9548A.h`: `DriverState` at line 16, `SettingsSnapshot` at line 24, `begin()` at line 64, `probe()` at line 79, `recover()` at line 83, `hardReset()` at line 88, `state()` at line 157, `getSettings(SettingsSnapshot&)` at line 172, and health counters at lines 179-194.
- Optional reset/backoff policy is in `include\TCA9548A\Config.h:54-70`.
- Recovery implementation uses bounded backoff and optional hard reset in `src\TCA9548A.cpp:138-204`.
- Raw and tracked I2C helpers are in `src\TCA9548A.cpp:715-780`; `_updateHealth()` is at `src\TCA9548A.cpp:787`.
- A `driverState()` alias was not found.
- No explicit HIL runner was found.

## Best Sources To Adapt

- Keep TCA9548A as the source pattern for bounded recovery with optional injected hard reset.
- Add the `driverState()` alias from SHT3x/BME280: `SHT3x-main\include\SHT3x\SHT3x.h:227-230`.
- For HIL runner design, adapt PCA9555/BME280 safe-command structure. TCA9548A HIL should emphasize downstream-channel safety.

## Implementation Tasks

1. Add `DriverState driverState() const { return state(); }` next to `state()` in `include\TCA9548A\TCA9548A.h`.
   Preserve existing compatibility aliases; do not remove or rename public APIs to achieve uniform naming.
2. Preserve the optional hard-reset callback and `recoverBackoffMs` behavior. Do not make reset pin ownership implicit.
3. Review `readRegister()` and `writeRegister()` docs. TCA9548A has a single control byte, not a normal addressed register map; docs must prevent misuse.
4. If a diagnostic CLI exists, add a HIL runner covering the common minimum contract: `version`, `scan`, `probe`, `settings`, `health`, failure-token classification, dry-run/parser test support, current mask read, set/clear channel mask, recover/backoff behavior, and optional hard reset only when configured. If no CLI exists, document HIL as not automated.
5. Add tests for `driverState()` and for recover backoff not issuing I2C/reset while gated.

## API Changes Required

- Add only the non-breaking `driverState()` alias.

## Simplifications Before Adding Code

- Do not add dirty-state APIs unless channel-mask cache can diverge in a way not already represented by `lastKnownMask` and recovery behavior.

## Tests To Add Or Update

- Native alias test for `driverState()`.
- Existing native recovery tests should remain passing: `test_poll_recover_backoff_gate_consumes_no_instructions_or_io`, `test_poll_recover_hard_reset_counts_reset_verify_restore`, and `test_sync_io_returns_busy_while_poll_job_active`.
- HIL parser tests only if a real runner is added.

## Commands To Run

- `pio test -e native`
- `pio run -e esp32s3dev`
- If HIL runner is added: dry-run/parser test first; live HIL only with known downstream-safe wiring.

## Constraints And Non-Goals

- Do not reset or reconfigure the upstream I2C bus from the device driver.
- Do not hide retries inside normal channel-selection calls.
- Do not add generic mux frameworks.
- Preserve distinct timeout, address NACK/device-not-found, data NACK, bus, unsupported, reset, and backoff statuses. Do not collapse them into generic `I2C_ERROR` or use `DEVICE_NOT_FOUND` for timeout/data/bus failures.

## Risks And Open Questions

- Open: whether automated HIL can safely toggle mux channels without a standardized downstream fixture.
