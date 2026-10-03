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
/// This is an explicit maintenance diagnostic: it performs exactly 112
/// address probes, never retries, and yields after each completed probe.
/// @return Number of devices found
inline int scan() {
  // Printed unconditionally: the HIL runner matches this line, so it must not
  // depend on LOG_LEVEL.
  LOG_SERIAL.println(F("Scanning I2C bus (112 bounded probes)..."));

  int count = 0;
  unsigned errors = 0U;
  // Exclude the reserved 0x00..0x07 and 0x78..0x7F address ranges.
  for (uint8_t addr = 0x08U; addr <= 0x77U; ++addr) {
    Wire.beginTransmission(addr);
    const uint8_t error = Wire.endTransmission(true);

    if (error == 0) {
      LOG_SERIAL.printf("  Found device at 0x%02X\n", addr);
      count++;
    } else if (error != 2U) {
      // Address NACK is normal absence. A timeout/controller failure cannot
      // prove absence and must not pass a routing-isolation check.
      ++errors;
      LOG_SERIAL.printf("  Scan error at 0x%02X: %u\n", addr,
                        static_cast<unsigned>(error));
    }
    yield();
  }

  if (count == 0) {
    LOGW("No I2C devices found");
  } else {
    LOGI("Found %d device(s)", count);
  }

  LOG_SERIAL.printf("Scan complete: devices=%d errors=%u\n", count, errors);

  return count;
}

} // namespace i2c
