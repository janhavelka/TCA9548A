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

/// Default I2C address used by most breakouts (A2=A1=A0=GND)
static constexpr uint8_t DEFAULT_ADDRESS = I2C_ADDR_BASE;

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

/// Number of address combinations exposed by A2/A1/A0
static constexpr uint8_t NUM_ADDRESSES = 8;

/// Mask of the configurable address bits
static constexpr uint8_t ADDRESS_PIN_MASK = 0x07;

// ============================================================================
// Control Register (single 8-bit register, no register address byte)
// ============================================================================

/// Logical control register identifier used in docs and CLI dumps.
/// The device does not consume a register address byte on the bus.
static constexpr uint8_t CONTROL_REG = 0x00;

/// Number of register-address bytes before control-register payload
static constexpr uint8_t CONTROL_REG_ADDR_BYTES = 0;

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

/// Lowest valid channel index
static constexpr uint8_t FIRST_CHANNEL = 0;

/// Highest valid channel index
static constexpr uint8_t LAST_CHANNEL = NUM_CHANNELS - 1;

/// Maximum number of TCA9548A devices on one upstream bus
static constexpr uint8_t MAX_DEVICES_PER_BUS = NUM_ADDRESSES;

// ============================================================================
// Data Lengths
// ============================================================================

/// Control register size in bytes
static constexpr size_t CONTROL_REG_LEN = 1;

// ============================================================================
// Bus / Reset Limits (datasheet-derived)
// ============================================================================

/// Standard-mode I2C maximum clock
static constexpr uint32_t I2C_STANDARD_MODE_HZ = 100000;

/// Fast-mode I2C maximum clock
static constexpr uint32_t I2C_FAST_MODE_HZ = 400000;

/// Maximum recommended bus capacitance per active segment
static constexpr uint32_t MAX_BUS_CAPACITANCE_PF = 400;

/// Minimum RESET low pulse width
static constexpr uint32_t RESET_MIN_LOW_NS = 6;

/// RESET-to-next-START recovery time
static constexpr uint32_t RESET_RECOVERY_NS = 0;

/// RESET-to-SDA-release worst-case timing
static constexpr uint32_t RESET_SDA_RELEASE_MAX_NS = 500;

} // namespace cmd
} // namespace TCA9548A
