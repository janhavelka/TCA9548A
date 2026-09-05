# TCA9548A native ESP-IDF CLI

This example uses native ESP-IDF APIs only: `app_main`, the
`driver/i2c_master.h` controller API, native GPIO/timer/task calls, and fixed
C buffers. It does not include Arduino, `Wire`, `String`, `Serial`, or any
Arduino compatibility layer.

The command surface matches the Arduino bring-up CLI: address and health
diagnostics, mask read/write/select, safe-off recovery, optional RESET,
bounded scan/stress, self-test, and the HIL commands. Console input is polled
through the shared fixed-size line accumulator: a command runs only after CR
or LF, and an overlong line is discarded through its terminator. The default
RESET GPIO is disabled; change `RESET_GPIO` in `main/main.cpp` only for the
actual fixture.

The bus runs at 400 kHz with the ESP32 internal pull-ups disabled, because at
roughly 45 kOhm they cannot drive it. Every active segment needs external
pull-ups sized as described in the hardware notes.

From an ESP-IDF 5.4 or 5.5 shell:

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Use `esp32s2` instead when required. A successful build, CLI dry run, or parser
contract is not hardware validation; live evidence requires an attached
TCA9548A, reviewed pull-ups/voltages, and the HIL runner described in the root
README. The example leaves all channels disabled after its startup check and
after every stress path; `selftest` restores the mask it found on entry, and
`scan` reports the active mask before probing the 126 normal 7-bit addresses.
