# TCA9548A Feature Matrix

Datasheet behavior (TI SCPS207H, Rev. H) mapped to the core API, the CLI
command in both examples, and the native test coverage.
`examples/01_basic_bringup_cli` (Arduino) and `examples/espidf_basic` (native
ESP-IDF) share one command set. Electrical constraints have no software API
and are covered in [HARDWARE_NOTES.md](HARDWARE_NOTES.md); the owner
integration pattern is in [PORTING.md](PORTING.md).

| Datasheet behavior | Core API | CLI command | Native test coverage |
| --- | --- | --- | --- |
| Strap address `0x70` through `0x77` | `Config::i2cAddress`, `cmd::isValidAddress()`, `cmd::addressFromPins()` | `cfg`; `scan` lists responding addresses | every valid address and both invalid boundaries |
| One control-byte read, no register-address phase | `begin()`, `readChannelMask()`, raw `probe()` | `begin`, `read`/`dump`, `probe` | null transmit pointer, zero transmit length, one receive byte |
| One-byte control write; bit N controls channel N | `selectChannel()`, `writeChannelMask()`, `disableAll()` | `select`, `mask`, `off` | all 8 one-hot values and all 256 masks as exactly one-byte writes |
| Any channel combination may be active | `ChannelMask::fromRaw()`, `all()`, `withEnabled()`, `withDisabled()` | `mask <0-255>` | all 256 control bytes plus typed-mask helpers |
| Selection is active after the write ACK followed by STOP | a successful `I2cWriteFn` means the transaction including STOP completed | every mutating command | callback shape and failure invalidation; STOP timing is hardware-only |
| Multi-byte write keeps only the final byte | not exposed; every state is expressible in one byte | none | driver writes are asserted to be exactly one byte |
| `0x00` disables all channels | `disableAll()`, `recover()` | `off`, `recover` | exact `0x00`, no retry or restore, failure invalidation |
| RESET clears the control byte and the I2C state machine | optional `HardResetFn`; `hardReset()` invalidates, calls once, verifies exact zero | `reset`/`hardreset`, `hil run reset` | missing callback, allowed failures, invalid result domain, zero, mismatch, read failure |
| POR clears all channels | owner calls `invalidateChannelMask()` after external power events | `invalidate`, then `read` | invalidation is bus-silent; later readback restores evidence |
| No identity register | `probe()` is a raw control-byte read without health effects | `probe` | observation updated, health unchanged |
| Downstream visibility follows the mask | owner composes mask selection and downstream transfers | `scan` prints the active mask, then 126 probes | CLI contract checker and HIL parser |
| Mask truth after success, failure, or external action | `ChannelMaskObservation` with `UNKNOWN`, `WRITE_COMPLETED`, `READBACK_OBSERVED` | `health`, `probe`, `read`, `invalidate`, `cfg` | successful, ambiguous, probe, RESET, and explicit-invalidation paths |
| Passive transport health | `DriverState`, timestamps, last error, saturating counters | `health`/`drv`/`state` | READY/DEGRADED/OFFLINE recovery, failed begin, saturation, timestamp wrap |
| Bounded diagnostics | core primitives stay one callback each; the examples own their loops | `selftest`, `stress`, `stress_mix` (cap 1000, finish all-off) | CLI contract checker |

Primary source: [TI TCA9548A datasheet SCPS207H](https://www.ti.com/lit/ds/symlink/tca9548a.pdf).
