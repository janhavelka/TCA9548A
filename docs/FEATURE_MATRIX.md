# TCA9548A Feature Matrix

Last reviewed: 2026-08-08 against Texas Instruments TCA9548A datasheet
SCPS207H, revision H, September 2024.

This matrix separates chip protocol from application bus ownership and
board-level electrical behavior. `Arduino CLI` means
`examples/01_basic_bringup_cli`; `IDF CLI` means the native
`examples/espidf_basic` application. The two examples have the same command
contract but do not share framework-bound transport code.

| SCPS207H behavior | Core/API owner | Arduino CLI | Native IDF CLI | Automated evidence | Documentation / open physical gate |
| --- | --- | --- | --- | --- | --- |
| Strap address `0x70` through `0x77` | `Config::i2cAddress`, `cmd::isValidAddress()`, `cmd::addressFromPins()` | `cfg`; `scan` discovers responding bus addresses | Same | Every valid address and both invalid boundaries; exact callback address | Hardware notes; live strap validation open |
| One direct control-byte read, with no register-address phase | `begin()`, `readChannelMask()`, raw diagnostic `probe()` | `begin`, `read`/`dump`, `probe` | Same | Exact null transmit pointer, zero transmit length, one receive byte, timeout and address | Porting guide; live identity is intentionally not claimed |
| One-byte control write; bit N controls channel N | `selectChannel()`, `writeChannelMask()`, `disableAll()` | `select`, `mask`, `off` | Same | All 8 one-hot values and all 256 mask values use exactly one-byte writes | README and hardware notes; downstream routing HIL open |
| Any combination of channels may be active | `ChannelMask::fromRaw()`, `all()`, `withEnabled()`, `withDisabled()` | `mask <0-255>` | Same | All 256 control bytes plus typed-mask helper boundaries | Hardware notes warn about address conflicts, pull-ups, and combined capacitance |
| New selection becomes active only after ACK followed immediately by STOP | Successful `I2cWriteFn` means the complete transaction, including STOP, finished | Every mutating command uses the driver adapter; no repeated-START shortcut | Same through `i2c_master_transmit()` | Callback shape, exact length, failure ambiguity, and cache invalidation are tested; physical STOP timing remains HIL | Config Doxygen and porting guide |
| Multi-byte write retains only the final byte | Deliberately not exposed: every useful state is expressible safely with one byte | No raw multi-byte command | No raw multi-byte command | Production tests require driver writes to stay exactly one byte; scripted transport models last-byte retention | Hardware notes record silicon behavior; no unnecessary raw API |
| Control byte `0x00` disables all channels | `disableAll()`; `recover()` makes one tracked safe-off write | `off`, `recover`; startup and stress verify write plus readback | Same | Exact `0x00`, no retry/restore in `recover()`, ambiguous failure invalidation | README lifecycle/recovery contract |
| RESET clears the control byte and I2C state machine | Optional bounded `HardResetFn`; `hardReset()` invalidates, invokes once, then requires exact-zero readback | `reset`/`hardreset`; optional `hil run reset` | Same | Missing callback, allowed callback failures, invalid callback result domain, exact zero, mismatch, read failure, no retry/route restore | Hardware notes; RESET timing/wiring HIL open |
| POR clears all channels | No software operation; owner calls `invalidateChannelMask()` after external POR/power work | `invalidate`; `read` reconciles | Same | Cache invalidation is bus-silent and later readback restores evidence | Hardware notes; power sequencing is board-owned |
| No identity register exists | `probe()` is explicitly a raw control-byte response check and does not claim identity or update health | `probe` prints status, observation, and unchanged-health self-test evidence | Same | Success/failure observation and no-health-side-effect regressions | Validation status explicitly limits the claim |
| Downstream visibility follows the active channel mask | Application/bus owner composes mask selection and downstream transfers; the core does not scan or own a bus | `scan` prints the observed active mask, then makes 126 bounded probes; use `select N` first to isolate one branch | Same | CLI contract requires executable scan handlers and matching topology output; HIL parser requires scan completion | Porting guide sole-owner sequence; per-channel fixture validation open |
| Passive mask truth after success/failure/external action | `ChannelMaskObservation` with `UNKNOWN`, `WRITE_COMPLETED`, or `READBACK_OBSERVED` | `health`, `probe`, `read`, `invalidate`, `cfg` | Same | Successful write/read, ambiguous write/read failure, probe, RESET, and explicit invalidation | README mask-observation table |
| Passive transport health without admission control | `DriverState`, timestamps, last error, saturating counters | `health`/`drv`/`state` with state coloring | Same | READY/DEGRADED/OFFLINE recovery, failed begin, saturation, timestamp wrap | README; owner retains retry/recovery policy |
| Bounded diagnostic mutation | Core primitives remain one callback each; example owns diagnostic loops | `selftest` explicitly reads its entry mask, checks all 8 one-hot routes, arbitrary `0xA5`, safe-off recovery, optional RESET, and restores the entry mask; `stress` and `stress_mix` cap at 1000 and finish verified all-off | Same | CLI contract and HIL parser tokens plus native core transaction tests | No physical claims without captured fixture evidence |
| Voltage translation, pull-ups, loading, hot insertion, rise time, and layout | No software API; these are electrical design constraints | Help/scan context cannot validate them | Same | None possible in host tests | Hardware notes summarize SCPS207H; schematic/layout/oscilloscope validation open |

## External-owner integration contract

The library is suitable for a system with one sole I2C task because it is a
non-owning synchronous protocol endpoint:

1. the owner stores the driver and borrowed callback context;
2. the owner serializes every driver call and prevents callback re-entry;
3. each ordinary call performs at most one callback with the configured finite
   timeout and never retries, locks, queues, creates tasks, or configures a bus;
4. the owner may schedule `selectChannel()` or `writeChannelMask()` in one poll,
   perform downstream work in later polls, then explicitly restore its reviewed
   idle mask;
5. controller recreation, external RESET, POR, or other out-of-band route
   changes call `invalidateChannelMask()` before cached mask evidence is used;
6. the owner, not this chip library, owns deadlines spanning multiple calls,
   cancellation, recovery, route leases, result identity, and exactly-once
   completion.

No product-specific task, queue, lock, device table, or downstream protocol is
part of the TCA9548A core.

The maintained CLIs are direct-owner fixture examples. A production console in
a sole-owner architecture must enqueue commands to that owner rather than call
the driver or controller from a second task.

Primary source: [TI TCA9548A datasheet SCPS207H, Rev. H](https://www.ti.com/lit/ds/symlink/tca9548a.pdf).
