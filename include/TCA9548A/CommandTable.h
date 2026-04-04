/// @file CommandTable.h
/// @brief Register addresses, bit masks, and constants for TCA9548A
#pragma once

#include <cstdint>
#include <cstddef>

namespace TCA9548A {
namespace cmd {

// ============================================================================
// I2C Addresses (7-bit)
// ============================================================================

/// Base I2C address (A2=A1=A0=GND)
static constexpr uint8_t I2C_ADDR_BASE = 0x70;

/// I2C address with A2=L, A1=L, A0=L
static constexpr uint8_t I2C_ADDR_0x70 = 0x70;

/// I2C address with A2=L, A1=L, A0=H
static constexpr uint8_t I2C_ADDR_0x71 = 0x71;

/// I2C address with A2=L, A1=H, A0=L
static constexpr uint8_t I2C_ADDR_0x72 = 0x72;

/// I2C address with A2=L, A1=H, A0=H
static constexpr uint8_t I2C_ADDR_0x73 = 0x73;

/// I2C address with A2=H, A1=L, A0=L
static constexpr uint8_t I2C_ADDR_0x74 = 0x74;

/// I2C address with A2=H, A1=L, A0=H
static constexpr uint8_t I2C_ADDR_0x75 = 0x75;

/// I2C address with A2=H, A1=H, A0=L
static constexpr uint8_t I2C_ADDR_0x76 = 0x76;

/// I2C address with A2=H, A1=H, A0=H
static constexpr uint8_t I2C_ADDR_0x77 = 0x77;

/// Minimum valid I2C address
static constexpr uint8_t I2C_ADDR_MIN = 0x70;

/// Maximum valid I2C address
static constexpr uint8_t I2C_ADDR_MAX = 0x77;

// ============================================================================
// Control Register (single 8-bit register, no register address byte)
// ============================================================================

/// Channel 0 enable bit (SC0/SD0)
static constexpr uint8_t CH0 = 0x01;

/// Channel 1 enable bit (SC1/SD1)
static constexpr uint8_t CH1 = 0x02;

/// Channel 2 enable bit (SC2/SD2)
static constexpr uint8_t CH2 = 0x04;

/// Channel 3 enable bit (SC3/SD3)
static constexpr uint8_t CH3 = 0x08;

/// Channel 4 enable bit (SC4/SD4)
static constexpr uint8_t CH4 = 0x10;

/// Channel 5 enable bit (SC5/SD5)
static constexpr uint8_t CH5 = 0x20;

/// Channel 6 enable bit (SC6/SD6)
static constexpr uint8_t CH6 = 0x40;

/// Channel 7 enable bit (SC7/SD7)
static constexpr uint8_t CH7 = 0x80;

/// All channels enabled
static constexpr uint8_t ALL_CHANNELS = 0xFF;

/// No channels enabled (POR/RESET default)
static constexpr uint8_t NO_CHANNELS = 0x00;

/// Default control register value after POR or RESET
static constexpr uint8_t CONTROL_REG_DEFAULT = 0x00;

// ============================================================================
// Channel Count
// ============================================================================

/// Total number of downstream channels
static constexpr uint8_t NUM_CHANNELS = 8;

// ============================================================================
// Data Lengths
// ============================================================================

/// Control register size in bytes
static constexpr size_t CONTROL_REG_LEN = 1;

} // namespace cmd
} // namespace TCA9548A
