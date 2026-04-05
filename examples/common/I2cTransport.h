/// @file I2cTransport.h
/// @brief Wire-based I2C transport adapter for examples
/// @note NOT part of the library - examples only
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "TCA9548A/Status.h"

namespace transport {

using TCA9548A::Status;
using TCA9548A::Err;

/// Initialize Wire for examples
/// @param sda SDA pin
/// @param scl SCL pin
/// @param freqHz I2C clock frequency
/// @param timeoutMs Wire timeout in milliseconds
/// @return true if initialized
inline bool initWire(int sda, int scl, uint32_t freqHz, uint32_t timeoutMs) {
  Wire.begin(sda, scl);
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
/// @return Status indicating success or failure
inline Status wireWrite(uint8_t addr, const uint8_t* data, size_t len,
                        uint32_t timeoutMs, void* user) {
  (void)timeoutMs;
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Wire instance is null");
  }
  if (data == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid write buffer");
  }

  wire->beginTransmission(addr);
  size_t written = wire->write(data, len);
  uint8_t result = wire->endTransmission(true);

  if (result != 0) {
    switch (result) {
      case 1: return Status::Error(Err::I2C_ERROR, "I2C write too long", result);
      case 2: return Status::Error(Err::I2C_NACK_ADDR, "I2C NACK addr", result);
      case 3: return Status::Error(Err::I2C_NACK_DATA, "I2C NACK data", result);
      case 4: return Status::Error(Err::I2C_BUS, "I2C bus error", result);
      case 5: return Status::Error(Err::I2C_TIMEOUT, "I2C timeout", result);
      default: return Status::Error(Err::I2C_ERROR, "I2C write failed", result);
    }
  }
  if (written != len) {
    return Status::Error(Err::I2C_ERROR, "I2C write incomplete", static_cast<int32_t>(written));
  }

  return Status::Ok();
}

/// I2C write-read callback using Wire library
/// @param addr I2C device address (7-bit)
/// @param txData Data to write (nullptr for read-only)
/// @param txLen Number of bytes to write (0 for read-only)
/// @param rxData Buffer for read data
/// @param rxLen Number of bytes to read
/// @param timeoutMs Timeout requested by the driver
/// @param user User context (expects TwoWire*)
/// @return Status indicating success or failure
inline Status wireWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                            uint8_t* rxData, size_t rxLen,
                            uint32_t timeoutMs, void* user) {
  (void)timeoutMs;
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Wire instance is null");
  }
  if (txLen > 0 && txData == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Invalid write buffer");
  }
  if (rxLen > 0 && rxData == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }

  // TCA9548A has no register-pointer phase. For tx+rx flows we intentionally
  // end the write with STOP, then perform a fresh read transaction.
  if (txLen > 0) {
    wire->beginTransmission(addr);
    wire->write(txData, txLen);
    uint8_t result = wire->endTransmission(true);
    if (result != 0) {
      switch (result) {
        case 1: return Status::Error(Err::I2C_ERROR, "I2C write too long", result);
        case 2: return Status::Error(Err::I2C_NACK_ADDR, "I2C NACK addr", result);
        case 3: return Status::Error(Err::I2C_NACK_DATA, "I2C NACK data", result);
        case 4: return Status::Error(Err::I2C_BUS, "I2C bus error", result);
        case 5: return Status::Error(Err::I2C_TIMEOUT, "I2C timeout", result);
        default: return Status::Error(Err::I2C_ERROR, "I2C write failed", result);
      }
    }
  }

  if (rxLen == 0) {
    return Status::Ok();
  }

  // Read phase
  size_t received = wire->requestFrom(addr, rxLen);
  if (received != rxLen) {
    if (received == 0) {
      return Status::Error(Err::I2C_ERROR, "I2C read returned 0 bytes",
                           static_cast<int32_t>(received));
    }
    for (size_t i = 0; i < received; i++) {
      (void)wire->read();
    }
    return Status::Error(Err::I2C_ERROR, "I2C read incomplete",
                         static_cast<int32_t>(received));
  }

  for (size_t i = 0; i < rxLen; i++) {
    rxData[i] = wire->read();
  }

  return Status::Ok();
}

}  // namespace transport
