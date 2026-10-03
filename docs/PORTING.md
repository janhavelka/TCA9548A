# Porting Guide

The driver is framework-neutral. It does not include Arduino, access `Wire`,
configure a controller, or call a framework clock or delay. The application
provides bounded transport callbacks and retains ownership of the bus.
Only a C++17 compiler and the standard fixed-width/size types are required.
Use the standalone CMake target or compile `src/TCA9548A.cpp` with `include/`
on the search path. Arduino and ESP-IDF are example adapter choices; neither
is required by the core. See [installation](../README.md#installation).

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
  register-pointer phase, and it must be its own START-to-STOP transaction.
  Never implement it as a repeated-START read chained to a write: the mux then
  returns the new byte while its switches have not changed (TI SCPA063,
  section 4.1).

The controller terminates the single read byte with NACK, then STOP. It must
also enforce the bus-free interval from STOP to the next START: at least
4.7 us in Standard-mode or 1.3 us in Fast-mode (datasheet section 5.6). This
applies to immediate successive calls and downstream transfers after a mask
write. The core does not delay or configure controller timing.

Each callback may make at most one physical attempt, must finish within
`timeoutMs`, and return `OK` or a failure: `NACK_ADDR`, `NACK_DATA`, `TIMEOUT`,
`BUS`, or `OTHER`, without retrying or collapsing the cause. The driver maps
that narrow result to public `Status`.
The timeout is a callback contract, not a preemption mechanism: this synchronous
driver cannot interrupt a backend that ignores it.

On failure, `TransportStatus::detail` is an opaque signed backend diagnostic
preserved in the mapped public `Status::detail`. Successful callbacks produce
`Status::Ok()` with detail zero. Applications must branch on the typed error
code, not on `detail` or the human-readable `Status::msg`, unless a specific
adapter separately defines stable detail values.

The callback context is borrowed until `end()`. One external owner must lock or
serialize all access. Driver calls and callbacks are not thread-safe, reentrant,
or ISR-safe.

## Optional Hooks

`nowMs` is a bounded, nonblocking monotonic millisecond source used only for
passive diagnostic timestamps. Omitting it leaves those timestamps at zero.
An owner using a 64-bit uptime may return its low 32 bits here; wrapping
diagnostic timestamps are supported. Retain the full-width operation deadline
in the owner, independently of this hook.

`hardReset(resetTimeoutMs, resetUser)` is an optional callback that owns the
active-low GPIO pulse. Assert RESET low for at least 6 ns, release it, and wait
until the 500 ns maximum propagation time from assertion has elapsed, all within
the supplied finite timeout. Return `Err::TIMEOUT` when that bound expires
and `Err::RESET_ERROR` for another GPIO/reset failure. `hardReset()` invokes it
once and, only if it succeeds, performs one exact-zero verification read.
The library never restores the previous mask.

## Owner Integration Pattern

Ordinary public hardware operations are synchronous and perform at most one
transport callback. A simple application may select a route, perform its
downstream transfer, and restore its idle mask in one synchronous sequence.
Serialize that entire sequence against any other caller of the bus, not just
the individual transfers. Check each result, including cleanup. No task,
queue, fixed transfer budget, or polling cadence is required by the library.

An application with a cooperative scheduler may instead stage a routed
operation across polls, retaining exclusive route ownership throughout:

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

For a dedicated sole I2C task, store the driver and callback context inside the
owner and call the synchronous primitives only from that task. Queue application
intent to the owner; do not call the same driver instance from producers. The
owner must prevent callback re-entry, may split route selection, downstream
work, and route restoration across separate polls, and retains the end-to-end
deadline and cancellation identity across those calls. The driver adds no task,
lock, queue, route lease, or bus-recovery policy of its own.

The maintained Arduino and native ESP-IDF CLIs are direct-owner bring-up
examples: while a command runs, that example owns the controller and no second
caller accesses it. In a production system with an existing sole I2C task, CLI
handlers must enqueue bounded intent to that owner and consume copied results;
they must not call this driver or the controller from the console task.

## Integrating A Bounded Bus Owner

The default `Config::i2cTimeoutMs` is 20 ms. Both maintained examples use that
same default. This is a per-transfer upper budget, not a target duration or a
whole-operation deadline. Explicit settings from 1 to 60000 ms remain valid
for other transports. An owner's stricter cap must still be enforced even if
the application supplies a larger library setting.

### Initialization without I/O during binding

During application binding, store the callbacks and `Config` in the owner or
device wrapper; do not call `begin()` yet. Once the controller is ready, a
permitted owner step calls `begin(config)`, which makes exactly one control-byte
read. The library deliberately does not acquire the bus or defer work itself.

Check `isBound()` after a failed `begin()`. If true, a valid binding was
retained: a later `readChannelMask()` retries the read and can establish READY.
Calling `begin()` again would return BUSY; a successful raw `probe()` would
leave health UNINIT. Validation failures leave the instance unbound and need
configuration correction before another `begin()`.

Do not treat a successful presence read as all-off. An MCU-only reboot may
leave the mux powered with an old route. Before normal downstream traffic,
schedule `disableAll()` and, when verified isolation is required, a separate
`readChannelMask()` that must return zero. Each is one transfer; each may fail.

### Callback bridge and deadlines

Map the two callbacks directly to the owner's existing single-transfer path:

| Driver callback | Write bytes | Read bytes | Successful owner evidence |
| --- | ---: | ---: | --- |
| `i2cWrite` | 1 | 0 | exactly one written byte, completed STOP, committed write if the backend reports write effects |
| `i2cWriteRead` | 0, null pointer | 1 | exactly one received byte and completed NACK/STOP |

An address-only probe does not replace the read. Do not synthesize a register
pointer or combine mux selection and a downstream transaction. If the owner
requires a registered device identity, pass the configured mux identity through
that owner; do not bypass its admission checks to reach the controller.

For each attempt, the owner/adapter must:

1. Check cancellation and deadline expiry before calling the driver. In the
   callback, check the deadline again before any physical transfer.
2. Compute `min(callback timeout, owner per-call cap, remaining operation time)`.
   Keep deadline arithmetic in the owner's 64-bit time domain; narrow only the
   resulting bounded duration. Preserve the owner's wrap-safe comparisons.
   Refuse an expired budget rather than extending it or passing zero to an SDK.
3. Perform one attempt and retain the complete owner result, including byte
   counts, completion time, backend detail, and any write-effect evidence.
4. Preserve an actual transfer failure. For an otherwise successful transfer,
   reject short counts, missing completion evidence, or completion at/after the
   operation deadline. A late write can have reached the chip: return TIMEOUT
   and retain its effect in the owner rather than turning it into success.

Map a phase-unknown NACK to `TransportErr::OTHER`; zero reported written bytes
does not prove an address NACK. Preserve narrower errors only when the backend
actually distinguishes them. The driver intentionally retains only its narrow
status and an opaque detail; it invalidates mask evidence on any callback
failure and does not retry. A callback rejected before physical I/O still
reports a transport failure to the driver, so avoid calling it for work the
owner already knows is expired or cancelled.

### Routing, cleanup, and reset steps

The application retains each downstream device's mux address/channel mapping.
Select the route before its first probe or transfer and prevent other callers
from changing it until that routed operation finishes. Selection, target work,
idle-mask cleanup, and optional verification can each occupy separate owner
steps. The existing primitives suffice; no chip-specific scheduler is needed
inside the library.

After timeout or cancellation, the original operation deadline may be unusable
for cleanup. Keep downstream access blocked and schedule cleanup with its own
explicit finite maintenance budget. Do not silently renew the failed operation
or publish cleanup success merely because it was attempted. A failed all-off
write leaves the route unknown; reconcile by readback, bounded recovery, or
external RESET before admitting further downstream traffic. Retain the original
operation error separately from any cleanup error.

`hardReset()` is a maintenance exception: one RESET callback followed by one
verification read, with a default combined callback budget of 30 ms. For an
owner that cannot admit that combined call, invalidate mask evidence **before**
attempting an externally controlled RESET pulse, then read the mask on a later
step and check for zero. GPIO failure or a nonzero read is an error. Controller
recovery also invalidates route evidence; mux RESET does not itself recreate
the controller. `end()` is bus-silent and never substitutes for cleanup.

The native driver suite exercises deadline clipping, deferred initialization,
late completion, and separately budgeted cleanup using a scripted owner. These
tests establish software behavior; they do not measure a real controller's
wall-clock bound or validate a product's routing implementation.

## ESP-IDF Adapter Shape

The maintained native example under `examples/espidf_basic/` targets ESP-IDF
5.4/5.5 and maps one-byte writes to
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

If the backend cannot distinguish address NACK from data NACK, or timeout from a
generic failure, return the narrowest truthful result it actually exposes.
Never infer a more specific fault from elapsed time or a short byte count alone.

For physical transfers, the maintained ESP-IDF adapter maps `ESP_ERR_TIMEOUT`
to `TIMEOUT` and other non-OK SDK results to `OTHER`, preserving the original
`esp_err_t` in `detail`.
It does not infer an address or data NACK phase from these generic results.

## Arduino Adapter Shape

The example adapter under `examples/common/I2cTransport.h` configures `Wire`
outside the library. Its serialized callback applies the supplied timeout to
the bus before each attempt. `endTransmission(true)` supplies the required STOP
and its result is mapped to `TransportStatus`. A production owner that cannot
set a per-attempt timeout must configure its bus-level timeout no larger than
`Config::i2cTimeoutMs` and the owner's cap. If the remaining operation budget
is shorter than that fixed timeout, refuse the attempt.
`TwoWire::requestFrom()` exposes only the received length, so this adapter maps
zero or short reads to `OTHER`; it does not invent a read-side NACK, timeout, or
bus cause that the API did not provide.

The ESP32 write result `2` combines address and data NACK, so the example maps
it to `OTHER` with detail `2`; code `3`, if exposed, maps to `NACK_DATA`.
Both example adapters accept only the exact one-byte TCA transaction shapes.
The Wire adapter rejects out-of-range timeouts before converting to its
16-bit setting, and does not turn a combined request into two separately
timed transfers. The native adapter registers its device during owner setup,
outside timed callbacks.

## Backend Fault And Timing Limits

The pinned ESP-IDF 5.5.5 driver can retain receive state after a failed read
and expose stale descriptor indices to a later address probe. Both bring-up
owners here latch a receive fault and refuse further controller calls
until MCU restart. `cfg` reports this state. Driver `end()`/`begin()` and mux
RESET alone do not clear it. This is example-owner policy, not a new core
health gate or hidden retry. Production recovery should recreate the native
controller explicitly, preserve cleanup failures, and invalidate route state.
See the [pinned SDK implementation](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_i2c/i2c_master.c).

That SDK's `i2c_master_probe()` uses 100 kHz independently of registered-device
speed. The pinned Arduino Wire backend also uses it for address-only writes.
Therefore scan-based route checks are 100 kHz reachability evidence; control
read/write uses the configured clock. Qualifying downstream payload transfers
at 400 kHz requires the target driver and a scope/logic-analyzer capture.

The core can enforce neither SDK scheduling latency nor internal lock waits.
The example Wire backend contains unbounded mutex acquisition internally and
lazily creates its first device handle; its configured timeout bounds the
physical attempt, not arbitrary contention/setup overhead. Examples rely on
sole ownership. Register handles during setup where possible, budget SDK tick
rounding, and establish the remaining bound under faults. When an application
has an overall operation deadline, clip each callback's timeout to its remaining
budget and refuse expired work before I/O. That policy alone cannot bound
unbounded SDK lock waits. Never copy the direct Wire example into a shared
multi-task bus and claim a hard wall-clock bound.

## Verification

Run the repository checks listed in [CONTRIBUTING.md](../CONTRIBUTING.md) for
every maintained adapter. Also verify the exact address, lengths, data byte,
timeout propagation, STOP completion, distinct error mapping, failed-write
observation invalidation, and that no framework include enters `src/` or the
public headers. Run `doxygen Doxyfile` after changing a public declaration;
undocumented public API and incomplete parameter/return documentation fail the
documentation build.
