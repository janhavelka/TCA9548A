/// @file I2cTransport.h
/// @brief Wire-based I2C transport adapter for examples
/// @note NOT part of the library - examples only
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "TCA9548A/Config.h"

namespace transport {

using TCA9548A::TransportErr;
using TCA9548A::TransportStatus;

inline TransportStatus mapWireResult(uint8_t result) {
  switch (result) {
    case 0: return TransportStatus::Ok();
    case 2: return TransportStatus::Error(TransportErr::NACK_ADDR, result);
    case 3: return TransportStatus::Error(TransportErr::NACK_DATA, result);
    case 4: return TransportStatus::Error(TransportErr::OTHER, result);
    case 5: return TransportStatus::Error(TransportErr::TIMEOUT, result);
    default: return TransportStatus::Error(TransportErr::OTHER, result);
  }
}

/// Initialize Wire for examples
/// @param sda SDA pin
/// @param scl SCL pin
/// @param freqHz I2C clock frequency
/// @param timeoutMs Wire timeout in milliseconds
/// @return true if initialized
inline bool initWire(int sda, int scl, uint32_t freqHz, uint32_t timeoutMs) {
  if (!Wire.begin(sda, scl)) {
    return false;
  }
  Wire.setClock(freqHz);
  Wire.setTimeOut(timeoutMs);
  return true;
}

/// I2C write callback using Wire library
/// @param addr I2C device address (7-bit)
/// @param data Data buffer to write
/// @param len Number of bytes to write
/// @param timeoutMs Timeout requested by the driver
/// @param user User context (expects TwoWire*)
/// @return Narrow transport result; success includes the terminating STOP
inline TransportStatus wireWrite(uint8_t addr, const uint8_t* data, size_t len,
                                 uint32_t timeoutMs, void* user) {
  (void)timeoutMs;
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return TransportStatus::Error(TransportErr::OTHER, -1);
  }
  if (data == nullptr || len == 0) {
    return TransportStatus::Error(TransportErr::OTHER, -2);
  }

  wire->beginTransmission(addr);
  size_t written = wire->write(data, len);
  uint8_t result = wire->endTransmission(true);

  if (result != 0U) {
    return mapWireResult(result);
  }
  if (written != len) {
    return TransportStatus::Error(TransportErr::OTHER,
                                  static_cast<int32_t>(written));
  }

  return TransportStatus::Ok();
}

/// I2C write-read callback using Wire library
/// @param addr I2C device address (7-bit)
/// @param txData Data to write (nullptr for read-only)
/// @param txLen Number of bytes to write (0 for read-only)
/// @param rxData Buffer for read data
/// @param rxLen Number of bytes to read
/// @param timeoutMs Timeout requested by the driver
/// @param user User context (expects TwoWire*)
/// @return Narrow transport result; success includes the terminating STOP
inline TransportStatus wireWriteRead(uint8_t addr, const uint8_t* txData,
                                     size_t txLen, uint8_t* rxData,
                                     size_t rxLen, uint32_t timeoutMs,
                                     void* user) {
  (void)timeoutMs;
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return TransportStatus::Error(TransportErr::OTHER, -1);
  }
  if (txLen > 0 && txData == nullptr) {
    return TransportStatus::Error(TransportErr::OTHER, -2);
  }
  if (rxLen > 0 && rxData == nullptr) {
    return TransportStatus::Error(TransportErr::OTHER, -3);
  }

  // TCA9548A has no register-pointer phase. For tx+rx flows we intentionally
  // end the write with STOP, then perform a fresh read transaction.
  if (txLen > 0) {
    wire->beginTransmission(addr);
    size_t written = wire->write(txData, txLen);
    uint8_t result = wire->endTransmission(true);
    if (result != 0U) {
      return mapWireResult(result);
    }
    if (written != txLen) {
      return TransportStatus::Error(TransportErr::OTHER,
                                    static_cast<int32_t>(written));
    }
  }

  if (rxLen == 0) {
    return TransportStatus::Ok();
  }

  // Read phase
  size_t received = wire->requestFrom(addr, rxLen);
  if (received != rxLen) {
    // TwoWire exposes only the received length here, not the underlying cause.
    // Preserve that uncertainty as OTHER instead of inventing NACK/TIMEOUT/BUS.
    if (received == 0) {
      return TransportStatus::Error(TransportErr::OTHER, 0);
    }
    for (size_t i = 0; i < received; i++) {
      (void)wire->read();
    }
    return TransportStatus::Error(TransportErr::OTHER,
                                  static_cast<int32_t>(received));
  }

  for (size_t i = 0; i < rxLen; i++) {
    if (wire->available() <= 0) {
      return TransportStatus::Error(TransportErr::OTHER,
                                    static_cast<int32_t>(i));
    }
    const int value = wire->read();
    if (value < 0) {
      return TransportStatus::Error(TransportErr::OTHER,
                                    static_cast<int32_t>(i));
    }
    rxData[i] = static_cast<uint8_t>(value);
  }

  return TransportStatus::Ok();
}

}  // namespace transport
