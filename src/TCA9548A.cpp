/**
 * @file TCA9548A.cpp
 * @brief TCA9548A 8-channel I2C switch driver implementation.
 */

#include "TCA9548A/TCA9548A.h"

#include <Arduino.h>
#include <limits>

namespace TCA9548A {
namespace {

static constexpr uint32_t MAX_I2C_TIMEOUT_MS = 60000;
static constexpr uint32_t MAX_RECOVER_BACKOFF_MS = 600000;

static uint32_t configNowMs(const Config& cfg) {
  return (cfg.nowMs != nullptr) ? cfg.nowMs(cfg.timeUser) : millis();
}

static bool isValidAddress(uint8_t addr) {
  return addr >= cmd::I2C_ADDR_MIN && addr <= cmd::I2C_ADDR_MAX;
}

static bool isI2cFailure(Err code) {
  return code == Err::I2C_ERROR || code == Err::I2C_NACK_ADDR ||
         code == Err::I2C_NACK_DATA || code == Err::I2C_TIMEOUT ||
         code == Err::I2C_BUS;
}

static bool timeElapsed(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

} // namespace

// ============================================================================
// Lifecycle
// ============================================================================

Status TCA9548A::begin(const Config& config) {
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _lastRecoverMs = 0;
  _lastRecoverValid = false;
  _lastKnownMask = cmd::NO_CHANNELS;

  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  if (config.i2cTimeoutMs == 0) {
    return Status::Error(Err::INVALID_CONFIG, "I2C timeout must be > 0");
  }
  if (config.i2cTimeoutMs > MAX_I2C_TIMEOUT_MS) {
    return Status::Error(Err::INVALID_CONFIG, "I2C timeout too large");
  }
  if (!isValidAddress(config.i2cAddress)) {
    return Status::Error(Err::INVALID_CONFIG,
                         "Invalid I2C address (must be 0x70-0x77)");
  }
  if (config.recoverBackoffMs > MAX_RECOVER_BACKOFF_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Recover backoff too large");
  }

  _config = config;
  if (_config.offlineThreshold == 0) {
    _config.offlineThreshold = 1;
  }

  uint8_t regValue = cmd::NO_CHANNELS;
  Status st = _readControlRegRaw(regValue);
  if (!st.ok()) {
    if (isI2cFailure(st.code)) {
      return Status::Error(Err::DEVICE_NOT_FOUND, "TCA9548A not found on bus",
                           st.detail);
    }
    return st;
  }

  _lastKnownMask = regValue;
  _initialized = true;
  _driverState = DriverState::READY;
  _lastOkMs = configNowMs(_config);
  return Status::Ok();
}

void TCA9548A::tick(uint32_t nowMs) {
  (void)nowMs;
  // TCA9548A has no pending I/O or state machine work.
}

void TCA9548A::end() {
  if (_initialized) {
    const uint8_t zero = cmd::NO_CHANNELS;
    (void)_i2cWriteRaw(&zero, sizeof(zero));
  }

  _initialized = false;
  _driverState = DriverState::UNINIT;
  _lastKnownMask = cmd::NO_CHANNELS;
}

// ============================================================================
// Diagnostics
// ============================================================================

Status TCA9548A::probe() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t regValue = cmd::NO_CHANNELS;
  Status st = _readControlRegRaw(regValue);
  if (!st.ok() && isI2cFailure(st.code)) {
    return Status::Error(Err::DEVICE_NOT_FOUND, "TCA9548A not found on bus",
                         st.detail);
  }
  return st;
}

Status TCA9548A::recover() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  if (_lastRecoverValid && _config.recoverBackoffMs > 0) {
    const uint32_t now = configNowMs(_config);
    if (!timeElapsed(now, _lastRecoverMs + _config.recoverBackoffMs)) {
      return Status::Error(Err::TIMEOUT, "Recover backoff active");
    }
  }

  const uint8_t desiredMask = _lastKnownMask;
  _lastRecoverMs = configNowMs(_config);
  _lastRecoverValid = true;

  if (_config.recoverUseHardReset && _config.hardReset != nullptr) {
    Status rstSt = hardReset();
    if (rstSt.ok()) {
      if (desiredMask == cmd::NO_CHANNELS) {
        return Status::Ok();
      }

      Status restoreSt = _writeControlReg(desiredMask);
      if (restoreSt.ok()) {
        _lastKnownMask = desiredMask;
      }
      return restoreSt;
    }
  }

  uint8_t regValue = cmd::NO_CHANNELS;
  Status st = _readControlReg(regValue);
  if (!st.ok()) {
    return st;
  }

  if (regValue == desiredMask) {
    _lastKnownMask = regValue;
    return Status::Ok();
  }

  st = _writeControlReg(desiredMask);
  if (st.ok()) {
    _lastKnownMask = desiredMask;
  }
  return st;
}

Status TCA9548A::hardReset() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_config.hardReset == nullptr) {
    return Status::Error(Err::UNSUPPORTED,
                         "hardReset callback not configured");
  }

  Status rstSt = _config.hardReset(_config.i2cUser);
  if (!rstSt.ok()) {
    return rstSt;
  }

  uint8_t regValue = cmd::NO_CHANNELS;
  Status st = _readControlReg(regValue);
  if (st.ok()) {
    _lastKnownMask = regValue;
  }
  return st;
}

Status TCA9548A::getSettings(SettingsSnapshot& out) const {
  out.initialized = _initialized;
  out.state = _driverState;
  out.i2cAddress = _config.i2cAddress;
  out.i2cTimeoutMs = _config.i2cTimeoutMs;
  out.offlineThreshold = _config.offlineThreshold;
  out.recoverBackoffMs = _config.recoverBackoffMs;
  out.hasNowMsHook = (_config.nowMs != nullptr);
  out.hasHardReset = (_config.hardReset != nullptr);
  out.recoverUseHardReset = _config.recoverUseHardReset;
  out.lastKnownMask = _lastKnownMask;
  return Status::Ok();
}

// ============================================================================
// Channel Control
// ============================================================================

Status TCA9548A::selectChannel(uint8_t channel) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (channel >= cmd::NUM_CHANNELS) {
    return Status::Error(Err::INVALID_PARAM, "Channel must be 0-7");
  }

  return _writeControlReg(static_cast<uint8_t>(1U << channel));
}

Status TCA9548A::setChannelMask(uint8_t mask) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  return _writeControlReg(mask);
}

Status TCA9548A::enableChannels(uint8_t mask) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t current = cmd::NO_CHANNELS;
  Status st = _readControlReg(current);
  if (!st.ok()) {
    return st;
  }

  const uint8_t newMask = static_cast<uint8_t>(current | mask);
  if (newMask == current) {
    return Status::Ok();
  }

  return _writeControlReg(newMask);
}

Status TCA9548A::disableChannels(uint8_t mask) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t current = cmd::NO_CHANNELS;
  Status st = _readControlReg(current);
  if (!st.ok()) {
    return st;
  }

  const uint8_t newMask = static_cast<uint8_t>(current & ~mask);
  if (newMask == current) {
    return Status::Ok();
  }

  return _writeControlReg(newMask);
}

Status TCA9548A::disableAll() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  return _writeControlReg(cmd::NO_CHANNELS);
}

Status TCA9548A::readChannelMask(uint8_t& mask) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  Status st = _readControlReg(mask);
  if (st.ok()) {
    _lastKnownMask = mask;
  }
  return st;
}

Status TCA9548A::isChannelEnabled(uint8_t channel, bool& enabled) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (channel >= cmd::NUM_CHANNELS) {
    return Status::Error(Err::INVALID_PARAM, "Channel must be 0-7");
  }

  uint8_t mask = cmd::NO_CHANNELS;
  Status st = _readControlReg(mask);
  if (st.ok()) {
    _lastKnownMask = mask;
    enabled = (mask & (1U << channel)) != 0;
  }
  return st;
}

// ============================================================================
// Transport Wrappers - RAW (no health tracking)
// ============================================================================

Status TCA9548A::_i2cWriteRaw(const uint8_t* buf, size_t len) {
  if (_config.i2cWrite == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write not set");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  return _config.i2cWrite(_config.i2cAddress, buf, len, _config.i2cTimeoutMs,
                          _config.i2cUser);
}

Status TCA9548A::_i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                                  uint8_t* rxBuf, size_t rxLen) {
  if (_config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write-read not set");
  }
  if (txLen > 0 && txBuf == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  if (rxLen > 0 && rxBuf == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  return _config.i2cWriteRead(_config.i2cAddress, txBuf, txLen, rxBuf, rxLen,
                              _config.i2cTimeoutMs, _config.i2cUser);
}

// ============================================================================
// Transport Wrappers - TRACKED (update health)
// ============================================================================

Status TCA9548A::_i2cWriteTracked(const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteRaw(buf, len);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

Status TCA9548A::_i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                                      uint8_t* rxBuf, size_t rxLen) {
  if (txLen > 0 && txBuf == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  if (rxLen > 0 && rxBuf == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteReadRaw(txBuf, txLen, rxBuf, rxLen);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

// ============================================================================
// Health Management
// ============================================================================

Status TCA9548A::_updateHealth(const Status& st) {
  const uint32_t now = configNowMs(_config);
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();
  const uint8_t maxU8 = std::numeric_limits<uint8_t>::max();

  if (!_initialized) {
    if (st.ok()) {
      _lastOkMs = now;
    } else {
      _lastError = st;
      _lastErrorMs = now;
    }
    return st;
  }

  if (st.ok()) {
    _lastOkMs = now;
    if (_totalSuccess < maxU32) {
      _totalSuccess++;
    }
    _consecutiveFailures = 0;
    _driverState = DriverState::READY;
    return st;
  }

  _lastError = st;
  _lastErrorMs = now;
  if (_totalFailures < maxU32) {
    _totalFailures++;
  }
  if (_consecutiveFailures < maxU8) {
    _consecutiveFailures++;
  }

  if (_consecutiveFailures >= _config.offlineThreshold) {
    _driverState = DriverState::OFFLINE;
  } else {
    _driverState = DriverState::DEGRADED;
  }

  return st;
}

// ============================================================================
// Internal Helpers
// ============================================================================

Status TCA9548A::_writeControlReg(uint8_t value) {
  uint8_t buf[cmd::CONTROL_REG_LEN] = {value};
  Status st = _i2cWriteTracked(buf, sizeof(buf));
  if (st.ok()) {
    _lastKnownMask = value;
  }
  return st;
}

Status TCA9548A::_readControlReg(uint8_t& value) {
  uint8_t buf[cmd::CONTROL_REG_LEN] = {cmd::NO_CHANNELS};
  Status st = _i2cWriteReadTracked(nullptr, 0, buf, sizeof(buf));
  if (st.ok()) {
    value = buf[0];
    _lastKnownMask = value;
  }
  return st;
}

Status TCA9548A::_readControlRegRaw(uint8_t& value) {
  uint8_t buf[cmd::CONTROL_REG_LEN] = {cmd::NO_CHANNELS};
  Status st = _i2cWriteReadRaw(nullptr, 0, buf, sizeof(buf));
  if (st.ok()) {
    value = buf[0];
  }
  return st;
}

} // namespace TCA9548A
