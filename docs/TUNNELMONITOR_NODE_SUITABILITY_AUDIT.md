# TunnelMonitor-node suitability audit

## 2026-07-19 independent post-release self-audit

This section supersedes stale release evidence in the 2.0.0 closeout below.
The complete original prompt, all 1,062 lines of this report, the complete diff
from baseline `817c8e8225b92b0bc65eb2ebe1c4ff0b91719520`, and both current
repositories were re-read. Independent requirement, core, and docs/CI reviews
were then reconciled against the implementation rather than accepted as proof.

### Exact basis

| Evidence | Exact state |
| --- | --- |
| Published 2.0.0 release | Annotated `v2.0.0` tag object `4b3665f264f4034e8099d66d0d7103345e3945dc`, target `7bf734102390e07a99e1262b3790742ef761bb5f`; remote branch and tag CI both passed host, ESP32-S2, and ESP32-S3 jobs |
| Self-audit patch | Version `2.0.1` release candidate on `hardening/tunnelmonitor-suitability-reaudit`; final tag target and CI evidence are recorded in a post-publication evidence update rather than predicted here |
| TunnelMonitor-node | Clean and equal to its remote branch at `b708f511964db6c51e949e99c67820476f00f9c7` on `docs/mb85rc-suitability-contract-facts`; this commit reverts the preceding FRAM-doc-only commit, so its tree equals baseline `0897f12c1a1369367747d1063936906005391580` |

TunnelMonitor still has a flat, address-only bus and no TCA9548A/PCA9548A
dependency, address, RESET pin, channel map, route identity, or mux health role.
No TunnelMonitor file was changed. Its `I2cTask` remains the sole owner of fixed
queues, per-operation deadlines and transfer caps, retries, recovery, exact
result identity, and take-or-reclaim delivery.

### Gaps found and closed by the self-audit

| Item | Disposition | Evidence |
| --- | --- | --- |
| Lifetime diagnostics | `end()` incorrectly reset counters described by the repository's binding `AGENTS.md` as lifetime totals. Binding reset now preserves saturating object-lifetime totals while clearing binding-local state. | New end/rebind continuity test; native suite is 32/32. |
| Append-only status contract | Existing `Err` values were implicit and only value 14 was tested. Values 0 through 14 are now explicit and fully asserted. | Numeric compatibility test plus unchanged values. |
| No-health paths | Invalid parameters and raw probe failures lacked complete regression assertions. | State, consecutive/total counters, timestamps, last error, cache, and zero-I2C effects are now locked where applicable. |
| Example startup/cleanup | The example inherited a powered mux route at MCU restart and restored its pre-test mask after recover/RESET. It now forces verified all-off after binding and after HIL/stress; failed controller initialization issues no device transaction. | Both firmware targets build; HIL final cleanup is a required token. |
| HIL false positives | Scan start could pass without completion, silent soak commands were not failures, nonzero `failures=` was not recognized, RESET was optional by default, and free-form assertions looked executed. | Parser self-test covers incomplete/completed scan, soak failure classification, RESET-default plan, interrupted identity, and live NOT_RUN/FAIL semantics. Reports label operator text as unexecuted. |
| Timeout and long diagnostics | The example Wire callbacks ignored the per-call timeout and maintenance loops did not yield. | Callbacks apply the validated timeout; scan/stress have fixed transfer maxima, no retry, per-transfer yield, documented worst-case bounds, and verified safe-off for stress. |
| Documentation version | `Doxyfile` duplicated the manifest version without a consistency guard. | The existing generator now checks/synchronizes both `Version.h` and `Doxyfile` from `library.json`. |
| Release pin/evidence | The prior closeout still said “Release-step gate” and documented only a tag name. | The exact 2.0.0 tag object/target is recorded above and README uses the full target SHA. The patch-release evidence follows publication. |

### Complete requirement disposition

- H-01 through H-12 remain resolved in core behavior; the self-audit found no
  new owner, transport, lifecycle, cache, RESET, recovery, deadline, or
  allocation defect in those findings.
- H-13 is resolved for 2.0.0 by the annotated tag and exact target above. The
  2.0.1 patch follows the same immutable-tag and CI process.
- H-14 remains resolved at the library boundary with exact scripted transport,
  fault, ambiguity, lifecycle, threshold, wrap, compile-time, and target-build
  coverage. Product route/cancellation/duplicate-address scan tests remain
  correctly blocked on an approved topology.
- H-15 remains an external physical gate. The harness now fails closed, but no
  attached TCA9548A fixture, routing/isolation trace, RESET fault injection,
  electrical measurement, or soak evidence was produced.
- General multi-step progress, cancellation, request identity, and exactly-once
  result rules remain application-owner responsibilities because every ordinary
  chip operation is already terminal in one callback. `hardReset()` remains the
  sole rare two-callback operation with explicit finite bounds and no retry.

The original workflow's numbered chunks 2 through 7 were intentionally combined
in commit `9ec4e8bfedc320204e6ccb1c37ba1f14d75749c9` because the breaking API,
state, tests, examples, and documentation formed one atomic ownership refactor.
The published tag is not rewritten to manufacture finer historical commits;
this self-audit uses separate focused core, HIL/example, build, release, and
evidence commits.

## 2026-07-19 implementation closeout (supersedes the original disposition)

The original audit remains below as historical evidence, but its line numbers,
"current" API descriptions, and unchanged-library conclusion no longer describe
the 2.0.0 implementation. This closeout revalidated all fifteen findings against
the current repositories and records the implemented disposition.

### Revalidation basis

| Repository/state | Revision | Working-tree evidence |
| --- | --- | --- |
| TCA9548A baseline | `817c8e8225b92b0bc65eb2ebe1c4ff0b91719520` on `main`, equal to `origin/main` | Clean before the closeout branch was created; no local or remote tags |
| TCA9548A implementation | `hardening/tunnelmonitor-suitability-reaudit` | All closeout work is isolated on this branch; release commits and the annotated `v2.0.0` tag are the publication artifacts |
| TunnelMonitor-node baseline | `0897f12c1a1369367747d1063936906005391580` on `develop`, equal to `origin/develop` | Clean and read only at the start of this task |
| TunnelMonitor-node final revalidation | `322a7b2b130da658d9c86ee35afa874b10617939` on `docs/mb85rc-suitability-contract-facts` | The shared checkout was advanced externally during this task; it was clean when re-read, and its three intervening changes concern only FRAM documentation |

No TunnelMonitor-node file was changed by this work. Its current hardware and
contracts still define a flat I2C bus and contain no TCA9548A/PCA9548A address,
RESET pin, channel map, route identity, or mux health role. `I2cTask` remains the
sole bus owner. It owns fixed request/result storage, command deadlines,
exact-result identity, scheduling, retry, health, and recovery. Normal RTC/FRAM
transfers use a 5 ms callback cap; probe/scan and the currently optional devices
use their documented 20 ms cap.

Primary protocol facts were rechecked against TI TCA9548A datasheet revision H:
one control byte with no register-address byte, selection effective after STOP,
and POR/RESET value `0x00`.

### Authority and applicability decisions

The closeout required three explicit authority decisions. For the first two,
original audit recommendations conflict with the binding `AGENTS.md`, so the
more specific repository contract controls:

- `begin()` must check device presence. It therefore validates and binds, then
  performs exactly one tracked control-byte read. A failed read leaves the valid
  binding usable, preserving the safety goal behind H-08 and H-09.
- The four health states and counters must remain. They are now passive
  transport diagnostics only: `OFFLINE` never denies I2C and recovery policy is
  entirely external, preserving the ownership goal behind H-01.
- The former `AGENTS.md` sentence that `recover()` tracks a probe failure
  presupposed the unsafe compound recovery removed for H-02/H-07. The repository
  contract now states the implemented invariant explicitly: `recover()` makes
  one tracked safe-off write, while diagnostic `probe()` remains raw and does
  not update health. This keeps recovery within one owner transaction.

The pasted general requirements for progress jobs, operation deadlines,
cancellation states, stale-result prevention, and exactly-once result retrieval
do not create an internal scheduler for this chip. The TCA9548A has no
conversion, measurement, interrupt, NVM write, calibration, or wait phase. Its
ordinary operations are already terminal in one transport callback. Request
identity and multi-poll route/target/cleanup composition therefore stay with the
external owner, where TunnelMonitor's authoritative contracts already place
them.

### Final operation classes and bounds

| Class | Operations | Maximum work per call | Completion/cancellation contract |
| --- | --- | --- | --- |
| Pure helpers/cache | address helpers, `ChannelMask` helpers, settings/health/observation reads, `invalidateChannelMask()`, `tick()` | Zero I2C, zero waits, fixed memory | Immediate |
| Lifecycle | `begin()`, `end()` | `begin()`: one read; `end()`: zero I2C | `begin()` returns one terminal status; failed presence keeps the binding; `end()` is bus-silent |
| Steady-state | `probe()`, `selectChannel()`, `writeChannelMask()`, `readChannelMask()`, `disableAll()`, `recover()` | At most one timeout-bounded transport callback, no wait/retry | Synchronous terminal result; an owner can cancel only between calls |
| Rare maintenance | `hardReset()` | Exactly one RESET callback and, after callback success, one verification read; default callback budget 10 ms + 50 ms, configurable maxima 60 s + 60 s | No retry or route restore; success requires readback `0x00`; callback failure leaves mask provenance unknown |

Every successful transport return means the physical transaction and its
terminating STOP completed. A caller cannot abort an in-flight synchronous
callback; the transport must terminate within the supplied timeout. There is no
hidden work after a public status returns.

### Finding disposition

| Finding | Revalidated evidence and resolution | Tests/evidence | Status |
| --- | --- | --- | --- |
| H-01 owner conflict | Removed admission gating, recovery backoff, automatic RESET policy, and retry. Kept only the required four-state passive diagnostics; `OFFLINE` still reaches transport. | Two failures reach `OFFLINE`; the next request is issued and success returns `READY`. | Resolved |
| H-02 unsafe restore | `recover()` is exactly one `0x00` write. `hardReset()` never restores an old mask and accepts success only after exact-zero readback. | Exact call counts, safe-off byte, ambiguous recovery failure, no-restore RESET tests. | Resolved |
| H-03 untruthful cache | Replaced `lastKnownMask` with a byte plus `UNKNOWN`, `WRITE_COMPLETED`, or `READBACK_OBSERVED` provenance. Failed/partial writes and reads invalidate it; readback reconciles it. | Ambiguous write with simulated hardware effect, partial read failure, external invalidation, and readback reconciliation. | Resolved |
| H-04 RESET verification | Added timeout-aware RESET callback and `RESET_STATE_MISMATCH`; nonzero readback is retained as verified evidence and returned in `detail`. | Callback failure, read failure, exact zero, and nonzero mismatch stages. | Resolved |
| H-05 cancellation cleanup | Deleted the mux job/cancellation engine. Every primitive is terminal; the owner records and performs cleanup in its own route job. No product route job was invented without a topology. | Zero hidden operations plus exact one-transfer primitive assertions. Product cancellation/cleanup remains a TunnelMonitor integration test after topology approval. | Resolved at library boundary; product gate remains |
| H-06 downstream scheduler | Deleted `PollDownstreamFn`, job state, instruction accounting, and arbitrary downstream callbacks. | Framework-neutral compile and obsolete-symbol search. | Resolved |
| H-07 compound work | Deleted read-modify-write and compound poll APIs. Ordinary methods use at most one callback. The sole rare exception, `hardReset()`, has two explicit bounded stages. | Address/length/value/timeout/call-count assertions for all primitives. | Resolved |
| H-08 lifecycle/rebind | Rebind while bound returns `BUSY` without replacing state. `end()` is repeatable and bus-silent. Safe-off is explicit and fallible. `begin()` retains a valid binding after its required presence read fails. | Invalid-config zero-I2C, transactional rebind, repeated end/rebind, and failed-presence cases. | Resolved with `AGENTS.md` lifecycle constraint |
| H-09 failed presence | Bound and initialized are separate. A failed initial read records the exact error but later tracked primitives and recovery remain callable. | Failed begin followed by successful read/recover without rebind. | Resolved |
| H-10 error collapse | Added narrow `TransportStatus`; address NACK, data NACK, timeout, bus, and other errors map distinctly when the backend exposes them. The part still has no identity register. Arduino `requestFrom()` exposes only a byte count, so its short reads truthfully map to `OTHER`. | Table-driven core error mapping and detail preservation; documented Wire limitation. | Resolved |
| H-11 STOP contract | Public callbacks now require completed STOP before success. The example uses `endTransmission(true)` and read completion; no operation queues work after returning. | Fake transport checks exact address, null/no-prefix read shape, lengths, byte, timeout, and context. Electrical STOP timing remains HIL evidence. | Resolved in API/adapter; physical gate remains |
| H-12 dual clocks | Removed recovery backoff and its second clock. The optional clock now timestamps passive health only; no deadline arithmetic depends on it. | `UINT32_MAX -> 0 -> 1` timestamp-wrap test. | Resolved |
| H-13 immutable release | Bumped the breaking API to 2.0.0, regenerated `Version.h`, pinned release validation platforms, and documented exact tag pinning. The final release step creates and verifies annotated tag `v2.0.0` at the complete closeout revision. | Version check, package creation, remote branch/tag verification in release workflow. | Release-step gate |
| H-14 transport/topology tests | Replaced the broad legacy suite with a fixed-capacity scripted transport that records exact transactions and models ambiguous hardware effects. Added GitHub Actions host and S2/S3 jobs. Product branch routing cannot be tested until a topology exists. | 31 native cases, including accepted 60 s bounds, threshold 255/failure-counter saturation, illegal asynchronous RESET response, plus compile-time size/copy/portability assertions and CI workflow. | Resolved for library; product topology gate remains |
| H-15 live hardware | The runner now fails live mode when required steps are `NOT_RUN` unless `--allow-not-run` is explicit; FAIL/UNKNOWN always fail. No fixture was attached during this task. | Parser contract: 8 steps; dry-run success; unavailable live fixture exit 1; explicit override exit 0. | External physical gate remains |

### Local closeout verification

| Check | Result |
| --- | --- |
| Generated `Version.h` consistency | PASS, 2.0.0 |
| Native scripted transport suite | PASS, 31/31 |
| Framework-neutral core build | PASS |
| Strict C++17 core compile | PASS with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror` |
| ESP32-S2 example, official `platformio/espressif32@7.0.1` | PASS, 26,876 B RAM / 277,086 B flash |
| ESP32-S3 example, official `platformio/espressif32@7.0.1` | PASS, 19,452 B RAM / 285,841 B flash |
| HIL parser self-test | PASS, 8 planned steps |
| Doxygen | PASS with warnings treated as errors |
| PlatformIO package | PASS, `TCA9548A-2.0.0.tar.gz` |

The remaining gates are genuinely external: approve a real TunnelMonitor board
topology and electrical budget; define mux address, RESET wiring, channel/idle
map, direct versus routed devices, and required/optional health identity; exact-
pin the reviewed tag in that product only after approval; and perform ESP32-S3
HIL for routing/isolation, duplicate downstream addresses, STOP sequencing,
400 kHz signal integrity, RESET, stuck-branch recovery, cancellation cleanup,
and soak behavior.

---

## Historical audit (2026-07-18; superseded by the closeout above)

## TCA9548A 8-channel I2C switch library

Date: 2026-07-18

Audit result: **good protocol base, not suitable unchanged; product topology and
focused library refactor are required before integration**

The current library correctly implements the TCA9548A's small wire protocol. It
uses injected transport callbacks, sends one control byte without a register
address, performs read-only control-byte reads, has fixed memory use, and builds
without Arduino or `Wire` in the core.

The current safety and ownership model does not fit TunnelMonitor. The library
owns an `OFFLINE` latch, recovery backoff, reset policy, and an arbitrary
downstream-job scheduler. `I2cTask` already owns those responsibilities. More
importantly, current recovery restores the previously selected mask after RESET.
That can immediately reconnect the branch that caused a stuck bus. Cancellation
can also leave a branch selected, and several failure paths report a requested
mask as known even when the hardware did not confirm it.

There is a separate product gate. Hardware revision `2.0.0` has no TCA9548A,
mux address, RESET pin, channel map, route model, or health identity. The current
firmware assumes one flat I2C bus. Platformization alone is not a reason to add a
mux.

The recommended direction is deliberately small:

1. Decide the real board topology and why a mux is needed.
2. Reduce the library to truthful, one-transaction control-byte primitives.
3. Put route selection and safe cleanup inside the existing `I2cTask` active-job
   owner.
4. Release and exact-pin the refactored library.
5. Prove routing, isolation, RESET, and recovery on actual hardware.

Do not integrate the current API and hide its ownership conflicts in an adapter.

## Audit basis

| Repository | Revision | Notes |
| --- | --- | --- |
| TunnelMonitor-node | `fff99fe17e60b9287ec4d8d3eca5b3230ae44223` | Branch `prompt-44b-sequence`; architecture authority and current flat-bus I2C owner |
| TCA9548A | `349c5298d2fa8fe621124207271720a926654517` | Branch `main`; matches `origin/main` during the audit |

### Latest-branch revalidation

On 2026-07-18, `origin` was fetched again with remote-branch pruning and tags.
`origin/HEAD` still selects `origin/main`, `origin/main` is the only remote
branch, and local `main` is exactly `349c5298d2fa8fe621124207271720a926654517`.
The local branch is zero commits ahead and zero commits behind. The remote still
publishes no tags.

There is no code delta between the revision audited by this report and the
current final checkout. A fresh full-source re-audit still re-read the final
configuration, public API, core implementation, native tests, release metadata,
and HIL tooling instead of relying only on the matching commit labels. It
reconfirmed all fifteen hard findings at their stated priority, including unsafe
mask restore after reset, untruthful cached-mask state, missing exact reset
verification, cancellation that can strand a branch, owner-policy conflicts,
weak transport-contract tests, the absent immutable tag, and the lack of live
hardware evidence. No finding, release gap, or recommendation changed. This
report remains the only untracked worktree file.

The TCA9548A checkout identifies itself as `1.0.0` in `library.json` and
`Version.h`. `CHANGELOG.md` claims a stable `v1.0.0` release, but the local and
remote repositories have no tags. Current `main` also contains changes listed
under `[Unreleased]`. There is therefore no immutable `v1.0.0` release to pin.
Findings in this report apply to the full commit above.

Primary chip behavior was checked against the current Texas Instruments
[TCA9548A datasheet](https://www.ti.com/lit/ds/symlink/tca9548a.pdf), revision H,
document SCPS207H. PDF pages 7, 12, 14, 16 through 18, and 21 were rendered and
visually reviewed. They cover Fast-mode timing, RESET/POR behavior, the
one-byte control protocol, the required STOP boundary, and branch pull-up and
capacitance constraints. Poppler was unavailable, so PyMuPDF was used. Temporary
PDF and render files were removed after review.

The TunnelMonitor checkout had a pre-existing edit to `.vscode/extensions.json`.
It was not changed. No TunnelMonitor source or configuration was edited. The
only intended source-tree change from this audit is this report in the TCA9548A
repository.

## Decision summary

### Current product decision

**Do not add TCA9548A to hardware revision `2.0.0` or to production dependencies
from this audit alone.** The documented board does not contain the part, and no
current requirement identifies which devices need isolation or duplicate-address
support.

If a later board adopts one TCA9548A, the current library is a useful refactor
base. It should not be integrated unchanged.

### Release gates

1. Freeze one concrete first-stage mux topology: mux address, direct devices,
   channel assignments, RESET wiring, and required/optional roles.
2. Make configuration/binding perform no I2C. Device absence at boot must not
   make the driver unusable until reboot.
3. Keep only operations that perform at most one bounded I2C transaction:
   `readMask`, `writeMask`, `selectChannel`, and `disableAll`.
4. Remove library-owned `OFFLINE`, health counters, recovery backoff, automatic
   reset recovery, and downstream callback scheduling.
5. Never claim that the applied mask is known after an ambiguous or failed
   write. Either remove the cache or add explicit known/unknown state.
6. RESET verification must read exactly `0x00`. Recovery must leave all channels
   off; the owner selects a new route deliberately.
7. State in the public transport contract that a successful control-byte write
   includes the terminating STOP condition.
8. Preserve address NACK, data NACK, timeout, and bus errors. Do not convert all
   of them to device absence.
9. Add a small owner-private route model in TunnelMonitor. Keep the physical
   `I2cBackend` route-agnostic and keep library types out of public contracts.
10. Make all-off the startup, recovery, cancellation-cleanup, scan baseline, and
    idle policy unless a reviewed product requirement says otherwise.
11. Cut a real SemVer release after the breaking refactor and exact-pin the
    immutable revision in TunnelMonitor.
12. Complete native integration tests and live ESP32-S3 HIL for real routing,
    isolation, RESET, fault recovery, and 400 kHz operation.

### Avoid adapter band-aids

Do not solve the findings by:

- setting `offlineThreshold` very high;
- calling library `recover()` after every owner recovery;
- trusting `lastKnownMask()` after a timeout, reset, cancellation, or failed
  restore;
- using `cancelPollJob()` and assuming the mux returned to the restore mask;
- wrapping `PollDownstreamFn` around TunnelMonitor device jobs;
- adding a public `select mux channel` command and making callers manage routes;
- duplicating the TCA control-byte protocol in both the adapter and library;
- keeping both flat and routed implementations for the same device after the
  routed path is qualified;
- adding a generic topology graph, cascaded-mux framework, registry, or second
  I2C owner; or
- accepting the dry-run HIL report as hardware evidence.

## What already fits

These parts are sound and should be retained:

- Core headers and source do not include Arduino, ESP-IDF, FreeRTOS, or `Wire`.
- The bus is supplied through function pointers and a caller context.
- Each transport call carries a finite timeout.
- The driver does not configure pins, clock rate, or an I2C controller.
- Core code has fixed-size state and no steady-path heap allocation.
- The control write is one byte and sends no register address.
- The control read uses a read-only transaction with zero transmit bytes.
- Address validation correctly accepts only `0x70` through `0x77`.
- Channel masks correctly map bit N to channel N.
- `selectChannel()` writes a one-hot mask and disables other channels.
- Multiple-channel masks remain available for legitimate chip-level use.
- Native tests exercise the main public APIs, health state, recovery, and
  poll-job instruction accounting.
- Native, framework-neutral, ESP32-S2, and ESP32-S3 builds pass.
- Strict host warnings and generated-version consistency checks pass.

The wire protocol does not need a rewrite. The ownership and state model does.

## TunnelMonitor requirements and product gaps

### Current documented hardware

TunnelMonitor hardware revision `2.0.0` defines one direct I2C bus on GPIO8 and
GPIO9. `BoardPins` defines these five endpoints:

| Device | Address | Current role |
| --- | ---: | --- |
| OLED | `0x3C` | optional |
| INA228 | `0x41` | optional |
| FRAM | `0x50` | required |
| RTC | `0x51` | required |
| BME280 default | `0x76` | optional |

There is no mux address, RESET GPIO, mux power control, direct-versus-downstream
map, or channel assignment in `include/TunnelMonitor/BoardPins.h` or
`docs/guidelines/reference/hardware_and_build_facts.md`. Repository-wide search
found no TCA9548A/PCA9548 route in the firmware, tests, guidelines, or
`platformio.ini`.

This is a product gap, not a defect in the TCA library. A board/topology decision
must come before firmware integration.

### Existing owner contract

| TunnelMonitor rule | Current evidence | Consequence for TCA9548A |
| --- | --- | --- |
| One I2C owner | `docs/guidelines/i2c_peripherals.md:28-45` | Only `I2cTask` invokes TCA transport. The library owns no bus, task, retry, health, or recovery policy. |
| One normal transfer per poll | `docs/guidelines/i2c_peripherals.md:100-133` | Route selection, target work, and cleanup advance as separate owner phases. |
| Fixed time bounds | `include/TunnelMonitor/i2c/I2cConfig.h:60-75` | Normal callbacks use the current 20 ms cap and retain the command's 64-bit deadline. |
| ESP-IDF backend | `docs/guidelines/i2c_peripherals.md:38-45` | The adapter maps to `I2cBackend`; no `Wire` path is added. |
| Private library types | `docs/guidelines/ownership.md:271-290` | TCA types do not appear in public commands, results, services, CLI, or web APIs. |
| Exact production pin | `docs/guidelines/dependency_policy.md:8-12,83-90` | A reviewed commit/tag and native/HIL proof are required before activation. |
| Fixed memory | project `AGENTS.md`; existing owner queues | Route state and diagnostics use fixed structs, not dynamic containers. |

### The current firmware is flat-bus only

`I2cTransfer` contains only an address and buffers
(`include/TunnelMonitor/i2c/I2cBackend.h:26-33`). That is correct for the
physical backend and should stay unchanged.

The higher owner layer is also address-only today:

- `I2cKnownDeviceSpec` contains `{key, device, address, role}` and has no route
  (`include/TunnelMonitor/i2c/I2cDiagnostics.h:117-122`).
- `findI2cKnownDeviceByAddress()` assumes an address identifies one logical
  device.
- RTC, FRAM, ENV, INA228, and OLED jobs use direct board addresses.
- `Scan` returns one 128-bit address bitmap and probes whatever topology happens
  to be connected.
- `Probe` has no route field.

With a mux, the logical identity is at least `(route, address)`. The same target
address may legally exist on different isolated channels. The backend still
only needs the physical address because route selection is an owner phase.

### Minimal TunnelMonitor integration shape

Support one explicitly configured first-stage mux first. Do not add cascaded
muxes or a general graph until a real board needs them.

An owner-private type is sufficient:

```cpp
enum class I2cRoute : uint8_t {
  Direct,
  MuxChannel0,
  MuxChannel1,
  MuxChannel2,
  MuxChannel3,
  MuxChannel4,
  MuxChannel5,
  MuxChannel6,
  MuxChannel7,
};

struct I2cEndpoint {
  uint8_t address{0};
  I2cRoute route{I2cRoute::Direct};
};
```

The exact representation can be smaller, but it must be fixed and typed. Put
the device-to-endpoint table beside current I2C diagnostics/board facts. Do not
put routes in the physical backend.

For one downstream operation, `I2cTask` should:

1. Resolve the compile-time endpoint.
2. If the known applied route is different or unknown, write the desired one-hot
   mask.
3. On an ambiguous write result, mark route state unknown and reconcile by a
   later control-byte read. Do not blindly retry a possibly committed write.
4. Keep the active route stable while the existing device job runs.
5. On success or target failure, write the reviewed idle mask, normally `0x00`.
6. If cleanup fails, retain both the target result and cleanup result, mark the
   route unknown, and block unrelated work until owner recovery makes topology
   safe.

The existing exclusive active-job envelope is the right owner mechanism. The
TCA library does not need to schedule the downstream driver.

### Startup and idle policy

An MCU restart does not prove that the TCA9548A was power-cycled. A previously
selected channel can remain connected. The current library `begin()` reads and
accepts that mask (`src/TCA9548A.cpp:80-93`). That is useful observation, but it
is not a safe TunnelMonitor startup state.

For unattended operation, use this policy unless the board decision says
otherwise:

- bind the library without I2C;
- force `0x00` before any routed target access;
- read back `0x00` in a later owner poll;
- treat the route as unknown until readback succeeds;
- select only one channel for a target operation; and
- return to `0x00` at idle.

If RESET is wired, owner startup may assert RESET first, then verify `0x00`.

### Address policy

TCA9548A straps select `0x70` through `0x77`. The mux remains visible upstream
while a downstream channel is selected. Therefore its address must not equal a
visible downstream target address.

The current BME280 default is `0x76`. Do not strap the mux to `0x76` if that BME
can be connected through an enabled channel. Freeze the chosen mux address in
board facts; do not make physical address straps a casual runtime setting.

Repeated target addresses on different channels are valid only when policy
keeps those channels isolated. TunnelMonitor should use one-hot selection and
must not enable two same-address branches together.

### RESET and branch-fault isolation

TI defines RESET as active-low. RESET/POR clears the control register to `0x00`
and deselects all channels. This is the feature that can isolate a downstream
branch holding SDA or SCL low.

TunnelMonitor's current recovery resets/reinitializes the ESP I2C controller and
can pulse the upstream lines. It has no mux RESET/power hook. If a selected
branch is holding the bus low, software may be unable to send a deselect command.
Upstream SCL pulses also reach that selected branch and do not guarantee
isolation.

For an unattended board that adopts the mux for fault containment, wiring RESET
to an owner-controlled GPIO is a hardware admission requirement. Record the pin,
active-low polarity, pull-up, and boot state in `BoardPins`. A simple recovery
sequence is:

1. assert/release mux RESET;
2. mark route state unknown;
3. recover/reinitialize the upstream controller if still needed;
4. read back `0x00`;
5. resume direct work; and
6. select a downstream branch only for a new admitted operation.

Do not automatically restore the branch that was active at the fault.

### Health and product role

If the mux is adopted, decide whether it is a distinct health device. Its role
depends on the topology:

- if required RTC or FRAM is downstream, the mux is required infrastructure;
- if only display and optional sensors are downstream, it can be optional
  infrastructure; and
- if the part is not populated for the profile, it should not consume a live
  health row.

Current production device health already uses its fixed capacity of 16 entries
(`include/TunnelMonitor/contracts/Capacities.h:81-90`). Adding a mux identity
requires an append-only `DeviceId`, known-device count/table change, capacity and
JSON review, and tests. Do not silently report mux-select NACK as target-device
absence.

If board routing permits it, keep required RTC and FRAM direct upstream and put
optional/fault-prone endpoints behind the mux. This minimizes the blast radius
without creating a more complex software model.

### Electrical admission facts

The chip supports 400 kHz, matching TunnelMonitor's configured bus speed. That
does not prove the populated board at 400 kHz.

Before release, record and verify:

- VCC and voltage domain for the upstream bus and every populated branch;
- external pull-up values for each branch;
- rise time and total capacitance for each selected path;
- RESET pull-up and GPIO behavior during ESP reset; and
- the effect of any level translation.

TI specifies a 400 pF Fast-mode bus-capacitance limit. When several channels are
enabled, their devices, wiring, and pull-ups contribute together. TunnelMonitor
should therefore use one-hot channel masks unless a concrete, electrically
validated broadcast use case exists.

## Hard library findings

### H-01: library health and recovery conflict with the I2C owner

Priority: integration blocker

`Config` contains `offlineThreshold`, `recoverBackoffMs`, and
`recoverUseHardReset` (`include/TCA9548A/Config.h:65-70`). Transport failures
drive `READY`, `DEGRADED`, and `OFFLINE` state. Once offline, tracked operations
are rejected (`src/TCA9548A.cpp:747-780`).

This duplicates TunnelMonitor's owner health, retry, backoff, and recovery. It
also creates a safety failure: after `I2cTask` repairs the bus, the library can
still reject `disableAll()` because its separate offline latch remains set.

Required refactor:

- remove health admission control, recovery backoff, and bus-recovery policy
  from the chip library;
- return each transport result truthfully; and
- let the sole owner decide health, retry, and recovery.

Optional counters may exist as passive observations, but they must never block a
requested isolation write.

### H-02: recovery can reconnect the faulted branch

Priority: integration blocker

Synchronous `recover()` saves `_lastKnownMask`, invokes hard reset, then restores
that mask (`src/TCA9548A.cpp:146-179`). Poll recovery restores the same desired
mask (`src/TCA9548A.cpp:470-551`). Existing tests explicitly expect this.

RESET's safety value is that it disconnects every branch. Restoring the previous
mask can immediately reconnect the branch that wedged the bus.

Required refactor:

- recovery/reset leaves `0x00` applied;
- route state is invalidated and then verified as all-off; and
- only the owner selects a branch for the next admitted operation.

### H-03: `lastKnownMask` is not always known

Priority: integration blocker

The cache is assigned the requested mask after failed restore or verify paths
even though hardware did not confirm it (`src/TCA9548A.cpp:177-179,198-200,
488-489,504-505,527-529,545-547`). `end()` ignores its best-effort write result
and then reports zero (`src/TCA9548A.cpp:102-114`). `probe()` successfully reads
the current byte but discards it (`src/TCA9548A.cpp:121-135`).

A timed-out control write is ambiguous: STOP and the byte may have reached the
device even if the callback returned timeout. Both the old and requested masks
are unsafe assumptions.

Required refactor:

- preferably remove the driver cache and let the owner hold route state; or
- split requested mask from observed mask and add explicit validity;
- invalidate observation after every ambiguous write, RESET, POR, bus recovery,
  backend reinitialization, and cancellation with incomplete cleanup; and
- update observation only from a successful read or an unambiguous successful
  write whose transport contract completed STOP.

### H-04: hard reset does not verify the reset value

Priority: integration blocker

The public contract says the device should return with all channels disabled
(`include/TCA9548A/TCA9548A.h:85-88`). `hardReset()` reads the control byte but
accepts any value (`src/TCA9548A.cpp:204-226`). Poll recovery can even accept a
no-op reset when the read value equals the prior desired mask.

Required refactor:

- a reset verification read must equal `0x00`;
- any other value returns a distinct reset-state mismatch; and
- no previous branch mask is restored automatically.

### H-05: cancellation can strand a selected channel

Priority: integration blocker

`cancelPollJob()` only clears software state (`src/TCA9548A.cpp:574-576`). If
selection already completed, the branch remains connected and restore is
skipped. A downstream callback that reports instruction use above its budget
also clears the job without cleanup (`src/TCA9548A.cpp:419-431`).

This is not fixable by making cancellation perform hidden I2C. Cancellation is
needed specifically when a deadline has expired and the owner must control the
cleanup sequence.

Required refactor:

- remove the downstream scheduler from the chip library;
- let `I2cTask` record that safe-off cleanup is required; and
- do not admit unrelated transfers until cleanup succeeds or RESET isolates all
  branches.

### H-06: the downstream callback engine crosses the chip boundary

Priority: integration blocker

`PollDownstreamFn` allows the TCA driver to poll arbitrary downstream work and
account for its instruction budget (`include/TCA9548A/TCA9548A.h:37-52,
225-247`). This makes a one-byte switch driver a second scheduler. It duplicates
`I2cTask` active-job ownership and couples the library to every downstream
driver's lifetime and status rules.

The callback may return `IN_PROGRESS` indefinitely because the library job has
no whole-operation deadline. TunnelMonitor already has a wrap-safe 64-bit
command deadline and is the correct place to enforce it.

Restore failure also replaces the original downstream result
(`src/TCA9548A.cpp:445-452`). Field diagnostics then lose whether the target
failed, cleanup failed, or both.

Required refactor:

- delete `PollDownstreamFn` and select/downstream/restore jobs;
- keep target scheduling in `I2cTask`; and
- retain separate bounded fields for target result, cleanup result, and final
  route validity.

### H-07: compound APIs exceed the normal owner work shape

Priority: required refactor

Synchronous `recover()` may invoke RESET, read, and write in one call.
`enableChannels()` and `disableChannels()` perform read-modify-write in one
call. Poll jobs exist beside those synchronous methods while `tick()` is a
no-op. The library therefore exposes two lifecycle models.

The read-mask poll job also stores caller-owned output storage until a later
poll completes (`include/TCA9548A/TCA9548A.h:212-214`). That lifetime contract
is avoidable for a single-read device.

The TCA9548A itself needs no long state machine. The simple fit is one transport
callback per fallible primitive. Remove synchronous compound I/O and the generic
poll-job engine. Pure mask helpers can replace bus-level read-modify-write
conveniences.

### H-08: configuration, rebind, and shutdown are not transactional

Priority: required refactor

`begin()` clears the valid live state before validating the replacement config
(`src/TCA9548A.cpp:44-75`). An invalid second `begin()` can leave hardware
connected while software reports uninitialized with a zero cache.

`end()` performs hidden best-effort I2C, discards its result, then claims zero
and uninitialized (`src/TCA9548A.cpp:102-114`). It is neither a truthful
shutdown operation nor a bus-silent state reset.

Required refactor:

- validate and bind configuration without I2C;
- reject rebind while bound, or validate into a temporary before replacing
  state;
- make safe-off an explicit fallible operation; and
- make unbind/end bus-silent and mark applied state unknown.

### H-09: failed initial presence prevents later recovery

Priority: required refactor

`begin()` requires a successful control-byte read before `_initialized` becomes
true. `probe()`, recovery, and normal operations then reject uninitialized use.
An optional device absent at boot cannot be probed or initialized later through
the normal API without repeating the destructive lifecycle call.

Required refactor:

- separate `bound/configured` from `responding/applied-state-known`;
- allow `readMask` or a compatibility probe after binding; and
- let the owner handle disappearance and reappearance without reconstructing
  the driver or rebooting.

### H-10: presence mapping destroys the root cause

Priority: required refactor

`begin()` and `probe()` turn address NACK, data NACK, timeout, and bus errors into
`DEVICE_NOT_FOUND` (`src/TCA9548A.cpp:80-87,129-135`). Those conditions need
different field action. A missing optional mux is not the same as a stuck shared
bus.

Required refactor:

- preserve narrow transport outcomes: address NACK, data NACK, timeout, bus
  fault, and other failure;
- map only address NACK to absence in the TunnelMonitor adapter; and
- do not treat library job states such as `BUSY` or `IN_PROGRESS` as transport
  health failures.

The part has no identity register. A successful control-byte read proves that a
compatible responder exists at the address; it cannot prove the exact chip
model. Document that limitation and treat exact identity as a board fact.

### H-11: STOP completion is not in the public callback contract

Priority: required refactor

TI states that the selected channel becomes active only after STOP. The next
downstream START must not occur until that STOP has completed.

`PORTING.md` mentions STOP, but the public `I2cWriteFn` contract only says
"I2C write" (`include/TCA9548A/Config.h:13-21`). A transport adapter could
return success after queueing or use a no-STOP form without violating the header
as written.

Required refactor:

- state that success means the one-byte transaction and terminating STOP are
  complete; and
- add fake-transport assertions for address, length, value, timeout, and STOP
  semantics where the backend can expose them.

### H-12: recovery time can use two unrelated clocks

Priority: cleanup with recovery removal

Synchronous recovery records `Config::nowMs`; poll recovery records the caller's
`pollJob(nowMs)`. Both use `_lastRecoverMs` without requiring the same epoch
(`src/TCA9548A.cpp:148-158,455-463`). Backoff is silently not enforced when the
optional time hook is absent, despite a nonzero default.

Removing library-owned backoff removes this problem. If a future standalone
policy remains, it must use one required monotonic clock and wrap-safe deadline
semantics.

### H-13: the declared release is not immutable

Priority: release blocker

`library.json` and `Version.h` say `1.0.0`. `CHANGELOG.md` links to a `v1.0.0`
release, but neither the local nor remote repository contains that tag. The
manifest has reported `1.0.0` since the first commit while later public APIs are
listed as unreleased.

TunnelMonitor production dependencies must be exact-pinned. After this breaking
refactor:

- update SemVer as a breaking release;
- generate `Version.h` from the manifest;
- create and verify the real immutable tag;
- document the full commit SHA; and
- exact-pin that reviewed revision in `platformio.ini`.

The README's current unpinned Git URL is not a production pin. The library's own
PlatformIO `espressif32` platform is also unpinned; pin it for reproducible
release validation.

### H-14: current tests do not model the transport contract or topology

Priority: release blocker

All 70 native tests pass, but the fake transport largely ignores address,
buffer lengths, timeout, transmitted data, and STOP behavior. It models one
control byte, not branch routing or ambiguous commit.

Missing tests include:

- exact address, read/write lengths, timeout, byte, and no register prefix;
- failed/ambiguous write invalidates applied state;
- reset readback other than `0x00` fails;
- invalid rebind preserves an existing valid binding;
- timeout and bus-stuck remain distinct from absence;
- cancellation after select requires owner cleanup;
- downstream failure plus cleanup failure retains both causes;
- route cache invalidation after controller recovery and mux RESET;
- duplicate target addresses on different one-hot channels; and
- deterministic upstream-only and per-channel scans.

There is no repository CI workflow or coverage report. Passing local tests is
useful evidence, not proof that the missing fault paths are covered.

### H-15: there is no live hardware evidence

Priority: integration gate

`docs/reports/hil-validation-COM8-20260629.md` is explicitly a dry run: 0 PASS,
0 FAIL, 0 UNKNOWN, and 8 NOT_RUN. It contains no upload, board transcript,
timing, routing target, reset test, or soak.

The host runner can also exit success when a live serial fixture is unavailable
and all required cases become NOT_RUN. Live mode should fail if required cases
are not run, unless the caller explicitly requested dry-run or allow-not-run.

Current HIL commands mainly mutate/read the control byte. They do not prove the
chip's main purpose: an enabled branch becomes reachable, disabled branches stay
isolated, STOP separates select from downstream access, and RESET isolates a
stuck branch.

## Recommended library API after refactor

The final names are less important than the ownership. Keep the public surface
small and explicit.

### Core types

```cpp
enum class Channel : uint8_t {
  Ch0 = 0,
  Ch1 = 1,
  Ch2 = 2,
  Ch3 = 3,
  Ch4 = 4,
  Ch5 = 5,
  Ch6 = 6,
  Ch7 = 7,
};

class ChannelMask {
 public:
  static constexpr ChannelMask none();
  static constexpr ChannelMask all();
  static constexpr ChannelMask one(Channel channel);
  static constexpr ChannelMask fromRaw(uint8_t raw);

  constexpr uint8_t raw() const;
  constexpr bool contains(Channel channel) const;
  constexpr bool isNone() const;
  constexpr bool isOneHot() const;
  constexpr ChannelMask withEnabled(Channel channel) const;
  constexpr ChannelMask withDisabled(Channel channel) const;
};

enum class TransportErr : uint8_t {
  Ok,
  NackAddress,
  NackData,
  Timeout,
  Bus,
  Failed,
};

struct TransportStatus {
  TransportErr code{TransportErr::Ok};
  int32_t detail{0};
};
```

`ChannelMask` may be a small struct instead of a class. Its purpose is to avoid
raw shift mistakes and provide pure helpers. It must remain one byte or similarly
small; no dynamic storage is needed.

Use a narrow transport result so `BUSY`, `IN_PROGRESS`, and library configuration
errors cannot be returned from the bus callback and accidentally counted as bus
failures.

### Lifecycle and operations

A suitable shape is:

```cpp
Status bind(const Config& config);       // validates only; zero I2C
void unbind();                           // zero I2C

Status readMask(ChannelMask& observed); // one read transaction
Status writeMask(ChannelMask desired);  // one write transaction ending STOP
Status select(Channel channel);         // one write transaction
Status disableAll();                     // one write transaction
```

Do not require a successful device read before the object is considered bound.
Do not hide readback, RESET, restore, retry, delay, or recovery inside these
methods.

If the library retains cache information, use a truthful type:

```cpp
enum class MaskValidity : uint8_t {
  Unknown,
  Observed,
};

struct MaskObservation {
  ChannelMask mask{ChannelMask::none()};
  MaskValidity validity{MaskValidity::Unknown};
};
```

The owner still decides whether a successful write is enough or a later readback
is required. For TunnelMonitor startup, recovery, and ambiguous writes, readback
is required.

### Nice-to-have pure helpers

These are useful if they stay `constexpr` and side-effect free:

- `constexpr bool isValidAddress(uint8_t address)`;
- `constexpr uint8_t addressFromPins(bool a2, bool a1, bool a0)`;
- `constexpr ChannelMask channelMask(Channel channel)`;
- `constexpr bool isOneHotOrNone(ChannelMask mask)`;
- `constexpr const char* statusName(StatusCode code)`;
- `constexpr const char* channelName(Channel channel)` for diagnostics; and
- `constexpr bool isResetValue(ChannelMask mask)`.

An `enum class Address` for all `0x70` through `0x77` is acceptable, but a
validated `uint8_t` is simpler when the address comes from fixed board facts.

Do not add a generic bus topology type to the chip library. `I2cRoute` and
`I2cEndpoint` are TunnelMonitor owner types because route policy is application
topology, not TCA9548A protocol.

### API that should be removed or demoted

- `DriverState`, mandatory health counters, and offline admission;
- `recover()` and recovery backoff;
- automatic restore after RESET;
- `PollDownstreamFn` and select/downstream/restore jobs;
- `tick()` if it remains a no-op;
- synchronous bus read-modify-write helpers;
- hidden I2C inside `end()`;
- raw `readRegister(reg, ...)` compatibility for a chip with no addressed
  register map; and
- redundant aliases that make the small API look larger than the protocol.

If read-modify-write is retained for general users, expose only pure mask
composition. The caller can schedule read and write separately.

## Scan and diagnostics behavior if a mux is adopted

Keep diagnostics explicit and bounded.

### Scan scopes

- **Upstream-only:** force/verify all-off, then scan the root bus.
- **One channel:** select one channel, scan, then return to all-off.
- **All populated channels:** run the one-channel scan repeatedly; do not build
  one unbounded combined result.

The mux's upstream address remains visible during a channel scan. Classify it as
the mux, not as a downstream result. A 16-byte address bitmap can be reused for
one scope at a time; include the route/scope beside it.

Do not let scan depend on the previously selected mask. Do not enable several
channels merely to make scan faster.

### Useful bounded diagnostics

Record owner-private fields such as:

- configured mux address;
- requested route/mask;
- last observed mask and whether it is known;
- last route-change time;
- last failing phase: `MuxSelect`, `MuxVerify`, `TargetTransfer`, `MuxCleanup`,
  or `MuxReset`;
- target result and cleanup result separately; and
- reset/reconciliation count.

Expose a neutral fixed snapshot to status/CLI only if operators need it. Do not
expose the library object or allow web/CLI code to select a channel directly.

## Validation required before TunnelMonitor use

### Library native tests

1. Write sends exactly one byte to the configured 7-bit address and success
   means STOP completed.
2. Read sends no register prefix and receives exactly one byte.
3. Every address `0x70..0x77` is accepted; other addresses are rejected without
   I2C.
4. All channel values and representative masks (`0x00`, every one-hot bit,
   `0xA5`, `0xFF`) encode exactly.
5. Address NACK, data NACK, timeout, bus error, and generic failure remain
   distinct.
6. Failed or ambiguous writes do not claim the desired mask is applied.
7. RESET verification accepts only `0x00`.
8. Binding and unbinding perform no I2C.
9. Invalid rebind cannot destroy a valid binding.
10. No public operation performs more than one transport callback.
11. No heap allocation, retry loop, wait, or hidden delay is introduced.

### TunnelMonitor native tests

1. Direct endpoints bypass mux selection.
2. Routed endpoints select and verify before target transfer.
3. Select failure prevents target transfer.
4. The selected route remains stable across a multi-poll RTC, FRAM, ENV, power,
   or display job.
5. Same-route operations avoid unnecessary selection only while route state is
   known.
6. Timeout during select marks route unknown and reconciles by readback.
7. Target failure still enters bounded all-off cleanup.
8. Target failure plus cleanup failure preserves both results.
9. Deadline expiry never silently strands a selected branch.
10. Controller recovery, mux RESET, power loss, and backend reinitialization
    invalidate route state.
11. Duplicate target addresses on two channels resolve by `(route, address)`.
12. Upstream and per-channel scans are deterministic and restore all-off.
13. Mux failure is not reported as downstream target absence.
14. Required/optional health projection matches the accepted board topology.

### Hardware-in-the-loop acceptance

Use the real ESP32-S3 board, selected address straps, current 400 kHz bus, and
the actual 20 ms transfer bound.

1. Capture a logic-analyzer trace of one-byte select ending in STOP, followed by
   the downstream START.
2. Power cycle and verify the control register starts at `0x00`.
3. Exercise all eight one-hot masks, `0x00`, and representative multi-channel
   masks at library level; read each value back.
4. Put known responders on populated branches and prove selected reachability
   and unselected isolation.
5. Put the same address on two branches and prove one-hot route selection avoids
   contention.
6. Inject target NACK and verify cleanup to all-off.
7. Hold one downstream SDA low. Prove RESET deselects it, the upstream bus is
   released, and a healthy/direct endpoint remains usable.
8. Verify RESET readback is `0x00` and the faulted branch is not automatically
   restored.
9. Measure rise time and validate pull-ups/capacitance for every populated path
   at 400 kHz.
10. Run a bounded mixed-operation soak with route changes, ENV/power reads,
    display chunks, RTC/FRAM traffic, error counters, and maximum transaction
    latency.
11. Retain raw serial/logic evidence and a condensed report. Do not convert
    missing-fixture cases to a passing run.
12. Build both TunnelMonitor `native` and `tunnelmonitor_wifi` with the exact
    dependency pin and owner-private adapter.

## Validation performed during this audit

Library validation on 2026-07-18 used PlatformIO 6.1.19:

| Check | Result |
| --- | --- |
| `python scripts/generate_version.py check` | PASS |
| `python -m platformio test -e native` | PASS, 70/70 |
| `python -m platformio run -e native_core_no_arduino` | PASS |
| `python -m platformio run -e esp32s3dev` | PASS |
| `python -m platformio run -e esp32s2dev` | PASS |
| Strict host C++17 `-Wall -Wextra -Wpedantic -Werror` | PASS |
| `doxygen Doxyfile` | PASS, no warnings |
| PlatformIO package creation | PASS |
| HIL parser self-test | PASS |
| Live device HIL | NOT RUN |
| `git diff --check` | PASS |

ESP32-S3 example build usage was 22,416 bytes of RAM from 327,680 and 369,910
bytes of flash from 1,310,720. These numbers prove the example builds, not the
future TunnelMonitor integration cost.

No hardware was attached. No HIL pass is claimed.

## Product decisions still required

Before any integration work, answer these in the authoritative TunnelMonitor
guidelines:

1. Is TCA9548A populated, and on which hardware profile/revision?
2. What concrete problem requires it: duplicate addresses, capacitance, fault
   isolation, voltage domains, connector modularity, or another reason?
3. What is the exact direct/channel 0..7 endpoint map?
4. What address straps are fitted? Avoid `0x76` with the current BME address.
5. Is RESET wired? To which GPIO, with what pull-up and boot behavior?
6. Are any required boot devices, especially RTC or FRAM, downstream?
7. Is the mux required or optional in health projection?
8. Is the production policy one-hot with all-off idle? This audit recommends
   yes.
9. What pull-ups, voltages, and maximum capacitance apply to every branch?
10. What do operator probe and scan mean for direct versus routed endpoints?
11. If the goal is multiple identical sensors, what are their stable logical
    identities, cadence, result fields, and health roles?
12. Is one mux sufficient? This audit recommends supporting one first-stage mux
    only until a real board proves another need.

## Final recommendation

The TCA9548A repository is a good protocol starting point but should become
smaller before platform use. Keep the injected transport and correct one-byte
protocol. Remove policy and scheduling that belong to the I2C owner. Make
applied mask state truthful, make RESET-to-all-off explicit, and preserve exact
transport failures.

TunnelMonitor should not change its sole-owner architecture to fit this driver.
If a later board adopts the mux, add one small route layer inside `I2cTask`, use
one-hot selection and all-off idle, wire RESET for fault isolation, exact-pin the
refactored release, and validate the real topology on hardware.
