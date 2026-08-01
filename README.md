# TCA9548A Driver Library

Production-focused, framework-neutral driver for the TI TCA9548A 8-channel
I2C switch. The application owns the bus, serialization, pins, scheduling,
timeouts, retry, and recovery policy; the library owns only the chip's one-byte
control protocol and truthful local diagnostics.

## Highlights

- Injected, non-owning I2C transport; no `Wire`, Arduino, ESP-IDF, task, or
  logging dependency in library code.
- Typed `Channel` and one-byte `ChannelMask` API for one-hot and multi-channel
  control.
- One timeout-bounded transport transaction per ordinary hardware operation.
- No internal retries, waits, queues, heap allocation, downstream scheduler, or
  bus recovery policy.
- Exact address/data NACK, timeout, bus, and generic transport error mapping.
- Truthful cached-mask provenance: unknown, completed write, or observed
  readback.
- Optional bounded hardware RESET callback with exact `0x00` verification and
  no automatic route restore.
- Passive health counters that never deny an operation or take recovery
  authority from the application.
- Configurable address across `0x70` through `0x77`.

## Documentation Map

- Public API reference: `include/TCA9548A/TCA9548A.h` - lifecycle, operations,
  state, observations, and bounds
- [Porting guide](docs/PORTING.md) - transport callbacks and owner integration
- [Hardware notes](docs/HARDWARE_NOTES.md) - protocol, RESET, topology, and
  electrical constraints
- Example firmware: `examples/01_basic_bringup_cli/` - bounded Arduino bring-up
  CLI and HIL firmware contract
- [Contributing](CONTRIBUTING.md) and [security policy](SECURITY.md)

## Installation

For reproducible production builds, pin a reviewed full commit SHA rather than
a branch or tag name. After the annotated release tag exists, resolve its
peeled commit and compare it with the release evidence before updating the
dependency. Replace `vX.Y.Z` with the reviewed release:

```sh
git ls-remote --tags https://github.com/janhavelka/TCA9548A.git \
  'refs/tags/vX.Y.Z^{}'
```

Use the returned 40-character commit, not the tag object, branch, or tag name:

```ini
lib_deps =
  https://github.com/janhavelka/TCA9548A.git#<peeled-40-character-commit-sha>
```

This version-independent procedure avoids stale installation guidance inside
an immutable release while keeping the product repository on an exact,
auditable revision. For manual installation, copy `include/TCA9548A/` and
`src/` into the project.

## Quick Start

```cpp
#include "TCA9548A/TCA9548A.h"

TCA9548A::TCA9548A mux;
struct BusContext {};
BusContext myBusContext;

void reportTransportFailure(const TCA9548A::Status& status);

TCA9548A::TransportStatus i2cWrite(
    uint8_t address, const uint8_t* data, size_t length,
    uint32_t timeoutMs, void* user);

TCA9548A::TransportStatus i2cRead(
    uint8_t address, const uint8_t* txData, size_t txLength,
    uint8_t* rxData, size_t rxLength, uint32_t timeoutMs, void* user);

void setupMux() {
  TCA9548A::Config config;
  config.i2cWrite = i2cWrite;
  config.i2cWriteRead = i2cRead;
  config.i2cUser = &myBusContext;
  config.i2cAddress = 0x70;
  config.i2cTimeoutMs = 5;

  // begin() validates/binds Config and performs exactly one control-byte read.
  // A valid binding remains usable even if that initial read fails.
  const TCA9548A::Status started = mux.begin(config);
  if (!started.ok()) {
    reportTransportFailure(started);
  }
}

void useChannelZero() {
  const TCA9548A::Status selected =
      mux.selectChannel(TCA9548A::Channel::CH0);
  if (!selected.ok()) {
    return;
  }

  // The external I2C owner performs downstream work here.

  const TCA9548A::Status safe = mux.disableAll();
  if (!safe.ok()) {
    // Route state is now unknown; owner recovery/reconciliation is required.
  }
}
```

`tick(nowMs)` is intentionally a no-op for this device. `end()` only unbinds
and performs no I2C; call and check `disableAll()` first when the application
requires a safe-off shutdown.

The example owner additionally forces and verifies `0x00` after its initial
binding. An MCU-only restart does not prove the mux was power-cycled, so an
example must not inherit a previously selected route silently.

## Typed Channel Masks

```cpp
using TCA9548A::Channel;
using TCA9548A::ChannelMask;

constexpr ChannelMask one = ChannelMask::one(Channel::CH2);
constexpr ChannelMask several =
    ChannelMask::one(Channel::CH0).withEnabled(ChannelMask::one(Channel::CH2));
constexpr ChannelMask raw = ChannelMask::fromRaw(0xA5);

static_assert(one.raw() == 0x04);
static_assert(several.raw() == 0x05);
static_assert(raw.contains(Channel::CH7));
```

`ChannelMask` is statically constrained to one byte. An out-of-range cast to
`Channel` is rejected by `selectChannel()` before I2C; pure helpers return a
safe empty mask for such a cast.

## Transport Contract

`Config` requires two callbacks:

- `i2cWrite` receives exactly one control byte. Success means the entire write,
  including the terminating STOP, completed.
- `i2cWriteRead` receives `txData == nullptr`, `txLen == 0`, and `rxLen == 1`
  for one read-only control-byte transaction. Success also includes STOP.

Both callbacks must return within `timeoutMs`, perform one physical attempt,
and map the platform result to the narrow transport type:

```cpp
enum class TransportErr : uint8_t {
  OK,
  NACK_ADDR,
  NACK_DATA,
  TIMEOUT,
  BUS,
  OTHER,
};
```

The library maps these to `Err::I2C_NACK_ADDR`, `I2C_NACK_DATA`,
`I2C_TIMEOUT`, `I2C_BUS`, and `I2C_ERROR` without converting transport faults
to device absence. Callback contexts are borrowed and must remain valid until
`end()`.

Error fidelity is limited by the backend. The example `Wire` adapter can map
the documented `endTransmission()` write results, but `requestFrom()` exposes
only a received-byte count. Zero or short reads are therefore reported as
`OTHER`, not guessed to be NACK, timeout, or bus errors. Production backends
such as ESP-IDF should preserve the more precise cause when it is available.

The application must serialize access. The class is non-copyable,
non-movable, not thread-safe, not reentrant, and not ISR-safe.

## Status Contract

All fallible driver APIs return `Status`. Make control-flow decisions from
`Status::code`; `Status::msg` is a static diagnostic string, not a stable
machine-readable interface. `Status::detail` preserves backend diagnostics for
transport failures and contains the observed byte for
`RESET_STATE_MISMATCH`.

The core emits these result classes:

| Result | Meaning |
| --- | --- |
| `OK` | Operation completed successfully. |
| `NOT_INITIALIZED` | No valid configuration is bound. |
| `INVALID_CONFIG`, `INVALID_PARAM`, `BUSY`, `UNSUPPORTED` | Configuration, argument, lifecycle, or optional-feature precondition failed without I2C. |
| `I2C_NACK_ADDR`, `I2C_NACK_DATA`, `I2C_TIMEOUT`, `I2C_BUS`, `I2C_ERROR` | Exact mapped transport outcome; inspect `detail` for backend context. |
| `TIMEOUT` | A bounded non-I2C callback, currently hardware RESET, reported its own timeout. |
| `RESET_STATE_MISMATCH` | RESET callback completed, but readback observed a nonzero control byte. |

`DEVICE_NOT_FOUND` and `IN_PROGRESS` remain append-only compatibility values;
the synchronous core does not synthesize them. Device absence is reported as
the exact transport failure, normally `I2C_NACK_ADDR`. An asynchronous
`IN_PROGRESS` result from the RESET callback violates its terminal contract and
is returned as `INVALID_CONFIG`.

## Operation Classes And Bounds

### Steady-state primitives

Each of these performs at most one transport callback and never retries or
waits:

- `probe()` — diagnostic read without health-counter changes.
- `selectChannel(Channel)` — one one-hot write.
- `writeChannelMask(ChannelMask)` — one arbitrary-mask write.
- `readChannelMask(ChannelMask&)` — one read-only transaction.
- `disableAll()` — one `0x00` write.
- `recover()` — one explicit `0x00` safe-off write; it does not reset the bus,
  assert RESET, retry, read back, or restore an old mask.

`begin()` is the managed-lifecycle exception required by this library family:
it validates and stores a valid configuration, then performs exactly one
presence read. A transport failure is returned but does not discard that valid
binding, so the owner can retry a primitive later without rebooting or
rebinding. Calling `begin()` again while bound returns `BUSY`; call `end()`
first to rebind.

### Explicit RESET operation

`hardReset()` is a rare two-step operation:

1. invoke the configured callback once with `resetTimeoutMs`;
2. after callback success, perform one control-byte read with `i2cTimeoutMs`.

There is no delay, retry, or previous-mask restore. Success requires exactly
`0x00`. A different observed byte returns `RESET_STATE_MISMATCH` with the byte
in `Status::detail`. A missing callback returns `UNSUPPORTED`.

The callback must complete the active-low RESET pulse within its supplied
timeout. GPIO ownership and electrical safety remain application concerns.

### No internal multi-step jobs

The TCA9548A has no conversion, measurement, nonvolatile programming,
calibration, interrupt, or other long-running device procedure. The library
therefore has no job queue, progress/result identity, or cancellation API.
External owner tasks compose route selection, downstream transfers, cleanup,
deadlines, cancellation, and exactly-once result delivery across their own
polls. If owner recovery, POR, or external RESET may have changed the mask,
call `invalidateChannelMask()`.

## Mask Observation And Ambiguous Effects

`channelMaskObservation()` returns a byte plus provenance:

| Provenance | Meaning |
| --- | --- |
| `UNKNOWN` | Hardware may differ; do not publish the cached byte as applied. |
| `WRITE_COMPLETED` | The transport reported a completed write through STOP. |
| `READBACK_OBSERVED` | A successful read observed this hardware value. |

`known()` accepts either successful evidence; `verified()` is true only for
readback. A failed or ambiguous write invalidates the observation when the
callback returns failure. `hardReset()` invalidates before RESET; an exact-zero
read records verified
all-off, while a mismatch records the actual verified byte and returns an
error. Use explicit readback whenever application policy requires proof.

## Passive Health Diagnostics

Tracked primitives update `state()`, timestamps, last error, consecutive
failures, and saturating object-lifetime counters that survive `end()` and
rebinding. `OFFLINE` is diagnostic only and never blocks I2C. `probe()` is
intentionally raw and does not update health or `isOnline()`.
The external owner remains responsible for admission, retry, health policy,
controller recovery, RESET policy, and route reconciliation.

## Hardware Protocol Notes

- The part has one 8-bit control register and no register-address byte.
- Bit N enables downstream channel N; any combination is legal at chip level.
- The new selection takes effect only after STOP.
- POR and RESET clear the byte to `0x00`.
- The supported address range is `0x70` through `0x77`.
- Standard-mode and Fast-mode are supported up to 400 kHz.
- Enabled branches contribute pull-ups and capacitance to the active bus.

See [Hardware Notes](docs/HARDWARE_NOTES.md) and the
[Porting Guide](docs/PORTING.md) for integration details.

## Versioning

`library.json` is the version source of truth. `Version.h` is generated; do not
edit it manually.

```cpp
#include "TCA9548A/Version.h"
Serial.println(TCA9548A::VERSION);
```

## Repository Validation

These maintainer checks require a full Git checkout. Installed library packages
intentionally omit CI and native-test scaffolding.

The ESP32-S2/S3 example environments exact-pin pioarduino
`platform-espressif32` `55.03.311`, which supplies Arduino-ESP32 `3.3.11`,
ESP-IDF `5.5.5`, and GCC `14.2.0`. This pin applies only to repository example
and HIL builds; consuming applications continue to own their platform version.
The `version` CLI command reports the runtime Arduino and ESP-IDF versions so
hardware evidence can identify the actual framework stack. It also reports MCU,
flash, and PSRAM identity so the S3 memory configuration can be checked on the
fixture instead of inferred from a successful compile.

```bash
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e native_core_no_arduino
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python tools/tca9548a_hil.py --parser-self-test
doxygen Doxyfile
python -m platformio pkg pack . --output .pio/TCA9548A.tar.gz
git diff --check
```

Generated Doxygen HTML is written to `docs/doxygen/html/` and is intentionally
ignored by Git. CI repeats documentation and package generation with pinned
tool versions and treats Doxygen warnings as errors.

Live HIL requires an attached ESP32 and TCA9548A fixture:

```bash
python tools/tca9548a_hil.py --port COM8 --baud 115200 --verbose
```

A live run requires RESET validation by default and exits nonzero if required
cases are `NOT_RUN`. `--skip-reset` is an explicit diagnostic exception and is
not release HIL evidence. Use `--allow-not-run` only for an explicitly accepted
missing-fixture run; FAIL and UNKNOWN remain failures. `--dry-run` validates
only the plan and never counts as hardware evidence.

## Example

`examples/01_basic_bringup_cli/` provides a fixed-buffer Arduino bring-up CLI.
Its `Wire` transport and board pins live under `examples/common/` and are not
part of the public library API. The CLI exposes typed mask operations, passive
health, safe-off recovery, and the HIL contract. `scan`, `stress`, and
`stress_mix` are explicit maintenance diagnostics: they are finite, yield after
each transaction, block command processing until complete, and always finish
stress at verified all-off. Scan makes exactly 126 probes. Stress makes at most
the requested 1,000 operations plus one safe-off write and one verification
read; with the default 50 ms Wire timeout its transport bound is 50.1 seconds.

## License

MIT. See [LICENSE](LICENSE).
