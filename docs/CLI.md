# Bring-up CLI and long runs

Both maintained firmware examples compile `examples/common/Tca9548aCli.cpp`.
This is example glue, separate from the portable driver. Type `help` for the
grouped command list and aliases. Use `help common`, `help connection`,
`help configuration`, `help switch`, `help diagnostics`, or `help quickstart`
for a smaller task-specific view; `help all` prints everything.
Commands accept CR or LF; maximum length is
127 bytes. Bad control bytes and overlong lines discard the entire line.
Numbers accept decimal, `0x` hexadecimal, and leading-zero octal; signs,
overflow, trailing text, extra arguments, and out-of-range values are rejected.

## Connection and detection

Use the board's native USB connector at 115200 baud. PlatformIO and the HIL
runner default to **DTR on, RTS off**. ESP32-S2 Arduino uses TinyUSB CDC;
ESP32-S3 uses hardware USB Serial/JTAG. The native IDF example supplies matching
target-specific `sdkconfig.defaults` files with secondary console output off.
An existing `sdkconfig` takes precedence: verify the console choice in
`idf.py menuconfig` when upgrading an existing build directory.

Startup does not wait for a terminal. It probes the configured address, binds
the driver, then writes and verifies all-off. Opening a terminal later may
miss the banner; send `version` and `help` again. Before `begin`, an address
NACK leaves the driver unbound and does not attempt a control-byte receive.

```text
color off
version
bus
detect
end
addr 0x72
timeout 20
resettimeout 10
offline 5
begin
cfg
health
```

Replace `0x72` with the actual strap address. `detect`/`discover` performs eight
address-only probes, `0x70` through `0x77`, without selecting channels. ACKs are
candidates: the chip has no identity register. These are addresses visible
through the current routing; external wiring and known fixture contents must
establish identity. Detection never automatically binds or changes routing.
Settings change only while ended and idle. `end` itself performs no I2C and
does not disable channels. Use `off` then `verify 0` before releasing a route.

`scan` reads and prints the active mask, then probes the 112 non-reserved
addresses. Use `select N` before `scan` to isolate one branch. Neither scan nor
discovery treats a timeout or bus error as proof of absence. A transport error
stops the scan; a NACK simply advances to the next address.
`stats` retains the candidate list and tested/ACK/absent/error counts, including
whether the configured address was acknowledged, absent, failed, or never
tested. It records the address at session start, even after a settings change.

## Direct access and transfer assertions

| Operation | Commands | Effect |
| --- | --- | --- |
| Read/control | `read`, `mask 0xA5`, `select 3`, `off` | Read one byte or write a complete channel mask |
| Change one bit | `enable 2`, `disable 2` | Fresh read, modify one bit, then write; never write after a failed read |
| Verify | `verify 0xA5`, `verify` | Compare live byte with an explicit value or the known cached value |
| Recovery | `recover`, `reset` | One all-off write, or configured hardware RESET plus exact-zero verification |
| Cached diagnostics | `cfg`, `health`, `bus`, `console`, `diag` | Settings, health including full last error, board identity, console backlog and loss counters |
| Cache invalidation | `invalidate` | Mark route knowledge unknown without bus access |
| Raw probe | `probe` | Read control byte without changing tracked health |

Physical counters include reads, writes and address probes, even NACKs during
a scan. Rejected calls that never reach the controller do not count. They are
separate from the driver's lifetime health counters. For example, on a bound,
healthy fixture:

```text
xfer_reset
health
cfg
invalidate
xfer_assert 0 0 0
select 3
verify 8
probe
xfer_assert 3 3 0
xfer_stats
off
verify 0
```

Counters saturate rather than wrap. Exact assertions reject saturated counts;
reset the physical counters while idle before a new measurement. `xfer_reset`
does not clear driver health. `version` reports the build's full Git commit,
clean/dirty state, framework, MCU, and console dropped-output count. Archives
without their own Git checkout report unknown source identity.

## Diagnostic sessions

| Command | Work bound | Final routing |
| --- | --- | --- |
| `hil parser` / `hil dry` | Parser/type checks; no I2C | Unchanged |
| `selftest` / `hil run` | All eight one-hot masks, `0xA5`, raw probe, recovery | Capture and verify restoration of entry mask |
| `hil run reset` | Above plus RESET from verified `0x01` seed | Restore entry mask |
| `sweep` | 256 mask writes/readbacks, entry read and restoration | Restore entry mask |
| `stress N` | `N` one-hot writes/readbacks, then two cleanup calls | Verify all-off |
| `stress_mix N` | `N` rounds of seven calls: one-hot, arbitrary mask, raw probe, recovery and readbacks; two cleanup calls | Verify all-off |
| `watch N interval_ms` | `N` reads spaced by interval, first immediately | Unchanged |
| `soak seconds` | Capture the current byte, verify it once per second and at the deadline | Unchanged; drift fails the session |

Stress defaults to 100 rounds and caps at 1,000. Watch defaults to 20 reads at
1,000 ms, capped at 1,000 reads and 60,000 ms per interval. Soak accepts 1 to
86,400 seconds. Slow execution skips catch-up bursts. Stress success counts
verified rounds, not merely attempted calls. Mixed stress visits every channel
when `N >= 8`. The session transfer counts remain useful after lifetime health
counters saturate.

Scan, discovery, sweep, stress, watch, and soak schedule at most one I2C
primitive per loop. `stats`, `health`, `version`, `help`, and `stop` remain
available; competing bus mutations return BUSY. `stop`/`cancel` schedules only
the required cleanup. The prompt returns when completion/cleanup finishes.
`stats`/`result` retains the primary failure and cleanup outcome separately;
`stats reset` clears this evidence only while idle. Self-test and HIL mask
checks are finite synchronous procedures; they do not accept cancellation
mid-command.

At the default 20 ms transfer timeout, 1,000 mixed rounds have 7,002 callback
budgets totaling 140.04 s, plus scheduler and console overhead. The native
example defaults to a 1 kHz scheduler and yields at least one tick per loop. Host
stress commands therefore default to a separate 240 s response deadline;
increase it within the runner's limits or reduce counts for larger timeouts.
Neither configured timeout is a measured controller wall-clock guarantee.

## Fixture and USB endurance procedure

Only run mask tests where their channel combinations are electrically safe.
Use `stress` for one-hot-only fixtures; arbitrary-mask HIL/mixed stress and
`sweep` can connect conflicting downstream addresses. Configure and wire RESET
before claiming RESET coverage. Follow the [live HIL procedure](VALIDATION_STATUS.md#live-hil-procedure)
for recorded route-isolation, exhaustive mask, stress, and host soak checks.

1. Record `version`, `cfg`, `health`, `console`, and `xfer_stats` on each supported board
   and framework. Start with `hil parser`, then `hil run reset` on a suitable
   fixture. Confirm every required check passes and entry routing is restored.
2. Run `stress 1000`, `stress_mix 1000`, and `sweep` where safe. Inspect `stats`
   after each. Cancel a repeat midway; require CANCELLED and verified cleanup.
3. Run `soak 3600`, using `stats` periodically. This firmware command only
   reads and verifies the selected route against its initial byte, including
   a final read at the deadline. The host runner's `--soak-duration-s` instead
   repeats quiet `stress_mix 8` batches covering every channel, followed by
   all-off readback, health and console checks. It requires at least one
   complete batch and verified all-off cleanup.
4. With the MCU independently powered, disconnect USB during a session, then
   reconnect. Send raw byte `0x15` (Ctrl-U), then CR or LF to discard a partial
   command before inspecting `version`, `stats`, `health`, and `console`.
   A newline alone can execute buffered input. Require continued execution,
   valid retained results, and no watchdog reset. Also test a host that stops
   reading output. A fixed 8 KiB output queue drains at most 64 bytes per loop
   through zero-wait backend calls. Overflow records a visible
   `CONSOLE_OUTPUT_LOST` error and counts lost bytes/messages; it must fail a
   recorded run. Accepted SDK bytes do not prove delivery to the terminal.
5. Repeat a quiet (`verbose off`) long run with transcript capture. Re-run
   `console` afterwards to inspect output-loss evidence. Keep the host awake,
   prevent USB selective suspend for the fixture port during this test, and
   close other monitors. Do not change machine-wide power settings blindly.

Send one command at a time and wait for its prompt; input queues are finite.
On an observable disconnect, the adapters discard partial commands and queued
input through the next terminator. Arduino S2 also handles RX overflow events.
`console` reports whether RX overflow reporting is supported: native S2 and
both S3 adapters cannot observe backend RX losses, so a zero counter there
does not prove loss-free reception. Native S2 also reports connection state
as unknown. The host runner fails on loss-counter changes, saturated loss
counters, malformed responses, or reset/startup banners. A reset retires its
serial session; it does not restore a mask captured before that reset.

Arduino S2 disables CDC-triggered reboot for long runs; subsequent uploads may
require holding BOOT while pressing RESET to enter download mode. Native S2
ROM CDC retains SDK control-line behavior, so keep DTR on and RTS off. Host
drivers can still toggle lines while opening a port; verify this on the fixture.
Arduino S3 uses the SDK driver directly with CDC-on-boot disabled to keep one
USB owner. Both S3 adapters allocate fixed 1 KiB TX/RX driver queues at startup.

The native S2 adapter checks endpoint readiness before calling the pinned SDK's
ROM CDC backend and preserves partial writes; the VFS otherwise reports requested
bytes even when it drops output. The native example requires a supported USB
console; a custom UART backend must supply its own bounded, nonblocking adapter.
These backend choices must be rechecked on an SDK upgrade:
[CDC VFS](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_vfs_console/vfs_cdcacm.c),
[ROM CDC backend](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_system/port/usb_console.c),
[USB Serial/JTAG API](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_usb_serial_jtag/include/driver/usb_serial_jtag.h).

Both I2C owners retire controller access after a receive failure on the pinned
SDK. `cfg` reports `Controller restart required: yes`. Fix the fixture and
restart the MCU; `end`, address changes, and mux RESET do not clear the latch.
Cleanup can fail under that condition; preserve its error and use external
RESET/power handling as appropriate. This example policy does not change the
portable driver's passive health model.

No real hardware endurance result is recorded yet. Compilation and host tests
establish software behavior; timing, USB disconnect/reconnect, electrical
routing and fault-injection results require the actual fixture.
