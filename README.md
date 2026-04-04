# TCA9548A Driver Library

Production-grade TCA9548A 8-channel I2C switch/multiplexer driver for ESP32-S2 / ESP32-S3
(Arduino framework, PlatformIO).

## Features

- **Injected I2C transport** - no Wire dependency in library code
- **8-channel control** - select single, multiple, or all channels
- **Health monitoring** - automatic state tracking (READY/DEGRADED/OFFLINE)
- **Deterministic behavior** - no unbounded loops, no heap allocations
- **Hardware reset support** - optional RESET pin callback
- **Address configurability** - 0x70–0x77 via A0–A2 pins
- **Managed synchronous lifecycle** - blocking I2C ops with cooperative `tick()`

## Hardware

**TCA9548A** specifications:
- 8-channel bidirectional I2C switch/multiplexer
- I2C interface (addresses: 0x70–0x77 via A0–A2 pins)
- Supply voltage: 1.65V–5.5V
- Low on-resistance: ~4 Ω (typical)
- Active-low hardware RESET input
- Supports 0–400 kHz I2C clock

**Typical wiring (ESP32):**
```
TCA9548A   ESP32
--------   -----
SDA    ->  GPIO8 (or custom)
SCL    ->  GPIO9 (or custom)
VCC    ->  3.3V
GND    ->  GND
RESET  ->  GPIO (optional, or pull to VCC)
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
#include "common/I2cTransport.h"

TCA9548A::TCA9548A mux;

void setup() {
  Serial.begin(115200);
  transport::initWire(8, 9, 400000, 50);

  TCA9548A::Config cfg;
  cfg.i2cWrite = transport::wireWrite;
  cfg.i2cWriteRead = transport::wireWriteRead;
  cfg.i2cUser = &Wire;
  cfg.i2cAddress = 0x70;

  auto status = mux.begin(cfg);
  if (!status.ok()) {
    Serial.printf("Init failed: %s\n", status.msg);
    return;
  }
  Serial.println("TCA9548A initialized!");
}

void loop() {
  mux.tick(millis());

  // Select channel 0 to talk to a downstream device
  if (mux.selectChannel(0).ok()) {
    // ... communicate with device on channel 0 ...
  }

  // Enable multiple channels simultaneously
  mux.setChannelMask(0x05);  // channels 0 and 2

  // Disable all channels when done
  mux.disableAll();
}
```

The example adapter maps Arduino `Wire` failures to specific `I2C_*` status codes and keeps
bus timeout ownership in `transport::initWire()`. If you do not inject `Config::nowMs`, the
driver falls back to `millis()` on Arduino/native-test builds.

## Versioning

The library version is defined in [library.json](library.json). A pre-build script automatically generates `include/TCA9548A/Version.h` with version constants.

**Print version in your code:**
```cpp
#include "TCA9548A/Version.h"

Serial.println(TCA9548A::VERSION);           // "1.0.0"
Serial.println(TCA9548A::VERSION_CODE);      // 10000
```

**Update version:** Edit `library.json` only. `Version.h` is auto-generated on every build.

## Transport Contract (Required)

Your I2C callbacks **must** return specific `Err` codes so the driver can make correct decisions:

- `Err::I2C_NACK_ADDR` - address NACK (device not responding)
- `Err::I2C_NACK_DATA` - data NACK
- `Err::I2C_TIMEOUT` - timeout
- `Err::I2C_BUS` - bus/arbitration error
- `Err::I2C_ERROR` - unspecified I2C failure

If your transport cannot distinguish these cases, return `Err::I2C_ERROR` and set `Status::detail` to the best available code.

### TCA9548A I2C Protocol Notes

- **No register address**: The TCA9548A has a single control register with no address byte. Write one byte directly; read returns the control register.
- **Last byte wins**: On multi-byte write, only the last byte is stored in the control register.
- **Channel takes effect after STOP**: The selected channels become active after the I2C STOP condition.

## Error Handling

All library functions return `Status` struct:

```cpp
struct Status {
  Err code;           // Error category (OK, I2C_ERROR, TIMEOUT, ...)
  int32_t detail;     // I2C error code or vendor detail
  const char* msg;    // Static error message (never heap-allocated)

  bool ok() const;    // Returns true if code == Err::OK
};
```

**Error codes:**
- `OK` - Operation successful
- `NOT_INITIALIZED` - Call `begin()` first
- `INVALID_CONFIG` - Missing transport callbacks or invalid parameters
- `INVALID_PARAM` - Channel number > 7
- `DEVICE_NOT_FOUND` - No ACK from device at configured address
- `I2C_NACK_ADDR` / `I2C_NACK_DATA` / `I2C_TIMEOUT` / `I2C_BUS` / `I2C_ERROR` - Transport failures

**Example error handling:**
```cpp
TCA9548A::Status st = mux.selectChannel(3);
if (!st.ok()) {
  Serial.printf("Error: %s (code=%d, detail=%d)\n",
                st.msg, static_cast<int>(st.code), st.detail);
}
```

## Health Monitoring

The driver tracks I2C communication health:

```cpp
// Check state
if (mux.state() == TCA9548A::DriverState::OFFLINE) {
  Serial.println("MUX offline!");
  mux.recover();  // Try to reconnect
}

// Get statistics
Serial.printf("Failures: %u consecutive, %lu total\n",
              mux.consecutiveFailures(),
              static_cast<unsigned long>(mux.totalFailures()));
```

### Driver States

| State | Description |
|-------|-------------|
| `UNINIT` | `begin()` not called or `end()` called |
| `READY` | Operational, no recent failures |
| `DEGRADED` | 1+ failures, below offline threshold |
| `OFFLINE` | Too many consecutive failures |

## API Reference

### Lifecycle

- `Status begin(const Config& config)` - Initialize driver
- `void tick(uint32_t nowMs)` - Cooperative update (no-op for TCA9548A)
- `void end()` - Shutdown driver

### Diagnostics

- `Status probe()` - Check device presence (no health tracking)
- `Status recover()` - Attempt recovery from DEGRADED/OFFLINE

### Channel Control

| Method | Description |
|--------|-------------|
| `selectChannel(ch)` | Enable one channel (0–7), disable all others |
| `setChannelMask(mask)` | Write raw bitmask to control register |
| `enableChannels(mask)` | Enable additional channels (OR with current) |
| `disableChannels(mask)` | Disable specific channels (AND NOT with current) |
| `disableAll()` | Write 0x00 to control register |
| `readChannelMask(mask)` | Read current control register value |
| `isChannelEnabled(ch, enabled)` | Check if a specific channel is enabled |

### State

- `DriverState state()` - Current driver state
- `bool isOnline()` - True if READY or DEGRADED

### Health

- `uint32_t lastOkMs()` - Timestamp of last success
- `uint32_t lastErrorMs()` - Timestamp of last failure
- `Status lastError()` - Most recent error
- `uint8_t consecutiveFailures()` - Failures since last success
- `uint32_t totalFailures()` - Lifetime failure count
- `uint32_t totalSuccess()` - Lifetime success count
- `uint8_t lastKnownMask()` - Cached channel mask from last successful I2C op

## Recovery

`recover()` attempts to restore communication:

1. Optional hard reset via `hardReset` callback (if `recoverUseHardReset` is true)
2. Tracked probe to verify device presence

Recovery uses `recoverBackoffMs` to avoid bus thrashing and does **not** run automatically inside `tick()`—the application controls retry strategy.

## Behavioral Contracts

**Threading Model:** Single-threaded by default. No FreeRTOS tasks created. Not ISR-safe.

**Timing:** `tick()` is a no-op (returns immediately). All other API calls perform synchronous I2C transactions bounded by `i2cTimeoutMs` (default 50 ms). Recovery backoff enforced via `recoverBackoffMs` (default 100 ms). No unbounded loops.

**Resource Ownership:** I2C transport and optional RESET pin passed via `Config`. No hardcoded pins or resources.

**Memory:** All allocation in `begin()`. Zero allocations in `tick()` and normal operations.

**Error Handling:** All errors returned as `Status`. No silent failures.

## Build

```bash
# Build for ESP32-S3
pio run -e esp32s3dev

# Upload and monitor
pio run -e esp32s3dev -t upload
pio device monitor -e esp32s3dev

# Run native tests (requires host gcc)
pio test -e native
```

## Examples

- `01_basic_bringup_cli/` - Interactive CLI for testing all channel operations

### Example Helpers (`examples/common/`)

Not part of the library. These simulate project-level glue and keep examples self-contained:

| File | Purpose |
|------|---------|
| `BoardConfig.h` | Pin definitions and Wire init for supported boards |
| `BuildConfig.h` | Compile-time `LOG_LEVEL` configuration |
| `Log.h` | Serial logging macros (`LOGE`/`LOGW`/`LOGI`/`LOGD`/`LOGT`/`LOGV`) |
| `I2cTransport.h` | Wire-based I2C transport adapter (`wireWrite`, `wireWriteRead`, `initWire`) |
| `I2cScanner.h` | I2C bus scanner with table output and bus recovery |
| `BusDiag.h` | Bus diagnostics wrapper (scan + probe) |
| `CliShell.h` | Serial command-line shell with line editing |
| `HealthView.h` | Compact health status display |

## Project Structure

```
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
  common/               - Example-only helpers (Log.h, BoardConfig.h, I2cTransport.h, etc.)
platformio.ini          - Build environments
library.json            - PlatformIO metadata
README.md               - This file
CHANGELOG.md            - Version history
AGENTS.md               - Coding guidelines
```

## Documentation

- [CHANGELOG.md](CHANGELOG.md) - full release history
- [AGENTS.md](AGENTS.md) - engineering guidelines
- [docs/IDF_PORT.md](docs/IDF_PORT.md) - ESP-IDF portability guidance

## License

MIT License. See [LICENSE](LICENSE) for details.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## References

- [TCA9548A Datasheet (TI)](https://www.ti.com/product/TCA9548A)
