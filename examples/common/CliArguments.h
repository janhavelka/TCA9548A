/**
 * @file CliArguments.h
 * @brief Shared bounded numeric argument parsing for the example CLIs.
 * @note Example-only helper; not part of the library API.
 */
#pragma once

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace cli_shell {

/// Parse one unsigned decimal, octal, or hexadecimal command argument.
/// Reject signs (strtoul otherwise accepts and wraps negative values),
/// overflow, trailing input, and values above maximum. Preserve output on error.
inline bool parseUnsignedArgument(const char* command, const char* prefix,
                                  unsigned long maximum, unsigned long& output) {
  const size_t prefixLength = std::strlen(prefix);
  if (std::strncmp(command, prefix, prefixLength) != 0 ||
      command[prefixLength] != ' ') {
    return false;
  }
  const char* text = command + prefixLength + 1U;
  while (*text == ' ' || *text == '\t') {
    ++text;
  }
  if (*text < '0' || *text > '9') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long value = std::strtoul(text, &end, 0);
  if (errno == ERANGE || end == text || *end != '\0' || value > maximum) {
    return false;
  }
  output = value;
  return true;
}

}  // namespace cli_shell
