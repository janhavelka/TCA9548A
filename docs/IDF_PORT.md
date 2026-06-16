# TCA9548A ESP-IDF Portability Status

Last audited: 2026-06-16

## Current Reality
- Primary runtime remains PlatformIO + Arduino.
- Transport is callback-based (`Config.i2cWrite`, `Config.i2cWriteRead`).
- Optional reset hook is supported (`Config.hardReset`, `Config.resetUser`).
- Optional timing hook is available (`Config.nowMs`).
- Core timing is framework-neutral. If `Config.nowMs` is null, timestamps are `0`
  and `recoverBackoffMs` is not enforced.

## ESP-IDF Adapter Requirements
To run under pure ESP-IDF, provide:
1. I2C write callback.
2. I2C write-read callback.
3. Optional hard reset callback.
4. Optional timing callback (`nowMs`) if health timestamps or recovery backoff are needed.

## Minimal Adapter Pattern
```cpp
static uint32_t idfNowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

TCA9548A::Config cfg{};
cfg.i2cWrite = myI2cWrite;
cfg.i2cWriteRead = myI2cWriteRead;
cfg.nowMs = idfNowMs;
cfg.resetUser = myResetContext;
```

## Porting Notes
- TCA9548A has no periodic operations; `tick()` is a no-op.
- Preserve transport error mapping for NACK/timeout/bus errors (important for recovery logic).
- The TCA9548A uses no register address byte — write one byte directly, read returns the control register.

## Verification Checklist
- Native tests pass (`pio test -e native`).
- Core compile without Arduino includes passes (`pio run -e native_core_no_arduino`).
- Example build passes (`pio run -e esp32s3dev`).
- No direct Arduino includes or timing calls are added to `src/` or public headers.
