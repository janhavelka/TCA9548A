/**
 * @file CliLineBuffer.h
 * @brief Framework-neutral fixed-storage command-line accumulator.
 * @note Example-only helper; not part of the library API.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cli_shell {

/// Result of feeding one byte to FixedLineBuffer.
enum class LineResult : uint8_t {
  NONE,              ///< No complete line is available yet
  READY,             ///< A trimmed command was copied to the caller buffer
  TOO_LONG,          ///< The complete overlong line was discarded
  OUTPUT_TOO_SMALL   ///< The destination cannot hold the complete command
};

/// Fixed-capacity CR/LF command accumulator shared by both example CLIs.
class FixedLineBuffer {
public:
  /// Maximum storage including the terminating null byte.
  static constexpr size_t CAPACITY = 128U;

  /// Feed one received byte into the accumulator.
  ///
  /// Empty and whitespace-only lines produce NONE. Once an overlong line is
  /// detected, every remaining byte is discarded through the next CR or LF so
  /// no partial command can be dispatched.
  /// @param value Received byte.
  /// @param output Destination for a completed, trimmed command.
  /// @param outputCapacity Destination capacity including its null byte.
  /// @return Current line result.
  LineResult push(char value, char* output, size_t outputCapacity) {
    if (value != '\r' && value != '\n') {
      if (_discarding) {
        return LineResult::NONE;
      }
      if (_length >= CAPACITY - 1U) {
        _discarding = true;
        _length = 0U;
        return LineResult::NONE;
      }
      _buffer[_length++] = value;
      return LineResult::NONE;
    }

    if (_discarding) {
      reset();
      return LineResult::TOO_LONG;
    }
    if (_length == 0U) {
      return LineResult::NONE;
    }

    size_t first = 0U;
    while (first < _length &&
           (_buffer[first] == ' ' || _buffer[first] == '\t')) {
      ++first;
    }
    while (_length > first &&
           (_buffer[_length - 1U] == ' ' ||
            _buffer[_length - 1U] == '\t')) {
      --_length;
    }

    const size_t commandLength = _length - first;
    if (commandLength == 0U) {
      reset();
      return LineResult::NONE;
    }
    if (output == nullptr || outputCapacity == 0U ||
        commandLength >= outputCapacity) {
      reset();
      return LineResult::OUTPUT_TOO_SMALL;
    }

    std::memcpy(output, _buffer + first, commandLength);
    output[commandLength] = '\0';
    reset();
    return LineResult::READY;
  }

  /// Discard any partial input and return to the empty state.
  void reset() {
    _length = 0U;
    _discarding = false;
  }

private:
  char _buffer[CAPACITY]{};
  size_t _length = 0U;
  bool _discarding = false;
};

}  // namespace cli_shell
