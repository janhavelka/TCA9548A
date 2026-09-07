# TCA9548A code audit

Audited 2026-09-03, findings re-verified claim by claim 2026-09-04. Sources: TI
datasheet SCPS207H (Rev. H), TI SCPA063 "How to Debug I2C", NXP UM10204 Rev. 7,
and the ESP-IDF 5.5 and Arduino-ESP32 3.x sources.

This is a working document, not product documentation. Every row in section 1
is implemented; the user-visible ones are in the changelog, and the rest are
internal cleanups recorded only here. Delete section 1 once you have
spot-checked it. Section 2 holds decisions only you can make.

**Result: the driver core has no functional bug. Every defect found was in the
examples or the tooling.** In the library, every control byte, transfer length,
buffer pointer, mask operation, status mapping, counter, and integer conversion
is correct, and every protocol constant matches the datasheet. What the audit
found there was duplication, provably dead code, and header comments that were
untrue in specific states.

The most serious of them: the native example's main loop delayed with
`pdMS_TO_TICKS(1)`, which is **zero ticks** at ESP-IDF's default 100 Hz tick
rate. `vTaskDelay(0)` does not block, it only forces a reschedule, and that
never selects the priority-0 idle task from a priority-1 caller. With a console
`getchar()` that is non-blocking by default, the CLI spun from first boot and
the CPU0 idle watchdog fired every five seconds. The constant came from
Arduino-ESP32, which ships a 1000 Hz tick where the same expression is one tick.

## 1. Implemented — verify, then delete this section

Verified by the full local suite: 38 native tests, the strict
`-Wconversion -Wsign-conversion -Werror` framework-neutral core build, the
Arduino example built for ESP32-S3 and ESP32-S2, the three repository checkers
plus the generated-metadata check, the HIL parser self-test, Doxygen with
warnings as errors, and `git diff --check`. The native ESP-IDF example build is
covered by CI only; no local ESP-IDF installation was available.

### 1.1 Core

| # | Change | How to check it |
| --- | --- | --- |
| 1 | Seven transport and control-byte helpers (`_i2cWriteRaw`, `_i2cWriteReadRaw`, `_i2cWriteTracked`, `_i2cWriteReadTracked`, `_readControlByteRaw`, and the old `_writeControlByte`/`_readControlByte` pair) collapsed into `_writeControlByte(mask)` and `_readControlByte(mask, tracked)`; `_recordMask()` inlined at all three of its call sites, which the read merge collapses to two assignments | `src/TCA9548A.cpp` went 340 to 272 lines (47 added, 115 removed). The deleted argument checks were unreachable: `begin()` validates the callbacks before setting `_bound`, and the only buffers ever passed are a one-byte local and a null transmit pointer. The dropped early-out that skipped `_updateHealth()` on `INVALID_CONFIG`/`INVALID_PARAM` is inert because `mapTransportStatus()` can only produce `OK` or an `I2C_*` code. |
| 2 | `recover()` is `return disableAll();` | The two were behaviourally identical: both checked the binding and made one tracked `0x00` write, `recover()` inline and `disableAll()` through `writeChannelMask()`. |
| 3 | `selectChannel()` states the channel bound once | `ChannelMask::one()` returns `none()` for any cast at or above 8, and `isOneHot()` rejects zero, so casts of 8 and 255 still yield `INVALID_PARAM` with no I2C. |
| 4 | `begin()` and `readChannelMask()` return the read result directly | `begin()` used to re-wrap a successful read as a fresh `Status::Ok()`; `readChannelMask()` copied through a redundant local. `begin()` still needs a discarded `ChannelMask` local for the out-parameter. |
| 5 | `_updateHealth()` returns `void` | It returned its argument unchanged on every path, so the two `return _updateHealth(status);` call sites gained nothing from the value. |
| 6 | The strict no-Arduino gate calls `probe()` | `probe()` is defined out of line, so the gate now proves the compiled core links, not just that it compiles. |
| 6a | Both example RESET callbacks return `RESET_ERROR`, not `UNSUPPORTED`, when no pin is wired | `Config.h` restricts the callback to `OK`, `TIMEOUT` and `RESET_ERROR`, so `hardReset()` would have converted `UNSUPPORTED` into `INVALID_CONFIG`. This is what makes section 3's "both RESET callbacks stay inside the documented domain" true. |

### 1.2 Behaviour that was undocumented or wrongly documented

Each row was confirmed against the running code, not just against the comment,
and each now has a test.

| # | Statement now in the header, README or AGENTS.md | What it replaced |
| --- | --- | --- |
| 7 | `UNINIT` means no successful *tracked* transaction; counters may already be nonzero | The header said "no successful device transaction", but a successful `probe()` is one and leaves `UNINIT`, and failures after a failed `begin()` increment counters while the state stays `UNINIT`. |
| 8 | `DEGRADED` and `OFFLINE` require an initialised binding | The header gave only the counter thresholds, which are not sufficient. |
| 9 | Any failed read, `probe()` included, invalidates the cached observation | Only a test recorded this; the README mentioned writes only. |
| 10 | The `hardReset()` verification read is tracked, and `RESET_STATE_MISMATCH` leaves `state()` at `READY` without updating `lastError()` | Undocumented and untested. An owner polling `lastError()` for RESET anomalies would have missed every mismatch. |
| 11 | An `offlineThreshold` of 1 goes `READY` to `OFFLINE` with no `DEGRADED` step | The AGENTS state table said every failure in `READY` goes to `DEGRADED`. |
| 12 | `resetTimeoutMs` is range-checked even when `hardReset` is null | Undocumented. See open item 5. |

### 1.3 Examples and adapters

| # | Change | How to check it |
| --- | --- | --- |
| 13 | Native CLI main loop delays `pdMS_TO_TICKS(10)`; the stress loop yields one tick every 64 iterations | See the headline. Delaying every iteration would add at least ten seconds per thousand operations at 100 Hz and overrun the HIL runner's per-command timeout; each transaction already blocks in the I2C driver, so a periodic yield suffices. Starvation was per-core, so a dual-core target kept IDLE1 alive. |
| 14 | The native I2C adapter refuses a combined write-read instead of calling `i2c_master_transmit_receive` | That call puts a repeated START on the mux, which returns the new control byte while the switches have not moved. The driver never asks for that shape, so this was a trap for the next person reusing the adapter, and it contradicted the Arduino adapter. |
| 15 | Native example sets `enable_internal_pullup = false` | The ESP32 internal pull-ups are about 45 kOhm. Espressif documents them as too weak for high-speed buses, and this example configures 400 kHz, so it contradicted the repository's own hardware notes. |
| 16 | Both examples deassert RESET before the pin becomes an output | The output latch reads zero after chip reset, so enabling the driver first asserts active-low RESET. The native example presets the level before `gpio_config()`, since `GPIO_OUT_REG` is writable beforehand and a pull-up cannot win against an enabled output driver. The Arduino example registers the pin as an input pull-up first, because Arduino-ESP32 3.x ignores `digitalWrite()` on a pin `pinMode()` has not claimed and merely logs an error. Both blocks are unreachable in the shipped default, where the pin constants are -1, so the pulse was latent. |
| 17 | Native stress prints the same `[FAIL] final safe-off was not verified` line as the Arduino CLI, and the HIL runner treats `safe_off=FAILED` and a nonzero `failure=` count as failures | Without both, an unverified all-off after stress, the safety property the example exists to demonstrate, reported as a pass. The runner matched only `failures=` while the CLIs print `failure=`. |
| 18 | `run_soak()` in `tools/tca9548a_hil.py` judges `health` output by the common failure patterns only | `health` prints a sticky last-error field, so one historical fault marked every later soak iteration failed. |
| 19 | Lines the HIL runner matches no longer depend on `LOG_LEVEL` | The Arduino scan banner went through `LOGI`, compiled out below level 2, so a supported build made a healthy fixture report UNKNOWN. The unknown-command error went through `LOGE`, compiled out at level 0. Both are now printed directly. |
| 20 | `mapWireResult()` documents the NACK-phase approximation and drops a duplicate case | Arduino-ESP32 3.x collapses every NACK into code 2 and never returns 3, so `NACK_ADDR` is an approximation the porting guide requires to be explicit. Case 4 duplicated the default. |
| 21 | The ESP-IDF contract checker uses word-anchored forbidden patterns and anchors its required tokens as calls | `i2c_master_receive` was satisfied as a substring of `i2c_master_transmit_receive`, so the check could not fail as intended. The bare substring `Serial` also made any comment mentioning USB-Serial-JTAG fail CI. |
| 22 | The HIL parser self-test looks steps up by test id | It indexed `plan[7]`, so combining `--parser-self-test` with `--skip-reset` failed with a misleading message. |
| 23 | `pulseReset` is `[[maybe_unused]]`, the native CLI prints an `[E]` tag on an unknown command, and both CLIs use the same `strtoul` range-error policy | `if constexpr` discards `pulseReset`'s only caller in the shipped default, leaving an unused-function warning. The rest was gratuitous divergence between two CLIs a checker exists to keep identical. |

### 1.4 Datasheet corrections in the hardware notes

Every constant in `include/TCA9548A/CommandTable.h` was compared against
SCPS207H and matches. The prose did not.

| # | Correction |
| --- | --- |
| 24 | A repeated START does not switch channels. The notes stated the STOP requirement but also listed channel activation after a repeated START among the behaviours "not promised", as though it were merely undefined. SCPA063 section 4.1 documents what actually happens: the read returns the new byte while the switches have not moved. That is now cited as a rule in both the hardware notes and the porting guide, and removed from the "not promised" list. |
| 25 | The mux answers at its own address on every enabled channel, so a downstream device may not use it. The known occupants of `0x70` to `0x77` are listed, including the PCA9685 All-Call address, which is enabled at power-up. |
| 26 | RESET frees the upstream bus but does not clear a stuck downstream target. UM10204 section 3.1.16 gives the nine-pulse remedy. Routing those pulses through the re-enabled channel is this library's inference from the pass-gate topology, and is now labelled as such rather than sitting inside the UM10204 citation. |
| 27 | Read exactly one byte; the content of a second is undocumented. |
| 28 | The POR falling threshold was stated backwards. SCPS207H section 5.5 gives `VPORF` as 0.8 V minimum and 1.0 V typical. |
| 29 | `VCC` is limited to 3.6 V above 85 C (SCPS207H section 5.3, Recommended Operating Conditions). |
| 30 | The 40 us re-ramp requirement triggers at `VPORF(min)` minus 50 mV or at GND, not at `VPORF`. The same table bounds supply fall and rise times. |
| 31 | Off-capacitance figures are typical and maximum columns, not a range: 20/28 pF upstream and 5.5/7.5 pF per disabled downstream pin (SCPS207H section 5.5, `Cio(off)`). |
| 32 | 400 pF is the datasheet limit in both Standard-mode and Fast-mode (`Cb`, section 5.6), and sink current sums across simultaneously enabled channels (section 8.2.1). |
| 33 | Two claims in `docs/HARDWARE_NOTES.md`, that the switch never stretches SCL and that the device is inert while RESET is held low, rested on TI E2E posts by a community member, not on vendor documentation. Both are now stated as expectations with their real basis: the datasheet specifies no SCL output driver for the switch, and UM10204 section 3.1.9 notes most targets cannot stretch. No other repository document repeated either claim. |
| 34 | "Behaviors not promised" mixed two different things. General-call handling and strap re-sampling stay, being genuinely undocumented, and address acknowledgement while RESET is low or `VCC` is below `VPORR` was added to them. Repeated-START activation moved out to the control-register rules (item 24) and clock stretching to the bus rules (item 33). Interrupts, fault registers, ADC, DAC, EEPROM and secondary registers moved to a new sentence saying the datasheet documents them absent rather than leaving them undefined. |

### 1.5 Documentation and tooling

| # | Change |
| --- | --- |
| 35 | Deleted `docs/NAMING_HYGIENE.md`, a completed audit report describing a finished process rather than the product, and removed every reference to it. |
| 36 | Rewrote `docs/VALIDATION_STATUS.md` and `docs/FEATURE_MATRIX.md` as what they claim to be. Both had become narratives of successive audit passes. |
| 37 | The validation command list lives only in `CONTRIBUTING.md`. It had been copied into the README, the porting guide and the validation status, and the copies had diverged. |
| 38 | `SECURITY.md` no longer names a specific staged version, which needed an edit on every bump and was enforced by a checker. |
| 39 | Rewrote the changelog's unreleased section as user-facing changes relative to v1.0.0. |
| 40 | `AGENTS.md`: corrected the repository map, the non-existent `readChannels` in the architecture diagram, the state-transition table, the version-generator outputs, and the rule that all I/O over 1-2 ms must be split across `tick()`, which contradicts the managed synchronous model. |
| 41 | `CMakeLists.txt` no longer shells out to Python at configure time, so an ESP-IDF consumer's build no longer needs a Python interpreter to check a file CI already checks. |
| 42 | `library.json` exports `idf_component.yml`, so an installed copy carries the whole ESP-IDF component rather than half of it, and no longer exports the repository checkers. |
| 43 | Rewrote `tools/check_repository_hygiene.py` from 313 lines to 199, holding six checks: `check_tracked_artifacts`, `check_encoding`, `check_local_links`, `check_core_is_framework_neutral`, `check_examples_use_core_names` and `check_ci_runs_this_guard`. Only the framework-neutral-core check is new; the enum-name-helper and CI-guard checks carried over from the old file. What went, without replacement, were brittle rules asserting exact CI pin counts, specific sentences in `SECURITY.md`, the presence of the naming report, and dozens of string tokens in the public header, which the strict build and the native tests already cover. |
| 44 | The `idf_component.yml.orig` rule was generalised, not dropped: that file is regenerated by the pioarduino component manager when it cannot locate the Arduino framework directory, so the hygiene guard now rejects any `.orig` or `.rej` file anywhere. Removed outright from the two contract checkers was the `CommandHandler.h` rule, which guarded a file deleted long ago. Two further losses were unintentional and have been restored: an anchored `\bArduinoCompat\b` pattern no longer matched `IdfArduinoCompat`, and three of the four required ESP-IDF README caveat tokens had been dropped. |

## 2. Open — your decision

### 2.1 Public API deletions

Each of these is dead inside this repository, but the header is a published
interface and I cannot see your other firmware. All are source-compatible
removals for a major version.

**1. `Status::inProgress()`** has zero callers anywhere, including tests. It
tests for `Err::IN_PROGRESS`, which this driver never produces.

**2. `toString(Err)`, `toString(DriverState)`, `toString(MaskProvenance)`** are
one-line forwarders to `errorName()`, `driverStateName()` and
`maskProvenanceName()`. The two CLIs call the named functions 32 times between
them and never call `toString`; only six test assertions and the documentation
do. Two spellings of one operation is the duplication the engineering rules say
to delete, but they may be what sibling drivers in the family expose.

**3. `driverState()`** duplicates `state()`. Each CLI calls it exactly once, to
assert it equals `state()` and print "State alias parity", so it exists in order
to be tested. `tools/check_cli_contract.py` also requires the call.

**4. Unused constants in `CommandTable.h`**: `I2C_ADDR_0x70` through
`I2C_ADDR_0x77`, `ADDRESS_PIN_MASK`, `CH0` through `CH7`, `FIRST_CHANNEL` and
`LAST_CHANNEL` have no caller anywhere. With their comments and separators they
are about 57 lines. The address constants duplicate `addressFromPins()`, and
`cmd::CHn` duplicates `ChannelMask::one(Channel::CHn)` with a weaker type. A
further ten constants are referenced only by the test that asserts their values.

`CONTROL_REG` is a separate case. The part has no register number at all, so
publishing a constant with that name is exactly the value a porter would wrongly
prepend as a register pointer, which the device would then latch as the channel
mask. Its comment now says never to send it on the bus; deleting it is safer.

Suggested order: delete item 1 now, and items 2 to 4 at the next major version,
unless a sibling driver needs the shared spellings.

### 2.2 Other decisions

**5. `resetTimeoutMs` is validated even with no RESET callback.** A config with
`hardReset == nullptr` and `resetTimeoutMs == 0` is rejected with a message
about a timeout that would never be used. I kept the uniform rule and documented
it: one unconditional validation block is simpler than a conditional one, and
the default of 10 makes the trap rare. Guarding the check with
`config.hardReset != nullptr` is friendlier and would need two test assertions
changed. Either is defensible.

**6. The shared CLI line-buffer test lives in the core test binary.**
`test/test_driver.cpp` includes `../examples/common/CliLineBuffer.h`, so the
library's own test binary depends on example code the engineering rules say is
not part of the library. It deserves its own test directory.
`tools/check_cli_contract.py` pins the test's name and file, so both move
together. Low value, no risk either way.

**7. A default HIL run cannot pass on an unmodified checkout.** RESET validation
is on by default, both examples ship with the pin disabled, and a missing
callback is reported as a failure. I documented the real requirement rather than
weakening the gate: the fixture must have RESET wired and the example constant
set. Reporting an unwired board as a skip would make the common case pass but
let a release run silently produce no RESET evidence. I would keep the
strictness.

**8. The native adapter can never report `NACK_ADDR`.** It maps only
`ESP_ERR_TIMEOUT` to a timeout and everything else to `OTHER`, so with no device
on the bus the Arduino CLI prints `I2C_NACK_ADDR` and the native one prints
`I2C_ERROR`. That is permitted: the porting guide says to return the narrowest
truthful result the backend exposes, and the documented ESP-IDF return set
carries no NACK phase information. Narrowing it means matching specific error
codes on the receive path, where an address NACK is the only possibility, and
those codes have changed across 5.x releases. Worth doing only against a pinned
IDF version you have tested.

**9. Remaining example smells, none of them faults.** Listed separately so each
can be accepted or fixed on its own. My recommendation follows each.

- **9a.** The native scan makes 126 probes with no explicit yield. Each probe
  blocks in the I2C driver, so the idle task still runs. Leave it.
- **9b.** `ensureDevice()` sets `bus.device = nullptr` before checking whether
  `i2c_master_bus_rm_device` succeeded, so a failed removal leaks the handle.
  Only reachable if the CLI changes address, which it cannot today. Worth
  fixing when an address command is added.
- **9c.** `i2c_master_bus_reset` exists but nothing calls it. Bus recovery is
  the owner's job by design, so this is a deliberate omission; leave it.
- **9d.** `CliStyle.h` prints through `Serial` while `CliShell.h` reads through
  `LOG_SERIAL`. Harmless while they are the same object, confusing if a project
  redefines `LOG_SERIAL`. One-line fix, low priority.
- **9e.** `tools/check_cli_contract.py` finds function bodies with a brace
  counter that does not understand strings or comments. It works on the current
  sources and fails loudly if it stops working. Leave it until it breaks.

**10. Leftover scratch directories — done, no decision needed.** `.pio/` held
`idf_source_gate`, `manual_nonpio`, `package_consumer_second` and
`postbuild_core.exe` from earlier audit passes, about 50 MB that PlatformIO does
not create and git ignores. Those and the four stale package tarballs beside
them are deleted; only the live `build` and `libdeps` remain.

## 3. Verified correct

Recorded so these areas are not re-litigated.

**Protocol.** The address range and `1110 A2 A1 A0` encoding; the single control
byte with no register-pointer phase; the one-byte write; the read as a null
transmit pointer with a one-byte receive; `0x00` as the POR and RESET default;
the exact-zero RESET verification; RESET timing of 6 ns minimum low, zero
recovery and 500 ns SDA release; 0 to 400 kHz; 400 pF; the supply and POR
thresholds. Every constant matches SCPS207H.

**Code.** Exact transport-to-status mapping with `detail` preserved and a
default branch for invalid casts; saturating counters that keep `OFFLINE`
reachable even at threshold 255; the mask cache transitions;
`ChannelMask::one()` guarding against an undefined shift on an out-of-range
cast; `isOneHot()` guarding its decrement; fixed underlying enum types, so the
tests' invalid casts are well defined; self-contained headers with no ODR
problem; transactional `begin()` where validation precedes any mutation and
`BUSY` precedes validation, so a bound driver is never disturbed; bus-silent
`end()` with lifetime totals surviving. `_lastError` only ever holds a
core-owned static string; `hardReset()` forwards a callback's `Status` to the
caller but never stores it.

The three enum-name helpers list every enumerator and then fall out of the
switch to a trailing `return "UNKNOWN";`, with no `default:` label. That is
deliberate: a `default:` would suppress `-Wswitch`, so a newly added enumerator
would compile silently instead of warning.

**Tests.** None asserted wrong behaviour. The scripted transport models the
datasheet's last-byte-wins write, ambiguous failed writes, and a controller that
filled the receive buffer before reporting failure, which the driver correctly
ignores.

**Adapters.** STOP completion on every write, one-byte writes, standalone
read-only reads with no register phase, no retries anywhere, no invented error
causes, bounded command parsing, and the 1000-operation stress cap. Both RESET
callbacks stay inside the `OK`, `TIMEOUT`, `RESET_ERROR` domain. The two CLIs
have identical command sets: 25 dispatch entries, four argument bounds, and
byte-identical help text.
