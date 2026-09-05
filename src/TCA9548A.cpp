/**
 * @file TCA9548A.cpp
 * @brief TCA9548A 8-channel I2C switch driver implementation.
 */

#include "TCA9548A/TCA9548A.h"

#include <limits>

namespace TCA9548A {
namespace {

/// Sanity bound for caller-supplied callback timeouts; not datasheet-derived.
constexpr uint32_t MAX_CALLBACK_TIMEOUT_MS = 60000;

uint32_t configNowMs(const Config& config) {
  return config.nowMs != nullptr ? config.nowMs(config.timeUser) : 0;
}

Status mapTransportStatus(const TransportStatus& transport) {
  switch (transport.code) {
    case TransportErr::OK:
      return Status::Ok();
    case TransportErr::NACK_ADDR:
      return Status::Error(Err::I2C_NACK_ADDR, "I2C address NACK",
                           transport.detail);
    case TransportErr::NACK_DATA:
      return Status::Error(Err::I2C_NACK_DATA, "I2C data NACK",
                           transport.detail);
    case TransportErr::TIMEOUT:
      return Status::Error(Err::I2C_TIMEOUT, "I2C transaction timeout",
                           transport.detail);
    case TransportErr::BUS:
      return Status::Error(Err::I2C_BUS, "I2C bus error", transport.detail);
    case TransportErr::OTHER:
    default:
      return Status::Error(Err::I2C_ERROR, "I2C transport error",
                           transport.detail);
  }
}

} // namespace

Status TCA9548A::begin(const Config& config) {
  if (_bound) {
    return Status::Error(Err::BUSY, "Driver already bound; call end()");
  }
  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  if (config.i2cTimeoutMs == 0 ||
      config.i2cTimeoutMs > MAX_CALLBACK_TIMEOUT_MS) {
    return Status::Error(Err::INVALID_CONFIG,
                         "I2C timeout must be 1-60000 ms");
  }
  if (config.resetTimeoutMs == 0 ||
      config.resetTimeoutMs > MAX_CALLBACK_TIMEOUT_MS) {
    return Status::Error(Err::INVALID_CONFIG,
                         "RESET timeout must be 1-60000 ms");
  }
  if (!cmd::isValidAddress(config.i2cAddress)) {
    return Status::Error(Err::INVALID_CONFIG,
                         "I2C address must be 0x70-0x77");
  }
  if (config.offlineThreshold == 0) {
    return Status::Error(Err::INVALID_CONFIG,
                         "Offline threshold must be greater than zero");
  }

  _resetBindingState();
  _config = config;
  _bound = true;

  ChannelMask observed;
  return _readControlByte(observed, true);
}

void TCA9548A::tick(uint32_t nowMs) {
  (void)nowMs;
}

void TCA9548A::end() {
  _resetBindingState();
}

Status TCA9548A::probe() {
  const Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  ChannelMask observed;
  return _readControlByte(observed, false);
}

Status TCA9548A::recover() {
  return disableAll();
}

Status TCA9548A::hardReset() {
  const Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  if (_config.hardReset == nullptr) {
    return Status::Error(Err::UNSUPPORTED,
                         "Hard-reset callback not configured");
  }

  invalidateChannelMask();
  const Status resetStatus =
      _config.hardReset(_config.resetTimeoutMs, _config.resetUser);
  if (!resetStatus.ok() && !resetStatus.is(Err::TIMEOUT) &&
      !resetStatus.is(Err::RESET_ERROR)) {
    return Status::Error(Err::INVALID_CONFIG,
                         "Hard-reset callback returned invalid Status code",
                         static_cast<int32_t>(resetStatus.code));
  }
  if (!resetStatus.ok()) {
    return resetStatus;
  }

  ChannelMask observed;
  const Status readStatus = _readControlByte(observed, true);
  if (!readStatus.ok()) {
    return readStatus;
  }
  if (!observed.isNone()) {
    return Status::Error(Err::RESET_STATE_MISMATCH,
                         "RESET control byte was not 0x00", observed.raw());
  }
  return Status::Ok();
}

Status TCA9548A::selectChannel(Channel channel) {
  const Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  const ChannelMask mask = ChannelMask::one(channel);
  if (!mask.isOneHot()) {
    return Status::Error(Err::INVALID_PARAM, "Channel must be CH0-CH7");
  }
  return _writeControlByte(mask);
}

Status TCA9548A::writeChannelMask(ChannelMask mask) {
  const Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  return _writeControlByte(mask);
}

Status TCA9548A::disableAll() {
  return writeChannelMask(ChannelMask::none());
}

Status TCA9548A::readChannelMask(ChannelMask& mask) {
  const Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  return _readControlByte(mask, true);
}

Status TCA9548A::getSettings(SettingsSnapshot& out) const {
  out.bound = _bound;
  out.initialized = _initialized;
  out.state = _driverState;
  out.i2cAddress = _config.i2cAddress;
  out.i2cTimeoutMs = _config.i2cTimeoutMs;
  out.resetTimeoutMs = _config.resetTimeoutMs;
  out.offlineThreshold = _config.offlineThreshold;
  out.hasNowMsHook = _config.nowMs != nullptr;
  out.hasHardReset = _config.hardReset != nullptr;
  out.maskObservation = _maskObservation;
  return Status::Ok();
}

void TCA9548A::invalidateChannelMask() {
  _maskObservation.provenance = MaskProvenance::UNKNOWN;
}

Status TCA9548A::_requireBound() const {
  if (!_bound) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  return Status::Ok();
}

Status TCA9548A::_writeControlByte(ChannelMask mask) {
  const uint8_t byte = mask.raw();
  const Status status = mapTransportStatus(_config.i2cWrite(
      _config.i2cAddress, &byte, sizeof(byte), _config.i2cTimeoutMs,
      _config.i2cUser));
  _updateHealth(status);
  if (!status.ok()) {
    invalidateChannelMask();
    return status;
  }
  _maskObservation =
      ChannelMaskObservation{mask, MaskProvenance::WRITE_COMPLETED};
  return status;
}

Status TCA9548A::_readControlByte(ChannelMask& mask, bool tracked) {
  uint8_t byte = cmd::NO_CHANNELS;
  const Status status = mapTransportStatus(_config.i2cWriteRead(
      _config.i2cAddress, nullptr, 0, &byte, sizeof(byte),
      _config.i2cTimeoutMs, _config.i2cUser));
  if (tracked) {
    _updateHealth(status);
  }
  if (!status.ok()) {
    invalidateChannelMask();
    return status;
  }
  mask = ChannelMask::fromRaw(byte);
  _maskObservation =
      ChannelMaskObservation{mask, MaskProvenance::READBACK_OBSERVED};
  return status;
}

void TCA9548A::_updateHealth(const Status& status) {
  const uint32_t now = configNowMs(_config);
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();
  const uint8_t maxU8 = std::numeric_limits<uint8_t>::max();

  if (status.ok()) {
    _lastOkMs = now;
    if (_totalSuccess < maxU32) {
      ++_totalSuccess;
    }
    _consecutiveFailures = 0;
    _initialized = true;
    _driverState = DriverState::READY;
    return;
  }

  _lastError = status;
  _lastErrorMs = now;
  if (_totalFailures < maxU32) {
    ++_totalFailures;
  }
  if (_consecutiveFailures < maxU8) {
    ++_consecutiveFailures;
  }

  // Failures before the first tracked success keep UNINIT; the counters still
  // count. A later successful primitive promotes the binding to READY.
  if (!_initialized) {
    return;
  }

  _driverState = _consecutiveFailures >= _config.offlineThreshold
                     ? DriverState::OFFLINE
                     : DriverState::DEGRADED;
}

void TCA9548A::_resetBindingState() {
  _config = Config{};
  _bound = false;
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _maskObservation = ChannelMaskObservation{};
}

} // namespace TCA9548A
