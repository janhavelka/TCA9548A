/// @file TCA9548A.h
/// @brief Production TCA9548A 8-channel I2C switch driver
#pragma once

#include <cstddef>
#include <cstdint>

#include "TCA9548A/CommandTable.h"
#include "TCA9548A/Config.h"
#include "TCA9548A/Status.h"
#include "TCA9548A/Version.h"

namespace TCA9548A {

/// Typed downstream channel identifier.
enum class Channel : uint8_t {
  CH0 = 0,
  CH1,
  CH2,
  CH3,
  CH4,
  CH5,
  CH6,
  CH7
};

/// Fixed-size value type for the TCA9548A control byte.
class ChannelMask {
public:
  constexpr ChannelMask() = default;

  static constexpr ChannelMask none() { return ChannelMask{cmd::NO_CHANNELS}; }
  static constexpr ChannelMask all() { return ChannelMask{cmd::ALL_CHANNELS}; }

  /// Return a one-hot mask. An out-of-range cast of Channel produces none().
  static constexpr ChannelMask one(Channel channel) {
    return static_cast<uint8_t>(channel) < cmd::NUM_CHANNELS
               ? ChannelMask{static_cast<uint8_t>(
                     1U << static_cast<uint8_t>(channel))}
               : none();
  }

  /// Explicit raw conversion for legitimate multi-channel masks.
  static constexpr ChannelMask fromRaw(uint8_t rawMask) {
    return ChannelMask{rawMask};
  }

  constexpr uint8_t raw() const { return _value; }

  constexpr bool contains(Channel channel) const {
    return static_cast<uint8_t>(channel) < cmd::NUM_CHANNELS &&
           (_value & one(channel)._value) != 0;
  }

  constexpr bool isNone() const { return _value == cmd::NO_CHANNELS; }

  constexpr bool isOneHot() const {
    return _value != 0 && (_value & static_cast<uint8_t>(_value - 1U)) == 0;
  }

  constexpr ChannelMask withEnabled(ChannelMask channels) const {
    return fromRaw(static_cast<uint8_t>(_value | channels._value));
  }

  constexpr ChannelMask withDisabled(ChannelMask channels) const {
    return fromRaw(
        static_cast<uint8_t>(_value & static_cast<uint8_t>(~channels._value)));
  }

private:
  explicit constexpr ChannelMask(uint8_t value) : _value(value) {}
  uint8_t _value = cmd::NO_CHANNELS;
};

static_assert(sizeof(ChannelMask) == sizeof(uint8_t),
              "ChannelMask must remain one byte");

/// Provenance of the cached channel-mask observation.
enum class MaskProvenance : uint8_t {
  UNKNOWN,             ///< Hardware state may differ from the cached byte
  WRITE_COMPLETED,     ///< Successful write returned after terminating STOP
  READBACK_OBSERVED    ///< Successful control-byte read observed this value
};

/// Truthful cached channel-mask value and its provenance.
struct ChannelMaskObservation {
  ChannelMask mask = ChannelMask::none();
  MaskProvenance provenance = MaskProvenance::UNKNOWN;

  constexpr bool known() const { return provenance != MaskProvenance::UNKNOWN; }
  constexpr bool verified() const {
    return provenance == MaskProvenance::READBACK_OBSERVED;
  }
};

/// Passive driver-health classification.
///
/// OFFLINE never blocks an operation; the external I2C owner retains admission,
/// retry, recovery, and bus-reset authority.
enum class DriverState : uint8_t {
  UNINIT,    ///< No successful device transaction in the current binding
  READY,     ///< Most recent tracked transport operation succeeded
  DEGRADED,  ///< 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    ///< consecutiveFailures >= offlineThreshold
};

/// Snapshot of current driver settings/state without performing I2C.
struct SettingsSnapshot {
  bool bound = false;                        ///< Valid Config is bound
  bool initialized = false;                  ///< Initial/tracked I2C succeeded
  DriverState state = DriverState::UNINIT;   ///< Passive health state
  uint8_t i2cAddress = cmd::DEFAULT_ADDRESS; ///< Active 7-bit device address
  uint32_t i2cTimeoutMs = 0;                 ///< Active I2C timeout
  uint32_t resetTimeoutMs = 0;               ///< Active RESET callback timeout
  uint8_t offlineThreshold = 0;              ///< Passive OFFLINE threshold
  bool hasNowMsHook = false;                 ///< Config::nowMs is provided
  bool hasHardReset = false;                 ///< Config::hardReset is provided
  ChannelMaskObservation maskObservation{};  ///< Cached mask and provenance
};

/// Managed synchronous TCA9548A driver.
///
/// Ownership and bounds:
/// - The driver is non-owning and never configures, locks, retries, or recovers
///   the I2C bus.
/// - Except for hardReset(), each fallible hardware API performs at most one
///   timeout-bounded transport transaction and never waits or retries.
/// - hardReset() is an explicit rare-operation exception: it invokes exactly
///   one reset callback and, if that callback succeeds, exactly one verification
///   read. Both receive finite configured timeouts, so its maximum callback
///   budget is resetTimeoutMs + i2cTimeoutMs; there are no waits/retries.
/// - Operations are synchronous and terminal. The owner may cancel between
///   calls; an in-flight callback must terminate within its supplied timeout.
/// - Calls are not thread-safe, reentrant, or ISR-safe. One external bus owner
///   must serialize driver and callback access.
class TCA9548A {
public:
  TCA9548A() = default;
  TCA9548A(const TCA9548A&) = delete;
  TCA9548A& operator=(const TCA9548A&) = delete;
  TCA9548A(TCA9548A&&) = delete;
  TCA9548A& operator=(TCA9548A&&) = delete;

  /// Validate and bind configuration, then perform one control-byte presence
  /// read as required by the managed lifecycle contract.
  ///
  /// A valid configuration remains bound if the presence read fails, and that
  /// exact transport Status is returned. The owner may retry probe(),
  /// readChannelMask(), or another primitive without rebinding. Rebinding is
  /// rejected with BUSY until end() is called.
  Status begin(const Config& config);

  /// No-op; the device has no pending I/O or state machine.
  void tick(uint32_t nowMs);

  /// Unbind without bus I/O. Explicitly call and check disableAll() first when
  /// safe-off is required.
  void end();

  /// Perform one raw diagnostic control-byte read without health accounting.
  /// A successful read stores READBACK_OBSERVED provenance. Exact transport
  /// errors are returned unchanged; the part has no identity register.
  Status probe();

  /// Make one explicit safe-off recovery attempt by writing 0x00.
  /// Performs one transfer, never asserts RESET or restores a previous mask.
  Status recover();

  /// Invoke RESET once with resetTimeoutMs and, on callback success, perform one
  /// verification read with i2cTimeoutMs. Success requires exactly 0x00; no
  /// prior mask is restored. A mismatch returns RESET_STATE_MISMATCH with the
  /// observed byte in detail and retains it as READBACK_OBSERVED.
  Status hardReset();

  /// Select one channel, disabling every other channel (one write).
  Status selectChannel(Channel channel);

  /// Apply an arbitrary channel mask (one write).
  Status writeChannelMask(ChannelMask mask);

  /// Disable every downstream channel (one write of 0x00).
  Status disableAll();

  /// Observe the applied channel mask (one read-only transaction).
  Status readChannelMask(ChannelMask& mask);

  DriverState state() const { return _driverState; }
  DriverState driverState() const { return state(); }
  /// True after a valid Config has been bound, even if begin() presence failed.
  bool isBound() const { return _bound; }

  /// True after the initial presence read or a tracked operation succeeds.
  /// A diagnostic probe() deliberately does not change health/lifecycle state.
  bool isInitialized() const { return _initialized; }
  bool isOnline() const {
    return _initialized && _driverState != DriverState::OFFLINE;
  }

  const Config& getConfig() const { return _config; }
  Status getSettings(SettingsSnapshot& out) const;

  /// Timestamp of the most recent successful tracked transport operation.
  uint32_t lastOkMs() const { return _lastOkMs; }

  /// Timestamp of the most recent failed tracked transport operation.
  uint32_t lastErrorMs() const { return _lastErrorMs; }

  /// Most recent failed tracked transport result mapped to public Status.
  Status lastError() const { return _lastError; }
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }
  uint32_t totalFailures() const { return _totalFailures; }
  uint32_t totalSuccess() const { return _totalSuccess; }

  ChannelMaskObservation channelMaskObservation() const {
    return _maskObservation;
  }

  /// Invalidate cached mask truth after external RESET/POR, controller recovery,
  /// backend reinitialization, or any other out-of-band hardware action.
  void invalidateChannelMask();

private:
  Status _requireBound() const;

  Status _i2cWriteRaw(const uint8_t* buf, size_t len);
  Status _i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                          uint8_t* rxBuf, size_t rxLen);
  Status _i2cWriteTracked(const uint8_t* buf, size_t len);
  Status _i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                              uint8_t* rxBuf, size_t rxLen);
  Status _updateHealth(const Status& st);

  Status _writeControlByte(ChannelMask mask);
  Status _readControlByte(ChannelMask& mask);
  Status _readControlByteRaw(ChannelMask& mask);
  void _recordMask(ChannelMask mask, MaskProvenance provenance);
  void _resetState();

  Config _config;
  bool _bound = false;
  bool _initialized = false;
  DriverState _driverState = DriverState::UNINIT;

  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;

  ChannelMaskObservation _maskObservation{};
};

} // namespace TCA9548A
