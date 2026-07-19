/// @file Config.h
/// @brief Configuration structure for TCA9548A driver
#pragma once

#include <cstddef>
#include <cstdint>

#include "TCA9548A/CommandTable.h"
#include "TCA9548A/Status.h"

namespace TCA9548A {

/// Narrow outcomes permitted from transport callbacks.
///
/// Driver lifecycle or scheduling states such as NOT_INITIALIZED, BUSY, and
/// IN_PROGRESS cannot cross the transport boundary.
enum class TransportErr : uint8_t {
  OK = 0,
  NACK_ADDR,
  NACK_DATA,
  TIMEOUT,
  BUS,
  OTHER
};

/// Result returned by an injected I2C transport.
struct TransportStatus {
  TransportErr code = TransportErr::OK;
  int32_t detail = 0; ///< Optional backend-specific diagnostic value

  constexpr TransportStatus() = default;
  constexpr TransportStatus(TransportErr error, int32_t detailCode)
      : code(error), detail(detailCode) {}

  constexpr bool ok() const { return code == TransportErr::OK; }
  static constexpr TransportStatus Ok() { return {}; }
  static constexpr TransportStatus Error(TransportErr error,
                                         int32_t detailCode = 0) {
    return TransportStatus{error, detailCode};
  }
};

/// I2C write callback signature.
///
/// This driver calls the callback with exactly one control byte. A successful
/// return MUST mean that the complete I2C transaction, including its
/// terminating STOP condition, has completed. The callback must return within
/// `timeoutMs` and must preserve the narrow outcome in TransportStatus (for
/// example address NACK, data NACK, timeout, or bus error). The driver never
/// retries a transfer.
/// @param addr      I2C device address (7-bit)
/// @param data      Pointer to data to write
/// @param len       Number of bytes to write
/// @param timeoutMs Maximum time to wait for completion
/// @param user      User context pointer passed through from Config
/// @return Narrow transport result
using I2cWriteFn = TransportStatus (*)(uint8_t addr, const uint8_t* data,
                                       size_t len, uint32_t timeoutMs,
                                       void* user);

/// I2C write-read callback signature.
///
/// The TCA9548A control-byte read is requested with `txData == nullptr`,
/// `txLen == 0`, and `rxLen == 1`; it is one read-only I2C transaction. A
/// successful return MUST mean that the complete transaction and terminating
/// STOP condition have completed. The callback must return within `timeoutMs`
/// and preserve the narrow transport outcome in TransportStatus. The driver
/// never retries a transfer.
/// @param addr      I2C device address (7-bit)
/// @param txData    Pointer to data to write (nullptr if txLen == 0)
/// @param txLen     Number of bytes to write (0 for read-only)
/// @param rxData    Pointer to buffer for read data
/// @param rxLen     Number of bytes to read
/// @param timeoutMs Maximum time to wait for completion
/// @param user      User context pointer passed through from Config
/// @return Narrow transport result
using I2cWriteReadFn = TransportStatus (*)(uint8_t addr,
                                           const uint8_t* txData, size_t txLen,
                                           uint8_t* rxData, size_t rxLen,
                                           uint32_t timeoutMs, void* user);

/// Optional hard-reset callback for the active-low RESET pin.
/// Implementations must assert RESET low for at least cmd::RESET_MIN_LOW_NS,
/// release it, and return only after RESET is complete. The callback is invoked
/// at most once per hardReset() call and must itself have a finite, documented
/// execution bound; the driver performs no delay or retry.
/// @param timeoutMs Maximum time allowed for the complete RESET operation
/// @param user User context pointer (Config::resetUser)
/// @return Status indicating success or failure
using HardResetFn = Status (*)(uint32_t timeoutMs, void* user);

/// Optional millisecond timestamp callback used only for passive diagnostics.
/// It must be monotonic, nonblocking, bounded, and perform no bus I/O.
/// @param user User context pointer passed through from Config
/// @return Current monotonic milliseconds
using NowMsFn = uint32_t (*)(void* user);

/// Configuration for TCA9548A driver.
///
/// begin() copies this value. The callback targets and context pointers named
/// by that copy are non-owning and must remain valid until end(). The
/// application owns bus configuration, serialization, locking, timeout
/// enforcement, retry, and recovery policy.
struct Config {
  // === I2C Transport (required) ===
  I2cWriteFn i2cWrite = nullptr;          ///< I2C write function pointer
  I2cWriteReadFn i2cWriteRead = nullptr;  ///< I2C write-read function pointer
  void* i2cUser = nullptr; ///< User context for transport callbacks
  HardResetFn hardReset = nullptr; ///< Optional hardware reset callback
  void* resetUser = nullptr; ///< User context for hardReset callback

  // === Timing Hooks (optional) ===
  NowMsFn nowMs = nullptr;                ///< Monotonic millisecond source
  void* timeUser = nullptr;               ///< User context for timing hooks

  // === Device Settings ===
  uint8_t i2cAddress = cmd::DEFAULT_ADDRESS; ///< I2C address: 0x70-0x77
  uint32_t i2cTimeoutMs = 50;   ///< I2C timeout in ms (1..60000)
  uint32_t resetTimeoutMs = 10; ///< RESET callback timeout in ms (1..60000)

  // === Passive Health Classification ===
  /// Consecutive failures at which state() reports OFFLINE (1..255). This
  /// diagnostic classification never prevents a transport operation.
  uint8_t offlineThreshold = 5;
};

} // namespace TCA9548A
