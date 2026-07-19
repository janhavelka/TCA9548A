# Porting Guide

The driver is framework-neutral. It does not include Arduino, access `Wire`,
configure a controller, or call a framework clock or delay. The application
provides bounded transport callbacks and retains ownership of the bus.

## Required Transport

Configure both callbacks:

```cpp
TCA9548A::TransportStatus write(
    uint8_t address, const uint8_t* data, size_t length,
    uint32_t timeoutMs, void* user);

TCA9548A::TransportStatus read(
    uint8_t address, const uint8_t* txData, size_t txLength,
    uint8_t* rxData, size_t rxLength, uint32_t timeoutMs, void* user);
```

The driver makes only these protocol requests:

- Write: configured address, non-null data, `length == 1`. Success must include
  the terminating STOP because channel changes take effect only after STOP.
- Read: configured address, `txData == nullptr`, `txLength == 0`, non-null
  receive buffer, `rxLength == 1`. This is a read-only transaction with no
  register-pointer phase.

Each callback must make one physical attempt, finish within `timeoutMs`, and
return `NACK_ADDR`, `NACK_DATA`, `TIMEOUT`, `BUS`, or `OTHER` without retrying
or collapsing the cause. The driver maps that narrow result to public `Status`.

The callback context is borrowed until `end()`. One external owner must lock or
serialize all access. Driver calls and callbacks are not thread-safe, reentrant,
or ISR-safe.

## Optional Hooks

`nowMs` is a bounded, nonblocking monotonic millisecond source used only for
passive diagnostic timestamps. Omitting it leaves those timestamps at zero.

`hardReset(resetTimeoutMs, resetUser)` is an optional callback that owns the
active-low GPIO pulse. It must return only after RESET is released and must
honor the supplied finite timeout. `hardReset()` invokes it once, then performs
one exact-zero verification read. The library never restores the previous mask.

## Owner Integration Pattern

Ordinary public hardware operations are synchronous and perform at most one
transport callback. An external owner can therefore advance a routed target
operation with one transaction per poll:

1. write the required TCA9548A mask;
2. on a later owner poll, advance the downstream device operation;
3. after success, failure, cancellation, or timeout, write the reviewed idle
   mask, commonly all-off;
4. if cleanup is ambiguous, invalidate the library observation and reconcile by
   readback or board-level recovery.

The library has no queue, worker, retries, deadlines above the per-transfer
timeout, cancellation, or result identity. Those remain owner policy. External
controller recovery, POR, power cycling, and GPIO RESET must call
`invalidateChannelMask()` unless a subsequent library read already observed the
hardware state.

## ESP-IDF Adapter Shape

An ESP-IDF adapter typically maps one-byte writes to
`i2c_master_transmit()` and read-only requests to `i2c_master_receive()`. Use
the callback timeout as the physical-operation cap and map platform outcomes:

| Platform outcome | `TransportErr` |
| --- | --- |
| completed transaction | `OK` |
| address did not acknowledge | `NACK_ADDR` |
| data byte did not acknowledge | `NACK_DATA` |
| transfer deadline expired | `TIMEOUT` |
| stuck bus/arbitration/controller fault | `BUS` |
| other transport failure | `OTHER` |

Do not expose `esp_err_t` as the public code; retain it in
`TransportStatus::detail` when useful.

## Arduino Adapter Shape

The example adapter under `examples/common/I2cTransport.h` configures `Wire`
outside the library. Its serialized callback applies the supplied timeout to
the bus before each attempt. `endTransmission(true)` supplies the required STOP
and its result is mapped to `TransportStatus`. A production owner that cannot
set a per-attempt timeout must configure its bus-level timeout no larger than
`Config::i2cTimeoutMs`.
`TwoWire::requestFrom()` exposes only the received length, so this adapter maps
zero or short reads to `OTHER`; it does not invent a read-side NACK, timeout, or
bus cause that the API did not provide.

## Verification

Run the repository checks for every maintained adapter:

```bash
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e native_core_no_arduino
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

Also verify exact address, lengths, data byte, timeout propagation, STOP
completion, distinct error mapping, failed-write observation invalidation, and
that no direct framework includes enter `src/` or public headers.
