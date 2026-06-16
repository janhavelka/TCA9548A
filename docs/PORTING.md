# Porting Guide

The core driver is framework-neutral. PlatformIO + Arduino is the first-party
example runtime, but library code does not include Arduino, use `Wire`, or call
`millis()`.

## Required Transport

Provide both I2C callbacks in `TCA9548A::Config`:

- `i2cWrite`: writes the one-byte TCA9548A control register through STOP.
- `i2cWriteRead`: reads the one-byte control register.

The transport owns the bus and must map platform-specific errors to
`TCA9548A::Status`. Do not leak `Wire`, ESP-IDF, or vendor HAL error values
through the public API.

The TCA9548A protocol has no register address byte. Write exactly the mask byte
for normal control-register writes, and read one byte for control-register
readback.

## Optional Hooks

- `hardReset` plus `resetUser`: pulse an external active-low RESET circuit.
- `nowMs` plus `timeUser`: provide monotonic milliseconds for health timestamps
  and recovery backoff.

If `nowMs` is not supplied, timestamps stay at `0` and `recoverBackoffMs` is not
enforced. This is intentional no-time behavior, not an Arduino fallback.

## ESP-IDF Shape

An ESP-IDF adapter usually wraps `i2c_master_transmit()` and the matching
write-read transaction. The callbacks should return:

- `OK` on a completed transaction.
- `I2C_NACK`, `I2C_TIMEOUT`, or `I2C_ERROR` for transport failures.
- `INVALID_CONFIG` or `INVALID_PARAM` only for adapter-side validation failures.

Example timing hook:

```cpp
static uint32_t idfNowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}
```

## Verification

Run the same checks used by the repository before relying on a new adapter:

```bash
pio test -e native
pio run -e native_core_no_arduino
pio run -e esp32s3dev
pio run -e esp32s2dev
```

Also confirm no direct Arduino includes, `Wire` access, or framework timing calls
are added to `src/` or public headers.
