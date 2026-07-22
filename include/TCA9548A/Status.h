/// @file Status.h
/// @brief Error codes and status handling for TCA9548A driver
#pragma once

#include <cstdint>

namespace TCA9548A {

/// Error codes for all TCA9548A operations.
///
/// Numeric values are an append-only compatibility contract. The synchronous
/// core does not synthesize DEVICE_NOT_FOUND or IN_PROGRESS; it preserves
/// specific I2C failures and requires terminal callback results.
enum class Err : uint8_t {
  OK = 0,                      ///< Operation successful
  NOT_INITIALIZED = 1,         ///< No valid Config is currently bound
  INVALID_CONFIG = 2,          ///< Invalid configuration parameter
  I2C_ERROR = 3,               ///< I2C communication failure (unspecified)
  TIMEOUT = 4,                 ///< Non-I2C callback timeout (for example RESET)
  INVALID_PARAM = 5,           ///< Invalid parameter value
  DEVICE_NOT_FOUND = 6,        ///< Reserved compatibility device-absence code
  UNSUPPORTED = 7,             ///< Operation not supported (missing callback)
  I2C_NACK_ADDR = 8,           ///< I2C NACK on address
  I2C_NACK_DATA = 9,           ///< I2C NACK on data
  I2C_TIMEOUT = 10,            ///< I2C transaction timeout
  I2C_BUS = 11,                ///< I2C bus error (SDA stuck, arbitration, etc.)
  BUSY = 12,                   ///< Operation cannot start because state is busy
  IN_PROGRESS = 13,            ///< Reserved for sibling-driver compatibility
  RESET_STATE_MISMATCH = 14    ///< RESET completed but byte was not 0x00
};

/// Status structure returned by all fallible operations
struct Status {
  Err code = Err::OK;     ///< Stable typed result code
  int32_t detail = 0;     ///< Backend or operation-specific detail
  const char* msg = "";   ///< Static-lifetime diagnostic string

  constexpr Status() = default;

  /// Construct an explicit status value.
  /// @param c Stable typed result code.
  /// @param d Optional backend or operation-specific detail.
  /// @param m Static-lifetime diagnostic message.
  constexpr Status(Err c, int32_t d, const char* m)
      : code(c), detail(d), msg(m) {}

  /// @return true if operation succeeded
  constexpr bool ok() const { return code == Err::OK; }

  /// @param expected Error code to compare with Status::code.
  /// @return true if status matches the requested error code
  constexpr bool is(Err expected) const { return code == expected; }

  /// Test the reserved sibling-driver progress code.
  /// @return true only when code equals Err::IN_PROGRESS.
  constexpr bool inProgress() const { return code == Err::IN_PROGRESS; }

  /// @return true if operation succeeded
  explicit constexpr operator bool() const { return ok(); }

  /// Create a success status.
  /// @return Status with code OK, detail zero, and a static message.
  static constexpr Status Ok() { return Status{Err::OK, 0, "OK"}; }

  /// Create an error status.
  /// @param err Stable typed error code.
  /// @param message Static-lifetime diagnostic message.
  /// @param detailCode Optional backend or operation-specific detail.
  /// @return Status containing the supplied error information.
  static constexpr Status Error(Err err, const char* message,
                                int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

} // namespace TCA9548A
