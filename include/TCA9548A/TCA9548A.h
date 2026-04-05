/// @file TCA9548A.h
/// @brief Main driver class for TCA9548A 8-channel I2C switch
#pragma once

#include <cstddef>
#include <cstdint>

#include "TCA9548A/CommandTable.h"
#include "TCA9548A/Config.h"
#include "TCA9548A/Status.h"
#include "TCA9548A/Version.h"

namespace TCA9548A {

/// Driver state for health monitoring
enum class DriverState : uint8_t {
  UNINIT,    ///< begin() not called or end() called
  READY,     ///< Operational, consecutiveFailures == 0
  DEGRADED,  ///< 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    ///< consecutiveFailures >= offlineThreshold
};

/// Snapshot of current driver settings/state without performing I2C.
struct SettingsSnapshot {
  bool initialized = false;                  ///< True after begin() succeeds
  DriverState state = DriverState::UNINIT;   ///< Current driver state
  uint8_t i2cAddress = cmd::DEFAULT_ADDRESS; ///< Active 7-bit device address
  uint32_t i2cTimeoutMs = 0;                 ///< Active transport timeout
  uint8_t offlineThreshold = 0;              ///< Consecutive failures before OFFLINE
  uint32_t recoverBackoffMs = 0;             ///< Minimum ms between recover() attempts
  bool hasNowMsHook = false;                 ///< True when Config::nowMs is provided
  bool hasHardReset = false;                 ///< True when Config::hardReset is provided
  bool recoverUseHardReset = false;          ///< True when recover() may use hard reset
  uint8_t lastKnownMask = cmd::NO_CHANNELS;  ///< Cached control-register value
};

/// TCA9548A driver class
class TCA9548A {
public:
  // =========================================================================
  // Lifecycle
  // =========================================================================

  /// Initialize the driver with configuration
  /// @param config Configuration including transport callbacks
  /// @return Status::Ok() on success, error otherwise
  Status begin(const Config& config);

  /// Process pending operations (no-op for TCA9548A)
  /// @param nowMs Current timestamp in milliseconds
  void tick(uint32_t nowMs);

  /// Shutdown the driver and release resources
  void end();

  // =========================================================================
  // Diagnostics
  // =========================================================================

  /// Check if device is present on the bus (no health tracking)
  /// @return Status::Ok() if device responds, error otherwise
  Status probe();

  /// Attempt to recover from DEGRADED/OFFLINE state
  /// @return Status::Ok() if device now responsive, error otherwise
  Status recover();

  /// Assert the optional RESET pin and verify the device responds again.
  /// The TCA9548A should return with all channels disabled (0x00).
  /// @return Status::Ok() on success, UNSUPPORTED if no hard-reset callback exists
  Status hardReset();

  // =========================================================================
  // Channel Control
  // =========================================================================

  /// Select a single channel (0-7), disabling all others
  /// @param channel Channel number (0-7)
  /// @return Status::Ok() on success, INVALID_PARAM if channel > 7
  Status selectChannel(uint8_t channel);

  /// Write a raw channel bitmask to the control register
  /// @param mask Bitmask where bit N enables channel N
  /// @return Status::Ok() on success
  Status setChannelMask(uint8_t mask);

  /// Write the control register directly.
  /// Alias for setChannelMask() to match register-oriented sibling libraries.
  Status writeControlRegister(uint8_t mask) { return setChannelMask(mask); }

  /// Enable one or more channels without changing others
  /// @param mask Bitmask of channels to enable (ORed with current state)
  /// @return Status::Ok() on success
  Status enableChannels(uint8_t mask);

  /// Disable one or more channels without changing others
  /// @param mask Bitmask of channels to disable (cleared from current state)
  /// @return Status::Ok() on success
  Status disableChannels(uint8_t mask);

  /// Disable all downstream channels (write 0x00)
  /// @return Status::Ok() on success
  Status disableAll();

  /// Read the current control register value
  /// @param[out] mask Current channel bitmask
  /// @return Status::Ok() on success
  Status readChannelMask(uint8_t& mask);

  /// Read the control register by register address.
  /// @param reg Register address (must be `cmd::CONTROL_REG`)
  /// @param[out] value Current control-register value
  /// @return Status::Ok() on success, INVALID_PARAM if reg is invalid
  Status readRegister(uint8_t reg, uint8_t& value);

  /// Read the control register directly.
  /// Alias for readChannelMask() to match register-oriented sibling libraries.
  Status readControlRegister(uint8_t& mask) { return readChannelMask(mask); }

  /// Write the control register by register address.
  /// @param reg Register address (must be `cmd::CONTROL_REG`)
  /// @param value New control-register value
  /// @return Status::Ok() on success, INVALID_PARAM if reg is invalid
  Status writeRegister(uint8_t reg, uint8_t value);

  /// Check if a specific channel is currently enabled
  /// @param channel Channel number (0-7)
  /// @param[out] enabled True if channel is enabled
  /// @return Status::Ok() on success, INVALID_PARAM if channel > 7
  Status isChannelEnabled(uint8_t channel, bool& enabled);

  // =========================================================================
  // Driver State
  // =========================================================================

  /// Get current driver state
  DriverState state() const { return _driverState; }

  /// Check if begin() has completed successfully
  bool isInitialized() const { return _initialized; }

  /// Check if driver is ready for operations
  bool isOnline() const {
    return _driverState == DriverState::READY ||
           _driverState == DriverState::DEGRADED;
  }

  /// Get a copy of the active configuration
  const Config& getConfig() const { return _config; }

  /// Get a snapshot of current configuration/runtime state (no I2C)
  Status getSettings(SettingsSnapshot& out) const;

  // =========================================================================
  // Health Tracking
  // =========================================================================

  /// Timestamp of last successful I2C operation
  uint32_t lastOkMs() const { return _lastOkMs; }

  /// Timestamp of last failed I2C operation
  uint32_t lastErrorMs() const { return _lastErrorMs; }

  /// Most recent error status
  Status lastError() const { return _lastError; }

  /// Consecutive failures since last success
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }

  /// Total failure count (lifetime)
  uint32_t totalFailures() const { return _totalFailures; }

  /// Total success count (lifetime)
  uint32_t totalSuccess() const { return _totalSuccess; }

  /// Last known channel mask (cached from last successful read/write)
  uint8_t lastKnownMask() const { return _lastKnownMask; }

private:
  // =========================================================================
  // Transport Wrappers
  // =========================================================================

  /// Raw I2C write (no health tracking)
  Status _i2cWriteRaw(const uint8_t* buf, size_t len);

  /// Raw I2C write-read (no health tracking)
  Status _i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                          uint8_t* rxBuf, size_t rxLen);

  /// Tracked I2C write (updates health)
  Status _i2cWriteTracked(const uint8_t* buf, size_t len);

  /// Tracked I2C write-read (updates health)
  Status _i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                              uint8_t* rxBuf, size_t rxLen);

  // =========================================================================
  // Health Management
  // =========================================================================

  /// Update health counters and state based on operation result
  /// Called ONLY from tracked transport wrappers
  Status _updateHealth(const Status& st);

  // =========================================================================
  // Internal Helpers
  // =========================================================================

  /// Write a value to the control register (tracked)
  Status _writeControlReg(uint8_t value);

  /// Read the control register (tracked)
  Status _readControlReg(uint8_t& value);

  /// Read the control register (raw, untracked)
  Status _readControlRegRaw(uint8_t& value);

  // =========================================================================
  // State
  // =========================================================================

  Config _config;
  bool _initialized = false;
  DriverState _driverState = DriverState::UNINIT;

  // Health counters
  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;

  // Recovery
  uint32_t _lastRecoverMs = 0;
  bool _lastRecoverValid = false;

  // Cached channel state
  uint8_t _lastKnownMask = cmd::NO_CHANNELS;
};

} // namespace TCA9548A
