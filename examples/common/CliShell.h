/**
 * @file CliShell.h
 * @brief Fixed-buffer serial command input for the example CLI.
 * @note Example-only helper; not part of the library API.
 */

#pragma once

#include <Arduino.h>
#include <cstring>

#include "examples/common/Log.h"

namespace cli_shell {

inline bool readLine(char* output, size_t capacity) {
  static char buffer[128];
  static size_t length = 0;
  static bool discarding = false;

  if (output == nullptr || capacity == 0U) {
    return false;
  }

  for (size_t processed = 0;
       processed < sizeof(buffer) && LOG_SERIAL.available() > 0;
       ++processed) {
    const int input = LOG_SERIAL.read();
    if (input < 0) {
      break;
    }

    const char value = static_cast<char>(input);
    if (value == '\r' || value == '\n') {
      if (discarding) {
        discarding = false;
        length = 0;
        LOGW("Command discarded: line exceeds %u bytes",
             static_cast<unsigned>(sizeof(buffer) - 1U));
        continue;
      }
      if (length == 0U) {
        continue;
      }

      buffer[length] = '\0';
      size_t first = 0;
      while (buffer[first] == ' ' || buffer[first] == '\t') {
        ++first;
      }
      while (length > first &&
             (buffer[length - 1U] == ' ' || buffer[length - 1U] == '\t')) {
        --length;
      }

      const size_t commandLength = length - first;
      if (commandLength == 0U) {
        length = 0;
        continue;
      }
      if (commandLength >= capacity) {
        length = 0;
        LOGW("Command discarded: destination buffer is too small");
        continue;
      }

      std::memmove(output, buffer + first, commandLength);
      output[commandLength] = '\0';
      length = 0;
      return true;
    }

    if (discarding) {
      continue;
    }
    if (length >= sizeof(buffer) - 1U) {
      discarding = true;
      length = 0;
      continue;
    }
    buffer[length++] = value;
  }

  return false;
}

}  // namespace cli_shell
