/**
 * @file CliArguments.h
 * @brief Shared bounded numeric argument parsing for the example CLIs.
 * @note Example-only helper; not part of the library API.
 */
#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>

namespace cli_shell {

/// Parse one unsigned decimal, octal, or hexadecimal command argument.
/// Reject signs (strtoul otherwise accepts and wraps negative values),
/// overflow, trailing input, and values outside the range. Preserve output on error.
inline bool parseUnsigned(const char* text, uint32_t minimum,
                           uint32_t maximum, uint32_t& output) {
  if (text == nullptr || *text < '0' || *text > '9') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long value = std::strtoul(text, &end, 0);
  if (errno == ERANGE || end == text || *end != '\0' ||
      value < minimum || value > maximum) {
    return false;
  }
  output = static_cast<uint32_t>(value);
  return true;
}

}  // namespace cli_shell
