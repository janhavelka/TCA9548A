# TCA9548A ESP-IDF Portability Status

Last audited: 2026-07-07

## Current Reality
- Primary runtime remains PlatformIO + Arduino.
- Transport is callback-based (`Config.i2cWrite`, `Config.i2cWriteRead`).
- Optional reset hook is supported (`Config.hardReset`).
- Optional timing hook is available (`Config.nowMs`).
- Core logic uses internal wrapper (`_nowMs`).
- `millis()` is the only Arduino fallback (inside `_nowMs` when `Config.nowMs` is null).

## ESP-IDF Adapter Requirements
To run under pure ESP-IDF, provide:
1. I2C write callback.
2. I2C write-read callback.
3. Optional hard reset callback.
4. Optional timing callback (`nowMs`).

## Minimal Adapter Pattern
```cpp
static uint32_t idfNowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

TCA9548A::Config cfg{};
cfg.i2cWrite = myI2cWrite;
cfg.i2cWriteRead = myI2cWriteRead;
cfg.nowMs = idfNowMs;
```

## Porting Notes
- TCA9548A has no periodic operations; `tick()` is a no-op.
- Preserve transport error mapping for NACK/timeout/bus errors (important for recovery logic).
- The TCA9548A uses no register address byte — write one byte directly, read returns the control register.

## Verification Checklist
- Native tests pass (`pio test -e native`).
- Example build passes (`pio run -e esp32s3dev`).
- No new direct Arduino timing calls are added outside wrapper fallback.
