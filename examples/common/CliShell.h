/**
 * @file CliShell.h
 * @brief Fixed-buffer serial command input for the example CLI.
 * @note Example-only helper; not part of the library API.
 */

#pragma once

#include <Arduino.h>

#include "examples/common/CliLineBuffer.h"
#include "examples/common/Log.h"

namespace cli_shell {

inline LineResult pollLine(char* output, size_t capacity) {
  static FixedLineBuffer lineBuffer;

  if (output == nullptr || capacity == 0U) {
    return LineResult::OUTPUT_TOO_SMALL;
  }

  for (size_t processed = 0;
       processed < FixedLineBuffer::CAPACITY && LOG_SERIAL.available() > 0;
       ++processed) {
    const int input = LOG_SERIAL.read();
    if (input < 0) {
      break;
    }

    const LineResult result =
        lineBuffer.push(static_cast<char>(input), output, capacity);
    if (result == LineResult::READY) {
      return result;
    }
    if (result == LineResult::TOO_LONG) {
      LOGW("Command discarded: line exceeds %u bytes",
           static_cast<unsigned>(FixedLineBuffer::CAPACITY - 1U));
      return result;
    } else if (result == LineResult::OUTPUT_TOO_SMALL) {
      LOGW("Command discarded: destination buffer is too small");
      return result;
    }
  }

  return LineResult::NONE;
}

}  // namespace cli_shell
