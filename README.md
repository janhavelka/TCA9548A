# TCA9548A Driver Library

Production-grade TCA9548A 8-channel I2C switch driver for ESP32-S2 / ESP32-S3
using Arduino and PlatformIO.

## Features

- Injected I2C transport with no `Wire` dependency in library code
- 8-channel control with single-channel, multi-channel, and all-off/all-on flows
- Health monitoring with `READY`, `DEGRADED`, and `OFFLINE` driver states
- Deterministic managed-synchronous lifecycle: `begin()`, `tick()`, `end()`
- Optional active-low RESET pin callback with explicit `hardReset()` support
- Runtime settings snapshot API (`getSettings()`) for diagnostics and examples
- Direct control-register aliases: `readControlRegister()` / `writeControlRegister()`
- Optional poll-chunked job API with explicit instruction accounting
- Address configurability across `0x70`-`0x77`

## Hardware

TCA9548A highlights:

- 8-channel bidirectional I2C/SMBus pass-FET switch
- Single volatile 8-bit control register, no register-address byte
- Active-low RESET input
- I2C Standard-Mode and Fast-Mode support (`0`-`400 kHz`)
- Supply range: `1.65 V`-`5.5 V`
- POR/RESET default: `0x00` (all channels off)

Typical ESP32 wiring:

```text
TCA9548A   ESP32
--------   -----
SDA    ->  GPIO8 (or custom)
SCL    ->  GPIO9 (or custom)
VCC    ->  3.3V
GND    ->  GND
RESET  ->  GPIO (optional, or pull up to VCC)
A0-A2  ->  GND / VCC (address select)
```

## Installation

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
  https://github.com/janhavelka/TCA9548A.git
```

### Manual

Copy `include/TCA9548A/` and `src/` into your project.

## Quick Start

```cpp
#include <Wire.h>
#include "TCA9548A/TCA9548A.h"

TCA9548A::TCA9548A mux;

TCA9548A::Status myI2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                            uint32_t timeoutMs, void* user);
TCA9548A::Status myI2cWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                                uint8_t* rxData, size_t rxLen,
                                uint32_t timeoutMs, void* user);
uint32_t myNowMs(void* user) {
  (void)user;
  return millis();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);
  Wire.setClock(400000);
  Wire.setTimeOut(50);

  TCA9548A::Config cfg;
  cfg.i2cWrite = myI2cWrite;
  cfg.i2cWriteRead = myI2cWriteRead;
  cfg.i2cUser = &Wire;
  cfg.nowMs = myNowMs;
  cfg.i2cAddress = 0x70;

  TCA9548A::Status st = mux.begin(cfg);
  if (!st.ok()) {
    Serial.printf("Init failed: %s\n", st.msg);
    return;
  }

  if (mux.selectChannel(0).ok()) {
    // ... talk to a downstream device on channel 0 ...
  }

  mux.setChannelMask(0x05);  // channels 0 and 2
  mux.disableAll();
}

void loop() {
  mux.tick(millis());
}
```

The repository also ships an example-only Arduino adapter in
`examples/common/I2cTransport.h`, but that helper is not part of the public
library API. If `Config::nowMs` is not provided, driver timestamps remain `0`
and `recoverBackoffMs` is not enforced.

## Versioning

The library version is defined in [library.json](library.json). `Version.h` is
auto-generated during build.

```cpp
#include "TCA9548A/Version.h"

Serial.println(TCA9548A::VERSION);
Serial.println(TCA9548A::VERSION_CODE);
Serial.println(TCA9548A::VERSION_FULL);
```

Update version metadata in `library.json` only.

## Transport Contract

Transport callbacks must map failures into library `Err` values:

- `Err::I2C_NACK_ADDR`
- `Err::I2C_NACK_DATA`
- `Err::I2C_TIMEOUT`
- `Err::I2C_BUS`
- `Err::I2C_ERROR`

If your transport cannot distinguish cases, return `Err::I2C_ERROR` and place
the best available detail code in `Status::detail`.

## TCA9548A Protocol Notes

- The chip exposes one 8-bit control register and does not use a register-address byte.
- Bit `N` enables downstream channel `N`.
- Any combination of channels may be enabled simultaneously.
- On multi-byte writes, only the last byte is latched.
- Channel changes take effect only after the I2C `STOP` condition.
- POR and RESET return the control register to `0x00`.

## Error Handling

All fallible APIs return:

```cpp
struct Status {
  Err code;
  int32_t detail;
  const char* msg;

  bool ok() const;
  bool is(Err expected) const;
  bool inProgress() const;
  explicit operator bool() const;
};
```

Important error codes:

- `OK`
- `NOT_INITIALIZED`
- `INVALID_CONFIG`
- `INVALID_PARAM`
- `DEVICE_NOT_FOUND`
- `UNSUPPORTED`
- `I2C_NACK_ADDR`
- `I2C_NACK_DATA`
- `I2C_TIMEOUT`
- `I2C_BUS`
- `I2C_ERROR`
- `BUSY`
- `IN_PROGRESS`

## Health Monitoring

The driver tracks communication health:

```cpp
if (mux.state() == TCA9548A::DriverState::OFFLINE) {
  mux.recover();
}

Serial.printf("failures=%u total=%lu\n",
              mux.consecutiveFailures(),
              static_cast<unsigned long>(mux.totalFailures()));
```

Driver states:

| State | Description |
|-------|-------------|
| `UNINIT` | `begin()` not called or `end()` called |
| `READY` | Operational, no recent failures |
| `DEGRADED` | One or more failures below the offline threshold |
| `OFFLINE` | Consecutive failures reached the offline threshold |

When the driver is `OFFLINE`, normal channel/control APIs do not touch the bus
and return `Err::BUSY`. Use explicit `recover()` or `hardReset()` to attempt
manual recovery.

## API Reference

### Lifecycle

- `Status begin(const Config& config)` - initialize driver and verify presence
- `void tick(uint32_t nowMs)` - reserved no-op for TCA9548A
- `void end()` - shut down driver and clear runtime state

### Diagnostics

- `Status probe()` - check presence without health tracking
- `Status recover()` - recover communication and restore the last known mux mask
- `Status hardReset()` - pulse RESET if configured, then verify the device responds

### Channel Control

- `Status selectChannel(uint8_t channel)` - enable one channel and disable others
- `Status setChannelMask(uint8_t mask)` - write raw mask
- `Status writeControlRegister(uint8_t mask)` - alias for `setChannelMask()`
- `Status enableChannels(uint8_t mask)` - convenience read-modify-write helper
- `Status disableChannels(uint8_t mask)` - convenience read-modify-write helper
- `Status disableAll()` - write `0x00`
- `Status readChannelMask(uint8_t& mask)` - read current mask
- `Status readControlRegister(uint8_t& mask)` - alias for `readChannelMask()`
- `Status readRegister(uint8_t reg, uint8_t& value)` - compatibility helper for the logical `CONTROL_REG`; no register-address byte is sent
- `Status writeRegister(uint8_t reg, uint8_t value)` - compatibility helper for the logical `CONTROL_REG`; no register-address byte is sent
- `Status isChannelEnabled(uint8_t channel, bool& enabled)` - convenience read helper

The minimal raw-mask API is `setChannelMask()`, `readChannelMask()`, and
`selectChannel()`. Managed adapters that already own a cached mux mask should
prefer those primitives over the compound convenience helpers.

### State And Health

- `DriverState state() const`
- `DriverState driverState() const` - alias for sibling-library uniformity
- `bool isInitialized() const`
- `bool isOnline() const`
- `const Config& getConfig() const`
- `Status getSettings(SettingsSnapshot& out) const`
- `uint32_t lastOkMs() const`
- `uint32_t lastErrorMs() const`
- `Status lastError() const`
- `uint8_t consecutiveFailures() const`
- `uint32_t totalFailures() const`
- `uint32_t totalSuccess() const`
- `uint8_t lastKnownMask() const`

### Poll-Chunked Jobs

- `Status startReadTca9548aMaskJob(uint8_t& mask)` - one-instruction mask read
- `Status startSetTca9548aMaskJob(uint8_t mask)` - one-instruction mask write
- `Status startEnableTca9548aChannelsJob(uint8_t mask)` - read/modify/write enable job
- `Status startDisableTca9548aChannelsJob(uint8_t mask)` - read/modify/write disable job
- `Status startSelectTca9548aChannelJob(...)` - select, downstream callback, restore job
- `Status startSelectTca9548aMaskJob(...)` - raw-mask select/downstream/restore job
- `Status startRecoverTca9548aJob()` - chunked recovery job
- `Status pollJob(uint32_t nowMs, uint8_t maxInstructions, PollJobResult& result)`
- `bool pollJobActive() const`
- `void cancelPollJob()`

Instruction accounting:

- One TCA9548A one-byte control write through STOP consumes one instruction.
- One TCA9548A read-only control-register read consumes one instruction.
- One hard-reset callback consumes one hardware instruction.
- `maxInstructions == 0` performs no work and returns `IN_PROGRESS`.
- Read-modify-write helpers may read and write in the same poll when budget allows.
- Select/downstream/restore jobs keep the mux select write, downstream callback,
  and restore write in separate polls even when additional budget remains.

While a poll job is active, synchronous bus-touching APIs return `Err::BUSY` so
the chunked sequence owns mux visibility until it completes or is cancelled.

## Recovery

`recover()` does not run automatically inside `tick()`. The application decides
when to retry. On each recovery attempt the driver:

1. Enforces `recoverBackoffMs`
2. Optionally pulses RESET if `recoverUseHardReset` is enabled
3. Verifies the device responds again
4. Restores the last known channel mask if the mux returned in the reset/default state

If a recovery attempt starts from `OFFLINE` and fails partway through, the driver
reasserts the `OFFLINE` latch. A configured `hardReset` callback receives
`Config::resetUser`; transport callbacks continue to receive `Config::i2cUser`.
The poll-chunked recovery job preserves the same backoff gate and splits reset,
verify/readback, and restore into separately accounted instructions.

## Notes

- The TCA9548A is a pure bus switch. It has no interrupts, ADC/DAC features, sensor readings, or persistent registers.
- Supported bus speeds are Standard-Mode and Fast-Mode only (`<= 400 kHz`).
- For voltage translation, `VCC` must be at or below the lowest translated bus voltage.
- Enabled channels accumulate downstream capacitance toward the `400 pF` I2C budget.
- TI application notes call current-source buffers such as `TCA9509` / `TCA9800` incompatible in series with this switch.
- The library intentionally does not promise general-call support, repeated-start channel activation, or dynamic A0/A1/A2 re-sampling because those behaviors are not documented clearly enough by TI.

## Behavioral Contracts

1. Threading model: single-threaded by default; not ISR-safe.
2. Timing model: `tick()` is bounded and currently a no-op; public I2C operations are blocking and bounded by the transport timeout unless the explicit poll-job API is used.
3. Resource ownership: bus, pins, reset callback context, and timebase remain application-owned via `Config`.
4. Memory behavior: no heap allocation in steady-state library operation.
5. Error handling: all fallible APIs return `Status`; no exceptions and no silent failures.

## Validation

```bash
python tools/tca9548a_hil.py --parser-self-test
python tools/tca9548a_hil.py --dry-run --port COM8 --baud 115200
pio run -e esp32s3dev
pio run -e esp32s2dev
pio test -e native
pio run -e native_core_no_arduino
```

Live HIL requires an attached ESP32 running `examples/01_basic_bringup_cli/` and
a wired TCA9548A fixture:

```bash
pio run -e esp32s3dev -t upload --upload-port COM8
python tools/tca9548a_hil.py --port COM8 --baud 115200 --verbose --report docs/reports/hil-validation-COM8-YYYYMMDD.md
```

Do not treat dry-run output as hardware validation; it only checks the host
runner plan and report generation.

## Examples

- `examples/01_basic_bringup_cli/`
  - `read` / `dump`, `read reg` / `rreg`, `write reg` / `wreg`
  - `select`, `mask`, `on`, `off`, `enable`, `disable`, `check`, `scanch`
  - `drv` / `health`, `state`, `probe`, `recover`, `reset`, `cfg`, `selftest`
  - `hil dry`, `hil parser`, `hil run`, `hil run reset`

### Example Helpers

`examples/common/` is example-only glue and is not part of the public library API.

| File | Purpose |
|------|---------|
| `BoardConfig.h` | Board-specific pin defaults and `Wire` setup |
| `BuildConfig.h` | Compile-time log-level configuration |
| `Log.h` | Serial logging helpers |
| `I2cTransport.h` | Wire-backed example transport adapter |
| `TransportAdapter.h` | Alias wrapper matching the family helper layout |
| `I2cScanner.h` | I2C bus scanner utility |
| `BusDiag.h` | Bus diagnostics wrapper |
| `CliShell.h` | Serial line reader helper |
| `CommandHandler.h` | Small bounded-buffer command helpers |
| `HealthView.h` | Compact health display helper |

## Project Structure

```text
include/TCA9548A/       - Public API headers (Doxygen documented)
  CommandTable.h        - Register constants and bit masks
  Status.h              - Error types
  Config.h              - Configuration struct
  TCA9548A.h            - Main library class
  Version.h             - Auto-generated version constants
src/
  TCA9548A.cpp          - Implementation
examples/
  01_basic_bringup_cli/ - Interactive CLI example
  common/               - Example-only helpers
docs/                   - Compact docs index and porting notes
tools/                  - Host-side helper scripts, including HIL capture
platformio.ini          - Build environments
library.json            - PlatformIO metadata
README.md               - This file
CHANGELOG.md            - Version history
AGENTS.md               - Coding guidelines
```

## Documentation

- [CHANGELOG.md](CHANGELOG.md) - release history
- [docs/README.md](docs/README.md) - compact docs index and hardware notes
- [docs/HARDWARE_NOTES.md](docs/HARDWARE_NOTES.md) - preserved chip and board-design notes
- [docs/PORTING.md](docs/PORTING.md) - framework-neutral transport and ESP-IDF porting notes

## License

MIT License. See [LICENSE](LICENSE).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## References

- [TCA9548A Datasheet (TI)](https://www.ti.com/product/TCA9548A)
