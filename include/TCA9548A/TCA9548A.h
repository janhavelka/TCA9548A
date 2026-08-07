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
  CH0 = 0, ///< Downstream channel 0 (control bit 0)
  CH1,     ///< Downstream channel 1 (control bit 1)
  CH2,     ///< Downstream channel 2 (control bit 2)
  CH3,     ///< Downstream channel 3 (control bit 3)
  CH4,     ///< Downstream channel 4 (control bit 4)
  CH5,     ///< Downstream channel 5 (control bit 5)
  CH6,     ///< Downstream channel 6 (control bit 6)
  CH7      ///< Downstream channel 7 (control bit 7)
};

/// Fixed-size value type for the TCA9548A control byte.
class ChannelMask {
public:
  /// Construct the safe all-disabled mask.
  constexpr ChannelMask() = default;

  /// Construct the safe all-disabled mask.
  /// @return Mask value 0x00.
  static constexpr ChannelMask none() { return ChannelMask{cmd::NO_CHANNELS}; }

  /// Construct the all-enabled mask.
  /// @return Mask value 0xFF.
  static constexpr ChannelMask all() { return ChannelMask{cmd::ALL_CHANNELS}; }

  /// Return a one-hot mask. An out-of-range cast of Channel produces none().
  /// @param channel Typed downstream channel.
  /// @return One-hot mask, or none() for an invalid cast value.
  static constexpr ChannelMask one(Channel channel) {
    return static_cast<uint8_t>(channel) < cmd::NUM_CHANNELS
               ? ChannelMask{static_cast<uint8_t>(
                     1U << static_cast<uint8_t>(channel))}
               : none();
  }

  /// Explicit raw conversion for legitimate multi-channel masks.
  /// @param rawMask Complete control-byte value.
  /// @return Mask containing exactly rawMask.
  static constexpr ChannelMask fromRaw(uint8_t rawMask) {
    return ChannelMask{rawMask};
  }

  /// Return the encoded control byte.
  /// @return Raw 8-bit channel mask.
  constexpr uint8_t raw() const { return _value; }

  /// Test whether a channel is enabled in this value.
  /// @param channel Typed downstream channel.
  /// @return true when the channel is valid and its bit is set.
  constexpr bool contains(Channel channel) const {
    return static_cast<uint8_t>(channel) < cmd::NUM_CHANNELS &&
           (_value & one(channel)._value) != 0;
  }

  /// Test whether all channels are disabled.
  /// @return true only for mask value 0x00.
  constexpr bool isNone() const { return _value == cmd::NO_CHANNELS; }

  /// Test whether exactly one channel is enabled.
  /// @return true only for nonzero masks containing one set bit.
  constexpr bool isOneHot() const {
    return _value != 0 && (_value & static_cast<uint8_t>(_value - 1U)) == 0;
  }

  /// Return this mask with additional channels enabled.
  /// @param channels Bits to enable.
  /// @return Bitwise union of this mask and channels.
  constexpr ChannelMask withEnabled(ChannelMask channels) const {
    return fromRaw(static_cast<uint8_t>(_value | channels._value));
  }

  /// Return this mask with selected channels disabled.
  /// @param channels Bits to disable.
  /// @return This mask with every bit in channels cleared.
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

/// Return the stable allocation-free display name for mask evidence.
/// @param provenance Evidence classification to describe.
/// @return Static-lifetime symbolic name, or "UNKNOWN" for an invalid cast.
constexpr const char* maskProvenanceName(MaskProvenance provenance) {
  switch (provenance) {
    case MaskProvenance::UNKNOWN: return "UNKNOWN";
    case MaskProvenance::WRITE_COMPLETED: return "WRITE_COMPLETED";
    case MaskProvenance::READBACK_OBSERVED: return "READBACK_OBSERVED";
  }
  return "UNKNOWN";
}

/// Compatibility overload for maskProvenanceName().
/// @param provenance Evidence classification to describe.
/// @return Static-lifetime symbolic name.
constexpr const char* toString(MaskProvenance provenance) {
  return maskProvenanceName(provenance);
}

/// Truthful cached channel-mask value and its provenance.
struct ChannelMaskObservation {
  ChannelMask mask = ChannelMask::none(); ///< Last retained control-byte value
  MaskProvenance provenance =
      MaskProvenance::UNKNOWN; ///< Evidence supporting mask

  /// Test whether the retained byte has successful hardware evidence.
  /// @return true for a completed write or observed readback.
  constexpr bool known() const { return provenance != MaskProvenance::UNKNOWN; }

  /// Test whether the retained byte came from an actual readback.
  /// @return true only for READBACK_OBSERVED provenance.
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

/// Return the stable allocation-free display name for a driver state.
/// @param state Driver state to describe.
/// @return Static-lifetime symbolic name, or "UNKNOWN" for an invalid cast.
constexpr const char* driverStateName(DriverState state) {
  switch (state) {
    case DriverState::UNINIT: return "UNINIT";
    case DriverState::READY: return "READY";
    case DriverState::DEGRADED: return "DEGRADED";
    case DriverState::OFFLINE: return "OFFLINE";
  }
  return "UNKNOWN";
}

/// Compatibility overload for driverStateName().
/// @param state Driver state to describe.
/// @return Static-lifetime symbolic name.
constexpr const char* toString(DriverState state) {
  return driverStateName(state);
}

/// Snapshot of current driver settings/state without performing I2C.
struct SettingsSnapshot {
  bool bound = false;                        ///< Valid Config is bound
  bool initialized = false;                  ///< Initial/tracked I2C succeeded
  DriverState state = DriverState::UNINIT;   ///< Passive health state
  /// Configuration fields below are meaningful only while bound is true.
  uint8_t i2cAddress = cmd::DEFAULT_ADDRESS; ///< Bound 7-bit device address
  uint32_t i2cTimeoutMs = 0;                 ///< Bound I2C timeout
  uint32_t resetTimeoutMs = 0;               ///< Bound RESET callback timeout
  uint8_t offlineThreshold = 0;              ///< Bound passive threshold
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
  /// Construct an unbound driver with zeroed binding-local diagnostics.
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
  /// @param config Valid callback, timeout, address, and health configuration.
  /// @return Presence-read result, or a validation/lifecycle error without I2C.
  Status begin(const Config& config);

  /// No-op; the device has no pending I/O or state machine.
  /// @param nowMs Ignored compatibility timestamp.
  void tick(uint32_t nowMs);

  /// Unbind without bus I/O. Explicitly call and check disableAll() first when
  /// safe-off is required.
  void end();

  /// Perform one raw diagnostic control-byte read without health accounting.
  /// A successful read stores READBACK_OBSERVED provenance. Exact transport
  /// errors are returned unchanged; the part has no identity register.
  /// @return Read result, or NOT_INITIALIZED when no Config is bound.
  Status probe();

  /// Make one explicit safe-off recovery attempt by writing 0x00.
  /// Performs one transfer, never asserts RESET or restores a previous mask.
  /// @return Write result, or NOT_INITIALIZED when no Config is bound.
  Status recover();

  /// Invoke RESET once with resetTimeoutMs and, on callback success, perform one
  /// verification read with i2cTimeoutMs. Success requires exactly 0x00; no
  /// prior mask is restored. A mismatch returns RESET_STATE_MISMATCH with the
  /// observed byte in detail and retains it as READBACK_OBSERVED.
  /// The callback may return only OK, TIMEOUT, or RESET_ERROR. TIMEOUT and
  /// RESET_ERROR are preserved exactly; another code returns INVALID_CONFIG
  /// with the invalid callback code in detail and performs no verification I2C.
  /// @return Terminal RESET callback or verification-read result.
  Status hardReset();

  /// Select one channel, disabling every other channel (one write).
  /// @param channel Channel to encode as a one-hot control byte.
  /// @return Write result; invalid cast values return INVALID_PARAM without I2C.
  Status selectChannel(Channel channel);

  /// Apply an arbitrary channel mask (one write).
  /// @param mask Complete control-byte value to apply.
  /// @return Write result, or NOT_INITIALIZED when no Config is bound.
  Status writeChannelMask(ChannelMask mask);

  /// Disable every downstream channel (one write of 0x00).
  /// @return Write result, or NOT_INITIALIZED when no Config is bound.
  Status disableAll();

  /// Observe the applied channel mask (one read-only transaction).
  /// @param mask Output assigned only after a successful read.
  /// @return Read result, or NOT_INITIALIZED when no Config is bound.
  Status readChannelMask(ChannelMask& mask);

  /// Return the passive tracked-health state.
  /// @return Current four-state health classification.
  DriverState state() const { return _driverState; }

  /// Compatibility alias for state().
  /// @return Current four-state health classification.
  DriverState driverState() const { return state(); }

  /// True after a valid Config has been bound, even if begin() presence failed.
  /// @return true between successful configuration validation and end().
  bool isBound() const { return _bound; }

  /// True after the initial presence read or a tracked operation succeeds.
  /// A diagnostic probe() deliberately does not change health/lifecycle state.
  /// @return true after tracked I2C has succeeded in the current binding.
  bool isInitialized() const { return _initialized; }

  /// Passive tracked-health shorthand; performs no probe and never controls
  /// admission. A raw diagnostic probe() does not change this value.
  /// @return true when initialized and passive state is not OFFLINE.
  bool isOnline() const {
    return _initialized && _driverState != DriverState::OFFLINE;
  }

  /// Return the copied bound configuration. When unbound, this is the neutral
  /// default Config installed by end(); inspect isBound() before using it.
  /// @return Borrowed reference valid for the lifetime of this driver object.
  const Config& getConfig() const { return _config; }

  /// Copy a bus-silent snapshot of configuration and driver state.
  /// Configuration fields are meaningful only when SettingsSnapshot::bound is
  /// true; the output is always assigned.
  /// @param out Destination snapshot.
  /// @return Always OK; this method performs no I2C.
  Status getSettings(SettingsSnapshot& out) const;

  /// Timestamp of the most recent successful tracked transport operation.
  /// @return Monotonic callback value, or zero when unavailable/not observed.
  uint32_t lastOkMs() const { return _lastOkMs; }

  /// Timestamp of the most recent failed tracked transport operation.
  /// @return Monotonic callback value, or zero when unavailable/not observed.
  uint32_t lastErrorMs() const { return _lastErrorMs; }

  /// Most recent failed tracked transport result mapped to public Status.
  /// @return Last tracked error, or OK when none exists in this binding.
  Status lastError() const { return _lastError; }

  /// Return failures since the last tracked success.
  /// @return Saturating consecutive-failure count for the current binding.
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }

  /// Return failed tracked transport operations over this object's lifetime.
  /// @return Saturating object-lifetime failure count; end() does not reset it.
  uint32_t totalFailures() const { return _totalFailures; }

  /// Return successful tracked transport operations over this object's lifetime.
  /// @return Saturating object-lifetime success count; end() does not reset it.
  uint32_t totalSuccess() const { return _totalSuccess; }

  /// Return the bus-silent cached mask and evidence provenance.
  /// @return Current channel-mask observation.
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
  void _resetBindingState();

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
