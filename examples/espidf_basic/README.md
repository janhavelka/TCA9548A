# TCA9548A native ESP-IDF CLI

This example uses native ESP-IDF APIs only: `app_main`, the new
`driver/i2c_master.h` controller API, native GPIO/timer/task calls, and fixed C
buffers. It does not include Arduino, `Wire`, `String`, `Serial`, or an Arduino
compatibility facade.

The command surface matches the Arduino bring-up CLI, including address and
health diagnostics, mask read/write/select, safe-off recovery, optional RESET,
bounded scan/stress, and HIL commands. The default RESET GPIO is disabled;
change `RESET_GPIO` in `main/main.cpp` only for the actual fixture.

From an ESP-IDF 5.4 or 5.5 shell:

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Use `esp32s2` instead when required. A successful build, CLI dry run, or parser
contract is not hardware validation. Live evidence requires an attached
TCA9548A, reviewed pull-ups/voltages, and the HIL runner described in the root
README. The example leaves all channels disabled after its startup check and
after every HIL/stress path.
