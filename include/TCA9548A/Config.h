/// @file Config.h
/// @brief Configuration structure for TCA9548A driver
#pragma once

#include <cstddef>
#include <cstdint>

#include "TCA9548A/CommandTable.h"
#include "TCA9548A/Status.h"

namespace TCA9548A {

/// I2C write callback signature
/// @param addr      I2C device address (7-bit)
/// @param data      Pointer to data to write
/// @param len       Number of bytes to write
/// @param timeoutMs Maximum time to wait for completion
/// @param user      User context pointer passed through from Config
/// @return Status indicating success or failure
using I2cWriteFn = Status (*)(uint8_t addr, const uint8_t* data, size_t len,
                              uint32_t timeoutMs, void* user);

/// I2C write-read callback signature
/// @param addr      I2C device address (7-bit)
/// @param txData    Pointer to data to write (nullptr if txLen == 0)
/// @param txLen     Number of bytes to write (0 for read-only)
/// @param rxData    Pointer to buffer for read data
/// @param rxLen     Number of bytes to read
/// @param timeoutMs Maximum time to wait for completion
/// @param user      User context pointer passed through from Config
/// @return Status indicating success or failure
using I2cWriteReadFn = Status (*)(uint8_t addr, const uint8_t* txData,
                                  size_t txLen, uint8_t* rxData, size_t rxLen,
                                  uint32_t timeoutMs, void* user);

/// Optional hard-reset callback for the active-low RESET pin.
/// Implementations should assert RESET low for at least
/// cmd::RESET_MIN_LOW_NS and release it before returning.
/// @param user User context pointer (Config::i2cUser)
/// @return Status indicating success or failure
using HardResetFn = Status (*)(void* user);

/// Millisecond timestamp callback
/// @param user User context pointer passed through from Config
/// @return Current monotonic milliseconds
using NowMsFn = uint32_t (*)(void* user);

/// Configuration for TCA9548A driver
struct Config {
  // === I2C Transport (required) ===
  I2cWriteFn i2cWrite = nullptr;          ///< I2C write function pointer
  I2cWriteReadFn i2cWriteRead = nullptr;  ///< I2C write-read function pointer
  void* i2cUser = nullptr;                ///< User context for transport/reset callbacks
  HardResetFn hardReset = nullptr;        ///< Optional hardware reset callback

  // === Timing Hooks (optional) ===
  NowMsFn nowMs = nullptr;                ///< Monotonic millisecond source
  void* timeUser = nullptr;               ///< User context for timing hooks

  // === Device Settings ===
  uint8_t i2cAddress = cmd::DEFAULT_ADDRESS; ///< I2C address: 0x70-0x77
  uint32_t i2cTimeoutMs = 50;                ///< I2C transaction timeout in ms (1..60000)

  // === Health Tracking ===
  uint8_t offlineThreshold = 5;           ///< Consecutive failures before OFFLINE

  // === Recovery ===
  uint32_t recoverBackoffMs = 100;        ///< Minimum ms between recover() attempts
  bool recoverUseHardReset = true;        ///< Use hard reset in recover() if callback provided
};

} // namespace TCA9548A
