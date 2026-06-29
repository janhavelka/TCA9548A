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

/// Downstream poll callback used by select/restore poll jobs.
/// @param nowMs Current timestamp in milliseconds
/// @param maxInstructions Maximum downstream instructions allowed this poll
/// @param[out] instructionsUsed Downstream instructions consumed by callback
/// @param user User context pointer
/// @return OK when downstream work is complete, IN_PROGRESS to continue later,
///         or an error. The mux restore step still runs after a terminal result.
using PollDownstreamFn = Status (*)(uint32_t nowMs, uint8_t maxInstructions,
                                    uint8_t& instructionsUsed, void* user);

/// Result from one pollJob() call.
struct PollJobResult {
  uint8_t instructionsUsed = 0;  ///< Mux/downstream instructions consumed
  bool active = false;           ///< True if a job remains active after polling
  bool complete = false;         ///< True if the active job completed this call
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

  /// Convenience read-modify-write helper: enable one or more channels without
  /// changing others. Prefer setChannelMask() when the caller owns a cached mask.
  /// @param mask Bitmask of channels to enable (ORed with current state)
  /// @return Status::Ok() on success
  Status enableChannels(uint8_t mask);

  /// Convenience read-modify-write helper: disable one or more channels without
  /// changing others. Prefer setChannelMask() when the caller owns a cached mask.
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

  /// Read the single control byte through the register-oriented compatibility API.
  /// TCA9548A has no addressed register map and no register-address byte is sent
  /// on the bus. `reg` is only a logical identifier and must be `cmd::CONTROL_REG`.
  /// Prefer readChannelMask() or readControlRegister() for new TCA9548A code.
  /// @param reg Logical register identifier (must be `cmd::CONTROL_REG`)
  /// @param[out] value Current control-register value
  /// @return Status::Ok() on success, INVALID_PARAM if reg is invalid
  Status readRegister(uint8_t reg, uint8_t& value);

  /// Read the control register directly.
  /// Alias for readChannelMask() to match register-oriented sibling libraries.
  Status readControlRegister(uint8_t& mask) { return readChannelMask(mask); }

  /// Write the single control byte through the register-oriented compatibility API.
  /// TCA9548A has no addressed register map and no register-address byte is sent
  /// on the bus. `reg` is only a logical identifier and must be `cmd::CONTROL_REG`.
  /// Prefer setChannelMask() or writeControlRegister() for new TCA9548A code.
  /// @param reg Logical register identifier (must be `cmd::CONTROL_REG`)
  /// @param value New control-register value
  /// @return Status::Ok() on success, INVALID_PARAM if reg is invalid
  Status writeRegister(uint8_t reg, uint8_t value);

  /// Convenience read helper: check if a specific channel is currently enabled.
  /// Prefer readChannelMask() when the caller needs more than one bit.
  /// @param channel Channel number (0-7)
  /// @param[out] enabled True if channel is enabled
  /// @return Status::Ok() on success, INVALID_PARAM if channel > 7
  Status isChannelEnabled(uint8_t channel, bool& enabled);

  // =========================================================================
  // Driver State
  // =========================================================================

  /// Get current driver state
  DriverState state() const { return _driverState; }

  /// Uniform alias for state() used by sibling I2C libraries
  DriverState driverState() const { return state(); }

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

  // =========================================================================
  // Poll-Chunked Jobs
  // =========================================================================

  /// Start a one-instruction control-register read job.
  /// @param[out] mask Storage that must remain valid until the job completes
  Status startReadTca9548aMaskJob(uint8_t& mask);

  /// Start a one-instruction control-register write job.
  Status startSetTca9548aMaskJob(uint8_t mask);

  /// Start a two-step read-modify-write enable helper job.
  Status startEnableTca9548aChannelsJob(uint8_t mask);

  /// Start a two-step read-modify-write disable helper job.
  Status startDisableTca9548aChannelsJob(uint8_t mask);

  /// Start a select/downstream/restore job using a single channel.
  Status startSelectTca9548aChannelJob(uint8_t channel,
                                       uint8_t restoreMask,
                                       PollDownstreamFn downstreamPoll,
                                       void* downstreamUser);

  /// Start a select/downstream/restore job using a raw channel mask.
  Status startSelectTca9548aMaskJob(uint8_t selectMask,
                                    uint8_t restoreMask,
                                    PollDownstreamFn downstreamPoll,
                                    void* downstreamUser);

  /// Start a chunked recovery job.
  Status startRecoverTca9548aJob();

  /// Poll the active chunked job.
  /// @param nowMs Current timestamp in milliseconds
  /// @param maxInstructions Maximum instructions to execute in this call
  /// @param[out] result Instruction accounting and completion state
  /// @return OK when complete/no active job, IN_PROGRESS when work remains,
  ///         or the terminal error from the job.
  Status pollJob(uint32_t nowMs, uint8_t maxInstructions,
                 PollJobResult& result);

  /// True if a poll-chunked job is active.
  bool pollJobActive() const;

  /// Cancel an active poll-chunked job without doing bus I/O.
  void cancelPollJob();

private:
  enum class PollJobKind : uint8_t {
    NONE,
    READ_MASK,
    SET_MASK,
    ENABLE_CHANNELS,
    DISABLE_CHANNELS,
    SELECT_RESTORE,
    RECOVER
  };

  enum class PollJobPhase : uint8_t {
    IDLE,
    READ_MASK,
    WRITE_MASK,
    SELECT_MASK,
    DOWNSTREAM,
    RESTORE_MASK,
    RECOVER_BACKOFF,
    RECOVER_HARD_RESET,
    RECOVER_WRITE_KNOWN,
    RECOVER_VERIFY,
    RECOVER_RESTORE
  };

  // =========================================================================
  // Transport Wrappers
  // =========================================================================

  /// Raw I2C write (no health tracking)
  Status _i2cWriteRaw(const uint8_t* buf, size_t len);

  /// Raw I2C write-read (no health tracking)
  Status _i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                          uint8_t* rxBuf, size_t rxLen);

  /// Tracked I2C write (updates health)
  Status _i2cWriteTracked(const uint8_t* buf, size_t len,
                          bool allowOffline = false);

  /// Tracked I2C write-read (updates health)
  Status _i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                              uint8_t* rxBuf, size_t rxLen,
                              bool allowOffline = false);

  // =========================================================================
  // Health Management
  // =========================================================================

  /// Update health counters and state based on operation result
  /// Called ONLY from tracked transport wrappers
  Status _updateHealth(const Status& st);

  /// Reassert OFFLINE after a failed explicit recovery that started OFFLINE
  void _reassertOfflineLatchIfNeeded(bool startedOffline);

  /// Reset poll job state to idle
  void _clearPollJob();

  /// Return a standard IN_PROGRESS status
  static Status _inProgressStatus();

  /// Initialize a poll job after shared precondition checks
  Status _startPollJob(PollJobKind kind, PollJobPhase phase);

  // =========================================================================
  // Internal Helpers
  // =========================================================================

  /// Write a value to the control register (tracked)
  Status _writeControlReg(uint8_t value, bool allowOffline = false);

  /// Read the control register (tracked)
  Status _readControlReg(uint8_t& value, bool allowOffline = false);

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

  // Poll job state
  PollJobKind _pollJobKind = PollJobKind::NONE;
  PollJobPhase _pollJobPhase = PollJobPhase::IDLE;
  uint8_t _pollRequestedMask = cmd::NO_CHANNELS;
  uint8_t _pollTargetMask = cmd::NO_CHANNELS;
  uint8_t _pollCurrentMask = cmd::NO_CHANNELS;
  uint8_t _pollRestoreMask = cmd::NO_CHANNELS;
  uint8_t _pollRecoverDesiredMask = cmd::NO_CHANNELS;
  uint8_t* _pollReadMaskOut = nullptr;
  PollDownstreamFn _pollDownstream = nullptr;
  void* _pollDownstreamUser = nullptr;
  Status _pollPendingStatus = Status::Ok();
  bool _pollStartedOffline = false;
  bool _pollRecoverUseHardReset = false;
};

} // namespace TCA9548A
