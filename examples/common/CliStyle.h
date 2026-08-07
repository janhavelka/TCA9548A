/**
 * @file CliStyle.h
 * @brief Shared ANSI formatting helpers for the example CLIs.
 * @note Example-only helper; not part of the library API.
 */

#pragma once

#include <Arduino.h>

#include "examples/common/Log.h"

namespace cli {

inline void printSection(const char* title) {
  Serial.printf("%s=== %s ===%s\n", LOG_COLOR_CYAN, title,
                LOG_COLOR_RESET);
}

inline void printPrompt() {
  Serial.printf("%s> %s", LOG_COLOR_CYAN, LOG_COLOR_RESET);
}

inline const char* resultColor(bool ok) {
  return ok ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

inline const char* stateColor(bool initialized, bool online,
                              uint8_t consecutiveFailures) {
  if (!initialized) {
    return LOG_COLOR_GRAY;
  }
  if (!online) {
    return LOG_COLOR_RED;
  }
  return consecutiveFailures == 0U ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW;
}

}  // namespace cli
