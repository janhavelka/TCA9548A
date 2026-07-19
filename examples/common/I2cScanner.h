/// @file I2cScanner.h
/// @brief Simple I2C bus scanner for debugging
/// @note NOT part of the library - examples only
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "Log.h"

namespace i2c {

/// Scan I2C bus and print found devices.
///
/// This is an explicit maintenance diagnostic: it performs exactly 126
/// address probes, never retries, and yields after each completed probe.
/// @return Number of devices found
inline int scan() {
  LOGI("Scanning I2C bus...");

  int count = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    const uint8_t error = Wire.endTransmission(true);

    if (error == 0) {
      Serial.printf("  Found device at 0x%02X\n", addr);
      count++;
    }
    yield();
  }

  if (count == 0) {
    LOGW("No I2C devices found");
  } else {
    LOGI("Found %d device(s)", count);
  }

  Serial.printf("Scan complete: devices=%d\n", count);

  return count;
}

} // namespace i2c
