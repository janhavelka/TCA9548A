/// @file Status.h
/// @brief Error codes and status handling for TCA9548A driver
#pragma once

#include <cstdint>

namespace TCA9548A {

/// Error codes for all TCA9548A operations
enum class Err : uint8_t {
  OK = 0,                 ///< Operation successful
  NOT_INITIALIZED,        ///< begin() not called
  INVALID_CONFIG,         ///< Invalid configuration parameter
  I2C_ERROR,              ///< I2C communication failure (unspecified)
  TIMEOUT,                ///< Driver-side timeout (internal wait/guard)
  INVALID_PARAM,          ///< Invalid parameter value
  DEVICE_NOT_FOUND,       ///< Device not responding on I2C bus
  UNSUPPORTED,            ///< Operation not supported (missing callback)
  I2C_NACK_ADDR,          ///< I2C NACK on address
  I2C_NACK_DATA,          ///< I2C NACK on data
  I2C_TIMEOUT,            ///< I2C transaction timeout
  I2C_BUS                 ///< I2C bus error (SDA stuck, arbitration, etc.)
};

/// Status structure returned by all fallible operations
struct Status {
  Err code = Err::OK;
  int32_t detail = 0;        ///< Implementation-specific detail (e.g., I2C error code)
  const char* msg = "";      ///< Static string describing the error

  constexpr Status() = default;
  constexpr Status(Err c, int32_t d, const char* m) : code(c), detail(d), msg(m) {}

  /// @return true if operation succeeded
  constexpr bool ok() const { return code == Err::OK; }

  /// Create a success status
  static constexpr Status Ok() { return Status{Err::OK, 0, "OK"}; }

  /// Create an error status
  static constexpr Status Error(Err err, const char* message, int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

} // namespace TCA9548A
