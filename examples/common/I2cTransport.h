/// @file I2cTransport.h
/// @brief Wire-based I2C transport adapter for examples
/// @note NOT part of the library - examples only
#pragma once

#include <Wire.h>
#include "TCA9548A/Config.h"

namespace transport {

using TCA9548A::TransportErr;
using TCA9548A::TransportStatus;

/// Match the driver's Config timeout range; every accepted value fits Wire's
/// uint16_t setting without truncation.
static constexpr uint32_t MAX_WIRE_TIMEOUT_MS = 60000;

/// Map a TwoWire::endTransmission() result to a narrow transport outcome.
///
/// Arduino-ESP32 3.x collapses every NACK into 2 and never returns 3, so a
/// result of 2 cannot identify the failed phase. Preserve it as OTHER with
/// detail 2. Cores that expose the distinct data-NACK result 3 map exactly.
inline TransportStatus mapWireResult(uint8_t result) {
  switch (result) {
    case 0: return TransportStatus::Ok();
    case 2: return TransportStatus::Error(TransportErr::OTHER, result);
    case 3: return TransportStatus::Error(TransportErr::NACK_DATA, result);
    case 5: return TransportStatus::Error(TransportErr::TIMEOUT, result);
    default: return TransportStatus::Error(TransportErr::OTHER, result);
  }
}

/// Initialize Wire for examples
/// @param sda SDA pin
/// @param scl SCL pin
/// @param freqHz I2C clock frequency (1..400000 Hz)
/// @param timeoutMs Wire timeout in milliseconds (1..60000)
/// @return true if initialized
inline bool initWire(int sda, int scl, uint32_t freqHz, uint32_t timeoutMs) {
  if (freqHz == 0 || freqHz > TCA9548A::cmd::I2C_FAST_MODE_HZ ||
      timeoutMs == 0 || timeoutMs > MAX_WIRE_TIMEOUT_MS) {
    return false;
  }
  if (!Wire.begin(sda, scl, freqHz)) {
    return false;
  }
  Wire.setTimeOut(static_cast<uint16_t>(timeoutMs));
  return true;
}

/// I2C write callback using Wire library
/// @param addr I2C device address (7-bit)
/// @param data Data buffer to write
/// @param len Exactly one control byte
/// @param timeoutMs Timeout requested by the driver (1..60000 ms)
/// @param user User context (expects TwoWire*)
/// @return Narrow transport result; success includes the terminating STOP
inline TransportStatus wireWrite(uint8_t addr, const uint8_t* data, size_t len,
                                 uint32_t timeoutMs, void* user) {
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return TransportStatus::Error(TransportErr::OTHER, -1);
  }
  if (data == nullptr || len != TCA9548A::cmd::CONTROL_REG_LEN) {
    return TransportStatus::Error(TransportErr::OTHER, -2);
  }
  if (!TCA9548A::cmd::isValidAddress(addr) || timeoutMs == 0 ||
      timeoutMs > MAX_WIRE_TIMEOUT_MS) {
    return TransportStatus::Error(TransportErr::OTHER, -4);
  }

  wire->setTimeOut(static_cast<uint16_t>(timeoutMs));
  wire->beginTransmission(addr);
  const size_t written = wire->write(data, len);
  // Complete/release the Wire transaction even if buffering failed.
  const uint8_t result = wire->endTransmission(true);

  if (result != 0U) {
    return mapWireResult(result);
  }
  if (written != len) {
    return TransportStatus::Error(TransportErr::OTHER,
                                  static_cast<int32_t>(written));
  }

  return TransportStatus::Ok();
}

/// TCA9548A read-only callback using Wire. Accepts only one receive byte and
/// no transmit phase, so one driver call never becomes two timed transfers.
/// @param addr I2C device address (7-bit)
/// @param txData Must be nullptr (read-only)
/// @param txLen Must be zero (read-only)
/// @param rxData Buffer for read data
/// @param rxLen Exactly one control byte
/// @param timeoutMs Timeout requested by the driver (1..60000 ms)
/// @param user User context (expects TwoWire*)
/// @return Narrow transport result; success includes the terminating STOP
inline TransportStatus wireWriteRead(uint8_t addr, const uint8_t* txData,
                                     size_t txLen, uint8_t* rxData,
                                     size_t rxLen, uint32_t timeoutMs,
                                     void* user) {
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return TransportStatus::Error(TransportErr::OTHER, -1);
  }
  if (txData != nullptr || txLen != 0) {
    return TransportStatus::Error(TransportErr::OTHER, -2);
  }
  if (rxData == nullptr || rxLen != TCA9548A::cmd::CONTROL_REG_LEN) {
    return TransportStatus::Error(TransportErr::OTHER, -3);
  }
  if (!TCA9548A::cmd::isValidAddress(addr) || timeoutMs == 0 ||
      timeoutMs > MAX_WIRE_TIMEOUT_MS) {
    return TransportStatus::Error(TransportErr::OTHER, -4);
  }

  wire->setTimeOut(static_cast<uint16_t>(timeoutMs));
  const size_t received = wire->requestFrom(addr, rxLen, true);
  if (received != rxLen) {
    // TwoWire exposes only the received length here, not the underlying cause.
    // Preserve that uncertainty as OTHER instead of inventing NACK/TIMEOUT/BUS.
    return TransportStatus::Error(TransportErr::OTHER,
                                  static_cast<int32_t>(received));
  }
  if (wire->available() != 1) {
    return TransportStatus::Error(TransportErr::OTHER, -5);
  }
  const int value = wire->read();
  if (value < 0 || value > 0xFF) {
    return TransportStatus::Error(TransportErr::OTHER, -6);
  }
  rxData[0] = static_cast<uint8_t>(value);

  return TransportStatus::Ok();
}

}  // namespace transport
