# TCA9548A native ESP-IDF CLI

This example uses native ESP-IDF APIs only: `app_main`, the
`driver/i2c_master.h` controller API, native GPIO/timer/task calls, and fixed
C buffers. It does not include Arduino, `Wire`, `String`, `Serial`, or any
Arduino compatibility layer.

The command surface matches the Arduino bring-up CLI: address and health
diagnostics, mask read/write/select, safe-off recovery, optional RESET,
cooperative scan/stress/sweep/watch/soak, self-test, physical counters, and the
HIL commands. Both frameworks compile the same example-only command engine;
see the [CLI guide](../../docs/CLI.md). Console input is polled
through the shared fixed-size line accumulator: a command runs only after CR
or LF, and an overlong or control-byte-contaminated line is discarded through
its terminator.

Review `I2C_SDA`, `I2C_SCL`, `I2C_ADDRESS`, and `RESET_GPIO` in `main/main.cpp`
for the fixture. Defaults are GPIO8/GPIO9, address `0x70`, and RESET disabled.
Set `I2C_ADDRESS` to match the straps, or use `end`, `addr`, then `begin` at
runtime. Device registration happens during explicit owner retargeting before
timed callbacks. An address-only probe precedes the first control-byte read. Each control-byte transfer uses the library's default
20 ms budget; the optional RESET callback has a separate 10 ms budget.

The bus runs at 400 kHz with the ESP32 internal pull-ups disabled. Each bus
segment needs external pull-ups sized for its voltage and capacitance; simultaneously
enabled branches combine their loading. See the
[hardware notes](../../docs/HARDWARE_NOTES.md#bus-electrical-rules).

From an ESP-IDF 5.4 or 5.5 shell, in `examples/espidf_basic/`:

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Use `esp32s2` instead when required. A successful build, CLI dry run, or parser
contract is not hardware validation; live evidence requires an attached
TCA9548A, reviewed pull-ups/voltages, and the
[live fixture procedure](../../docs/VALIDATION_STATUS.md#live-hil-procedure).
The default HIL run requires the optional RESET connection to be configured.
Startup and stress require verified all-off for success; `selftest`
attempts verified restoration of its entry mask. Failures are reported and
may require external RESET/power handling when controller access is retired.
`scan` reports the active mask before probing the 112 non-reserved 7-bit addresses.

The pinned native probe runs at 100 kHz; control-byte transfers use 400 kHz.
Scan rejects a failed topology read and counts bus faults separately from
ordinary address NACKs. A receive failure latches controller access off until
MCU restart (`cfg` reports it), guarding the pinned SDK's stale receive-state
path. Neither mux RESET nor driver rebind recreates the controller. See
[the adapter limits](../../docs/PORTING.md#backend-fault-and-timing-limits).

The supplied defaults select native USB CDC on ESP32-S2 and USB Serial/JTAG
on ESP32-S3, with secondary output disabled. Existing `sdkconfig` values take
precedence; verify the console choice in `idf.py menuconfig` after upgrading.
Use the native USB connector with DTR on and RTS off. Startup does not wait for
a terminal. Output attempts have a 50 ms budget per piece, input is nonblocking,
and every loop yields at least one FreeRTOS tick. `version` reports dropped
output and Git commit/tree identity, refreshed even on incremental builds.

S2 uses the SDK's internal ROM CDC buffer API to preserve partial-write results;
its generic VFS can silently discard them. S3 uses the native driver with fixed
queues allocated once during startup and zero-wait TX calls. These example-only
backend choices are checked by the pinned 5.4/5.5 build matrix and require review
on SDK upgrades. Firmware build success does not validate USB endurance; follow
the [disconnect and long-run procedure](../../docs/CLI.md#fixture-and-usb-endurance-procedure).
