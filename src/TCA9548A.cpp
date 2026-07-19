/**
 * @file TCA9548A.cpp
 * @brief TCA9548A 8-channel I2C switch driver implementation.
 */

#include "TCA9548A/TCA9548A.h"

#include <limits>

namespace TCA9548A {
namespace {

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

  _resetState();
  _config = config;
  _bound = true;

  ChannelMask observed = ChannelMask::none();
  Status status = _readControlByte(observed);
  if (!status.ok()) {
    return status;
  }
  return Status::Ok();
}

void TCA9548A::tick(uint32_t nowMs) {
  (void)nowMs;
}

void TCA9548A::end() {
  _resetState();
}

Status TCA9548A::probe() {
  Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }

  ChannelMask observed = ChannelMask::none();
  return _readControlByteRaw(observed);
}

Status TCA9548A::recover() {
  Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  return _writeControlByte(ChannelMask::none());
}

Status TCA9548A::hardReset() {
  Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  if (_config.hardReset == nullptr) {
    return Status::Error(Err::UNSUPPORTED,
                         "Hard-reset callback not configured");
  }

  invalidateChannelMask();
  Status resetStatus =
      _config.hardReset(_config.resetTimeoutMs, _config.resetUser);
  if (resetStatus.inProgress()) {
    return Status::Error(Err::INVALID_CONFIG,
                         "Hard-reset callback must return a terminal Status");
  }
  if (!resetStatus.ok()) {
    return resetStatus;
  }

  ChannelMask observed = ChannelMask::none();
  Status readStatus = _readControlByte(observed);
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
  Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  if (static_cast<uint8_t>(channel) >= cmd::NUM_CHANNELS) {
    return Status::Error(Err::INVALID_PARAM, "Channel must be CH0-CH7");
  }
  return _writeControlByte(ChannelMask::one(channel));
}

Status TCA9548A::writeChannelMask(ChannelMask mask) {
  Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  return _writeControlByte(mask);
}

Status TCA9548A::disableAll() {
  return writeChannelMask(ChannelMask::none());
}

Status TCA9548A::readChannelMask(ChannelMask& mask) {
  Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }

  ChannelMask observed = ChannelMask::none();
  Status status = _readControlByte(observed);
  if (status.ok()) {
    mask = observed;
  }
  return status;
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

Status TCA9548A::_i2cWriteRaw(const uint8_t* buf, size_t len) {
  if (_config.i2cWrite == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write callback not set");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C write buffer");
  }

  return mapTransportStatus(_config.i2cWrite(
      _config.i2cAddress, buf, len, _config.i2cTimeoutMs, _config.i2cUser));
}

Status TCA9548A::_i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                                  uint8_t* rxBuf, size_t rxLen) {
  if (_config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG,
                         "I2C write-read callback not set");
  }
  if (txLen > 0 && txBuf == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C transmit buffer");
  }
  if (rxLen > 0 && rxBuf == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C receive buffer");
  }

  return mapTransportStatus(_config.i2cWriteRead(
      _config.i2cAddress, txBuf, txLen, rxBuf, rxLen,
      _config.i2cTimeoutMs, _config.i2cUser));
}

Status TCA9548A::_i2cWriteTracked(const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C write buffer");
  }

  Status status = _i2cWriteRaw(buf, len);
  if (status.is(Err::INVALID_CONFIG) || status.is(Err::INVALID_PARAM)) {
    return status;
  }
  return _updateHealth(status);
}

Status TCA9548A::_i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                                      uint8_t* rxBuf, size_t rxLen) {
  if (txLen > 0 && txBuf == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C transmit buffer");
  }
  if (rxLen > 0 && rxBuf == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C receive buffer");
  }

  Status status = _i2cWriteReadRaw(txBuf, txLen, rxBuf, rxLen);
  if (status.is(Err::INVALID_CONFIG) || status.is(Err::INVALID_PARAM)) {
    return status;
  }
  return _updateHealth(status);
}

Status TCA9548A::_updateHealth(const Status& status) {
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
    return status;
  }

  _lastError = status;
  _lastErrorMs = now;
  if (_totalFailures < maxU32) {
    ++_totalFailures;
  }
  if (_consecutiveFailures < maxU8) {
    ++_consecutiveFailures;
  }

  // A failed presence read cannot move an as-yet-uninitialized binding into a
  // health state. A later successful primitive promotes it to READY.
  if (!_initialized) {
    return status;
  }

  _driverState = _consecutiveFailures >= _config.offlineThreshold
                     ? DriverState::OFFLINE
                     : DriverState::DEGRADED;
  return status;
}

Status TCA9548A::_writeControlByte(ChannelMask mask) {
  const uint8_t byte = mask.raw();
  Status status = _i2cWriteTracked(&byte, sizeof(byte));
  if (status.ok()) {
    _recordMask(mask, MaskProvenance::WRITE_COMPLETED);
  } else {
    invalidateChannelMask();
  }
  return status;
}

Status TCA9548A::_readControlByte(ChannelMask& mask) {
  uint8_t byte = cmd::NO_CHANNELS;
  Status status = _i2cWriteReadTracked(nullptr, 0, &byte, sizeof(byte));
  if (status.ok()) {
    mask = ChannelMask::fromRaw(byte);
    _recordMask(mask, MaskProvenance::READBACK_OBSERVED);
  } else {
    invalidateChannelMask();
  }
  return status;
}

Status TCA9548A::_readControlByteRaw(ChannelMask& mask) {
  uint8_t byte = cmd::NO_CHANNELS;
  Status status = _i2cWriteReadRaw(nullptr, 0, &byte, sizeof(byte));
  if (status.ok()) {
    mask = ChannelMask::fromRaw(byte);
    _recordMask(mask, MaskProvenance::READBACK_OBSERVED);
  } else {
    invalidateChannelMask();
  }
  return status;
}

void TCA9548A::_recordMask(ChannelMask mask, MaskProvenance provenance) {
  _maskObservation.mask = mask;
  _maskObservation.provenance = provenance;
}

void TCA9548A::_resetState() {
  _config = Config{};
  _bound = false;
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _maskObservation = ChannelMaskObservation{};
}

} // namespace TCA9548A
