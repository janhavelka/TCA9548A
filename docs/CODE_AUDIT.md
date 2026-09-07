# Open audit decisions

Temporary owner decision list. Remove each item once decided and delete this
file when no decisions remain. Durable behavior and validation belong in
[CHANGELOG.md](../CHANGELOG.md), [HARDWARE_NOTES.md](HARDWARE_NOTES.md), and
[VALIDATION_STATUS.md](VALIDATION_STATUS.md).

## Public API deletions requiring owner approval

All four groups remain published API. Removing any of them is source-breaking
and requires owner approval and a major-version release, even if no production
caller in this repository needs it. Check downstream firmware and sibling
drivers before choosing whether to retain, deprecate, or remove them.

1. `Status::inProgress()` has no in-repository callers and checks
   `Err::IN_PROGRESS`, which this driver never returns.
2. `toString(Err)`, `toString(DriverState)`, and `toString(MaskProvenance)`
   forward to `errorName()`, `driverStateName()`, and `maskProvenanceName()`.
   Tests exercise these aliases; the CLIs use the named helpers.
3. `driverState()` duplicates `state()`. Both CLIs exercise alias parity and
   `tools/check_cli_contract.py` requires it, so removal would also require
   updating those callers and the checker.
4. Unused `CommandTable.h` aliases include `I2C_ADDR_0x70` through
   `I2C_ADDR_0x77`, `ADDRESS_PIN_MASK`, `CH0` through `CH7`, `FIRST_CHANNEL`,
   and `LAST_CHANNEL`. Other protocol constants are referenced only by tests.
   `CONTROL_REG` deserves separate review: the device has no register-address
   phase, and its comment warns never to send this value as a register pointer.

## Other open decisions

- **RESET timeout validation:** retain the documented unconditional
  `resetTimeoutMs` range check, or validate it only when a RESET callback is
  configured. The current rule rejects zero even without a callback and is
  covered by native tests. Recommendation: retain the uniform rule.
- **HIL RESET requirement:** retain the default failure when RESET validation
  cannot run, or permit a skip for an unwired board. Both examples ship with
  RESET disabled. Recommendation: retain the strict requirement and the
  documented fixture configuration; a skip must not become release evidence.
- **Native NACK mapping:** retain the conservative `TIMEOUT`/`OTHER` mapping,
  or narrow receive errors against a tested, pinned ESP-IDF version. The
  current adapter preserves the backend detail and does not infer a NACK phase.
  Recommendation: retain the current mapping until backend behavior is tested.
- **Native scan yielding:** the scan makes 126 bounded, blocking probes with
  no additional explicit yield. Recommendation: retain it unless measurements
  show idle-task starvation.
- **Native bus recovery:** the example does not call `i2c_master_bus_reset()`.
  Recommendation: retain application ownership of controller recovery.
- **CLI checker parsing:** its brace counter does not parse C++ strings or
  comments. It works for the current sources and reports unmatched bodies.
  Recommendation: retain it until a concrete source change requires more.
