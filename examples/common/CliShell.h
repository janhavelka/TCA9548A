#pragma once

#include <Arduino.h>

namespace cli_shell {
static constexpr size_t MAX_COMMAND_LENGTH = 96;

inline bool readLine(String& outLine) {
  static char buffer[MAX_COMMAND_LENGTH + 1];
  static size_t length = 0;
  static bool discarding = false;

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (discarding) {
        discarding = false;
        length = 0;
        buffer[0] = '\0';
        continue;
      }
      if (length == 0) {
        continue;
      }
      outLine = buffer;
      length = 0;
      buffer[0] = '\0';
      outLine.trim();
      if (outLine.length() == 0) {
        continue;
      }
      return true;
    }

    if (discarding) {
      continue;
    }
    if (length >= MAX_COMMAND_LENGTH) {
      discarding = true;
      length = 0;
      buffer[0] = '\0';
      continue;
    }
    buffer[length++] = c;
    buffer[length] = '\0';
  }
  return false;
}
}
