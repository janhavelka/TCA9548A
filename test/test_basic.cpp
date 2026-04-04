/**
 * @file test_basic.cpp
 * @brief Unit tests for TCA9548A driver (native platform)
 */

#include <unity.h>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include "TCA9548A/TCA9548A.h"
#include "TCA9548A/CommandTable.h"

// ============================================================================
// Arduino stubs for native tests
// ============================================================================

uint32_t gMillis = 0;
uint32_t gMicros = 0;
uint32_t gMillisStep = 0;
uint32_t gMicrosStep = 0;
SerialClass Serial;
TwoWire Wire;

// ============================================================================
// Fake Transport
// ============================================================================

namespace {

struct FakeTransport {
  // Scripted responses
  uint8_t readValue = 0x00;
  TCA9548A::Err writeResult = TCA9548A::Err::OK;
  TCA9548A::Err readResult = TCA9548A::Err::OK;
  uint8_t lastWrittenValue = 0xFF;
  int writeCalls = 0;
  int readCalls = 0;

  void reset() {
    readValue = 0x00;
    writeResult = TCA9548A::Err::OK;
    readResult = TCA9548A::Err::OK;
    lastWrittenValue = 0xFF;
    writeCalls = 0;
    readCalls = 0;
  }
};

FakeTransport gFake;

TCA9548A::Status fakeWrite(uint8_t addr, const uint8_t* data, size_t len,
                            uint32_t timeoutMs, void* user) {
  (void)addr; (void)timeoutMs; (void)user;
  gFake.writeCalls++;
  if (len > 0) {
    gFake.lastWrittenValue = data[0];
  }
  if (gFake.writeResult != TCA9548A::Err::OK) {
    return TCA9548A::Status::Error(gFake.writeResult, "Fake write error");
  }
  // Update read value to match what was written (simulates real device)
  if (len > 0) {
    gFake.readValue = data[len - 1]; // last byte wins per TCA9548A spec
  }
  return TCA9548A::Status::Ok();
}

TCA9548A::Status fakeWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                                 uint8_t* rxData, size_t rxLen,
                                 uint32_t timeoutMs, void* user) {
  (void)addr; (void)txData; (void)txLen; (void)timeoutMs; (void)user;
  gFake.readCalls++;
  if (gFake.readResult != TCA9548A::Err::OK) {
    return TCA9548A::Status::Error(gFake.readResult, "Fake read error");
  }
  if (rxLen > 0 && rxData != nullptr) {
    rxData[0] = gFake.readValue;
  }
  return TCA9548A::Status::Ok();
}

TCA9548A::Config makeConfig(uint8_t addr = 0x70) {
  TCA9548A::Config cfg;
  cfg.i2cWrite = fakeWrite;
  cfg.i2cWriteRead = fakeWriteRead;
  cfg.i2cUser = nullptr;
  cfg.i2cAddress = addr;
  cfg.i2cTimeoutMs = 50;
  cfg.offlineThreshold = 3;
  cfg.recoverBackoffMs = 0; // disable backoff for tests
  return cfg;
}

} // namespace

// ============================================================================
// Status API Tests
// ============================================================================

void test_status_ok() {
  TCA9548A::Status st = TCA9548A::Status::Ok();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(TCA9548A::Err::OK, st.code);
}

void test_status_error() {
  TCA9548A::Status st = TCA9548A::Status::Error(TCA9548A::Err::I2C_ERROR, "fail", 42);
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_EQUAL(TCA9548A::Err::I2C_ERROR, st.code);
  TEST_ASSERT_EQUAL(42, st.detail);
}

// ============================================================================
// Configuration Tests
// ============================================================================

void test_config_defaults() {
  TCA9548A::Config cfg;
  TEST_ASSERT_EQUAL(0x70, cfg.i2cAddress);
  TEST_ASSERT_EQUAL(50, cfg.i2cTimeoutMs);
  TEST_ASSERT_EQUAL(5, cfg.offlineThreshold);
  TEST_ASSERT_NULL(cfg.i2cWrite);
  TEST_ASSERT_NULL(cfg.i2cWriteRead);
  TEST_ASSERT_NULL(cfg.hardReset);
}

void test_begin_rejects_null_callbacks() {
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg;
  cfg.i2cAddress = 0x70;
  TCA9548A::Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL(TCA9548A::Err::INVALID_CONFIG, st.code);
}

void test_begin_rejects_invalid_address() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig(0x50); // outside 0x70–0x77
  TCA9548A::Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL(TCA9548A::Err::INVALID_CONFIG, st.code);
}

void test_begin_rejects_zero_timeout() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.i2cTimeoutMs = 0;
  TCA9548A::Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL(TCA9548A::Err::INVALID_CONFIG, st.code);
}

void test_begin_rejects_huge_timeout() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.i2cTimeoutMs = 100000;
  TCA9548A::Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL(TCA9548A::Err::INVALID_CONFIG, st.code);
}

void test_begin_accepts_all_valid_addresses() {
  for (uint8_t addr = 0x70; addr <= 0x77; addr++) {
    gFake.reset();
    TCA9548A::TCA9548A dev;
    TCA9548A::Config cfg = makeConfig(addr);
    TCA9548A::Status st = dev.begin(cfg);
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), "begin() should succeed for valid address");
  }
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

void test_begin_success() {
  gFake.reset();
  gMillis = 100;
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  TCA9548A::Status st = dev.begin(cfg);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::READY),
                    static_cast<int>(dev.state()));
  TEST_ASSERT_TRUE(dev.isOnline());
}

void test_begin_device_not_found() {
  gFake.reset();
  gFake.readResult = TCA9548A::Err::I2C_NACK_ADDR;
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  TCA9548A::Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL(TCA9548A::Err::DEVICE_NOT_FOUND, st.code);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::UNINIT),
                    static_cast<int>(dev.state()));
}

void test_end_resets_state() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  dev.end();
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::UNINIT),
                    static_cast<int>(dev.state()));
  TEST_ASSERT_FALSE(dev.isOnline());
}

void test_end_disables_all_channels() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  dev.selectChannel(3);
  dev.end();
  // end() should have written 0x00 (best-effort)
  TEST_ASSERT_EQUAL(0x00, gFake.lastWrittenValue);
}

// ============================================================================
// Channel Control Tests
// ============================================================================

void test_select_channel_0() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TCA9548A::Status st = dev.selectChannel(0);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(0x01, gFake.readValue);
}

void test_select_channel_7() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TCA9548A::Status st = dev.selectChannel(7);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(0x80, gFake.readValue);
}

void test_select_channel_invalid() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TCA9548A::Status st = dev.selectChannel(8);
  TEST_ASSERT_EQUAL(TCA9548A::Err::INVALID_PARAM, st.code);
}

void test_set_channel_mask() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TCA9548A::Status st = dev.setChannelMask(0x0F);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(0x0F, gFake.readValue);
}

void test_set_all_channels() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TCA9548A::Status st = dev.setChannelMask(0xFF);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(0xFF, gFake.readValue);
}

void test_disable_all_channels() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  dev.setChannelMask(0xFF);
  TCA9548A::Status st = dev.disableAll();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(0x00, gFake.readValue);
}

void test_read_channel_mask() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  dev.setChannelMask(0xA5);

  uint8_t mask = 0;
  TCA9548A::Status st = dev.readChannelMask(mask);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(0xA5, mask);
}

void test_enable_channels() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  // Start with channel 0
  dev.selectChannel(0);

  // Enable channel 2 in addition
  TCA9548A::Status st = dev.enableChannels(TCA9548A::cmd::CH2);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(0x05, gFake.readValue); // CH0 | CH2
}

void test_disable_channels() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  dev.setChannelMask(0x0F); // channels 0-3

  TCA9548A::Status st = dev.disableChannels(TCA9548A::cmd::CH1 | TCA9548A::cmd::CH3);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(0x05, gFake.readValue); // CH0 | CH2
}

void test_is_channel_enabled() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  dev.setChannelMask(0x42); // channels 1 and 6

  bool enabled = false;
  TCA9548A::Status st = dev.isChannelEnabled(1, enabled);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(enabled);

  st = dev.isChannelEnabled(0, enabled);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(enabled);

  st = dev.isChannelEnabled(6, enabled);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(enabled);
}

void test_is_channel_enabled_invalid() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());

  bool enabled = false;
  TCA9548A::Status st = dev.isChannelEnabled(8, enabled);
  TEST_ASSERT_EQUAL(TCA9548A::Err::INVALID_PARAM, st.code);
}

// ============================================================================
// Not Initialized Tests
// ============================================================================

void test_operations_reject_when_not_initialized() {
  TCA9548A::TCA9548A dev;
  uint8_t mask = 0;
  bool enabled = false;

  TEST_ASSERT_EQUAL(TCA9548A::Err::NOT_INITIALIZED, dev.selectChannel(0).code);
  TEST_ASSERT_EQUAL(TCA9548A::Err::NOT_INITIALIZED, dev.setChannelMask(0xFF).code);
  TEST_ASSERT_EQUAL(TCA9548A::Err::NOT_INITIALIZED, dev.enableChannels(0x01).code);
  TEST_ASSERT_EQUAL(TCA9548A::Err::NOT_INITIALIZED, dev.disableChannels(0x01).code);
  TEST_ASSERT_EQUAL(TCA9548A::Err::NOT_INITIALIZED, dev.disableAll().code);
  TEST_ASSERT_EQUAL(TCA9548A::Err::NOT_INITIALIZED, dev.readChannelMask(mask).code);
  TEST_ASSERT_EQUAL(TCA9548A::Err::NOT_INITIALIZED, dev.isChannelEnabled(0, enabled).code);
  TEST_ASSERT_EQUAL(TCA9548A::Err::NOT_INITIALIZED, dev.recover().code);
}

// ============================================================================
// Health Tracking Tests
// ============================================================================

void test_health_ready_after_begin() {
  gFake.reset();
  gMillis = 500;
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());

  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::READY),
                    static_cast<int>(dev.state()));
  TEST_ASSERT_EQUAL(0, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL(0u, dev.totalFailures());
}

void test_health_degraded_after_failure() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());

  // Inject a failure
  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  dev.setChannelMask(0x01);

  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::DEGRADED),
                    static_cast<int>(dev.state()));
  TEST_ASSERT_EQUAL(1, dev.consecutiveFailures());
  TEST_ASSERT_TRUE(dev.isOnline()); // DEGRADED is still online
}

void test_health_offline_after_threshold() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.offlineThreshold = 3;
  dev.begin(cfg);

  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  dev.setChannelMask(0x01);
  dev.setChannelMask(0x01);
  dev.setChannelMask(0x01);

  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));
  TEST_ASSERT_EQUAL(3, dev.consecutiveFailures());
  TEST_ASSERT_FALSE(dev.isOnline()); // OFFLINE is not online
}

void test_health_recovery_on_success() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.offlineThreshold = 3;
  dev.begin(cfg);

  // Cause degraded
  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  dev.setChannelMask(0x01);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::DEGRADED),
                    static_cast<int>(dev.state()));

  // Recover with a successful operation
  gFake.writeResult = TCA9548A::Err::OK;
  dev.setChannelMask(0x02);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::READY),
                    static_cast<int>(dev.state()));
  TEST_ASSERT_EQUAL(0, dev.consecutiveFailures());
}

void test_total_counters_increment() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());

  dev.setChannelMask(0x01);
  dev.setChannelMask(0x02);
  TEST_ASSERT_EQUAL(2u, dev.totalSuccess());

  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  dev.setChannelMask(0x03);
  TEST_ASSERT_EQUAL(1u, dev.totalFailures());
}

// ============================================================================
// Probe Tests
// ============================================================================

void test_probe_does_not_affect_health() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());

  // probe uses raw transport — should not count
  gFake.readResult = TCA9548A::Err::I2C_NACK_ADDR;
  TCA9548A::Status st = dev.probe();
  TEST_ASSERT_FALSE(st.ok());

  // State should still be READY (probe doesn't track health)
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::READY),
                    static_cast<int>(dev.state()));
  TEST_ASSERT_EQUAL(0, dev.consecutiveFailures());
}

void test_probe_success() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());

  TCA9548A::Status st = dev.probe();
  TEST_ASSERT_TRUE(st.ok());
}

// ============================================================================
// Recovery Tests
// ============================================================================

void test_recover_success() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.offlineThreshold = 2;
  dev.begin(cfg);

  // Go offline
  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  dev.setChannelMask(0x01);
  dev.setChannelMask(0x01);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));

  // Recover
  gFake.writeResult = TCA9548A::Err::OK;
  gFake.readResult = TCA9548A::Err::OK;
  gFake.readValue = 0x00;
  TCA9548A::Status st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::READY),
                    static_cast<int>(dev.state()));
}

void test_recover_with_hard_reset() {
  gFake.reset();

  bool resetCalled = false;
  auto resetFn = [](void* user) -> TCA9548A::Status {
    *static_cast<bool*>(user) = true;
    return TCA9548A::Status::Ok();
  };

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.hardReset = resetFn;
  cfg.i2cUser = &resetCalled;
  // Patch write/read to use gFake, ignoring user pointer for reset test
  cfg.i2cWrite = [](uint8_t addr, const uint8_t* data, size_t len,
                     uint32_t timeoutMs, void* user) -> TCA9548A::Status {
    return fakeWrite(addr, data, len, timeoutMs, user);
  };
  cfg.i2cWriteRead = [](uint8_t addr, const uint8_t* txData, size_t txLen,
                          uint8_t* rxData, size_t rxLen,
                          uint32_t timeoutMs, void* user) -> TCA9548A::Status {
    return fakeWriteRead(addr, txData, txLen, rxData, rxLen, timeoutMs, user);
  };
  cfg.recoverUseHardReset = true;
  dev.begin(cfg);

  // Go degraded
  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  dev.setChannelMask(0x01);

  // Recover - should call hard reset
  gFake.writeResult = TCA9548A::Err::OK;
  gFake.readResult = TCA9548A::Err::OK;
  gFake.readValue = 0x00;
  TCA9548A::Status st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(resetCalled);
}

// ============================================================================
// Command Table Tests
// ============================================================================

void test_command_table_constants() {
  TEST_ASSERT_EQUAL(0x70, TCA9548A::cmd::I2C_ADDR_MIN);
  TEST_ASSERT_EQUAL(0x77, TCA9548A::cmd::I2C_ADDR_MAX);
  TEST_ASSERT_EQUAL(0x00, TCA9548A::cmd::CONTROL_REG_DEFAULT);
  TEST_ASSERT_EQUAL(8, TCA9548A::cmd::NUM_CHANNELS);
  TEST_ASSERT_EQUAL(0x01, TCA9548A::cmd::CH0);
  TEST_ASSERT_EQUAL(0x80, TCA9548A::cmd::CH7);
  TEST_ASSERT_EQUAL(0xFF, TCA9548A::cmd::ALL_CHANNELS);
  TEST_ASSERT_EQUAL(0x00, TCA9548A::cmd::NO_CHANNELS);
}

void test_channel_bit_values() {
  TEST_ASSERT_EQUAL(0x01, TCA9548A::cmd::CH0);
  TEST_ASSERT_EQUAL(0x02, TCA9548A::cmd::CH1);
  TEST_ASSERT_EQUAL(0x04, TCA9548A::cmd::CH2);
  TEST_ASSERT_EQUAL(0x08, TCA9548A::cmd::CH3);
  TEST_ASSERT_EQUAL(0x10, TCA9548A::cmd::CH4);
  TEST_ASSERT_EQUAL(0x20, TCA9548A::cmd::CH5);
  TEST_ASSERT_EQUAL(0x40, TCA9548A::cmd::CH6);
  TEST_ASSERT_EQUAL(0x80, TCA9548A::cmd::CH7);
}

// ============================================================================
// Last Known Mask Tests
// ============================================================================

void test_last_known_mask_after_select() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  dev.selectChannel(5);
  TEST_ASSERT_EQUAL(0x20, dev.lastKnownMask());
}

void test_last_known_mask_after_write() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  dev.setChannelMask(0xAB);
  TEST_ASSERT_EQUAL(0xAB, dev.lastKnownMask());
}

void test_last_known_mask_zero_after_end() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  dev.setChannelMask(0xFF);
  dev.end();
  TEST_ASSERT_EQUAL(0x00, dev.lastKnownMask());
}

// ============================================================================
// Enable/Disable No-Op Optimization Tests
// ============================================================================

void test_enable_channels_noop_when_already_set() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  dev.setChannelMask(0x0F);

  int writesBefore = gFake.writeCalls;
  TCA9548A::Status st = dev.enableChannels(0x01); // already enabled
  TEST_ASSERT_TRUE(st.ok());
  // Should NOT have written again (one readControlReg but no writeControlReg)
  // readControlReg uses writeRead (readCalls), not writeCalls
  TEST_ASSERT_EQUAL(writesBefore, gFake.writeCalls);
}

void test_disable_channels_noop_when_already_clear() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  dev.setChannelMask(0x0F);

  int writesBefore = gFake.writeCalls;
  TCA9548A::Status st = dev.disableChannels(0xF0); // already disabled
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(writesBefore, gFake.writeCalls);
}

// ============================================================================
// Version Tests
// ============================================================================

void test_version_constants() {
  TEST_ASSERT_EQUAL(1, TCA9548A::VERSION_MAJOR);
  TEST_ASSERT_EQUAL(0, TCA9548A::VERSION_MINOR);
  TEST_ASSERT_EQUAL(0, TCA9548A::VERSION_PATCH);
  TEST_ASSERT_EQUAL(10000u, TCA9548A::VERSION_CODE);
}

// ============================================================================
// Test Runner
// ============================================================================

void setUp() {
  gFake.reset();
  gMillis = 0;
  gMicros = 0;
  gMillisStep = 0;
  gMicrosStep = 0;
}

void tearDown() {}

int main() {
  UNITY_BEGIN();

  // Status
  RUN_TEST(test_status_ok);
  RUN_TEST(test_status_error);

  // Config
  RUN_TEST(test_config_defaults);
  RUN_TEST(test_begin_rejects_null_callbacks);
  RUN_TEST(test_begin_rejects_invalid_address);
  RUN_TEST(test_begin_rejects_zero_timeout);
  RUN_TEST(test_begin_rejects_huge_timeout);
  RUN_TEST(test_begin_accepts_all_valid_addresses);

  // Lifecycle
  RUN_TEST(test_begin_success);
  RUN_TEST(test_begin_device_not_found);
  RUN_TEST(test_end_resets_state);
  RUN_TEST(test_end_disables_all_channels);

  // Channel control
  RUN_TEST(test_select_channel_0);
  RUN_TEST(test_select_channel_7);
  RUN_TEST(test_select_channel_invalid);
  RUN_TEST(test_set_channel_mask);
  RUN_TEST(test_set_all_channels);
  RUN_TEST(test_disable_all_channels);
  RUN_TEST(test_read_channel_mask);
  RUN_TEST(test_enable_channels);
  RUN_TEST(test_disable_channels);
  RUN_TEST(test_is_channel_enabled);
  RUN_TEST(test_is_channel_enabled_invalid);

  // Not initialized
  RUN_TEST(test_operations_reject_when_not_initialized);

  // Health tracking
  RUN_TEST(test_health_ready_after_begin);
  RUN_TEST(test_health_degraded_after_failure);
  RUN_TEST(test_health_offline_after_threshold);
  RUN_TEST(test_health_recovery_on_success);
  RUN_TEST(test_total_counters_increment);

  // Probe
  RUN_TEST(test_probe_does_not_affect_health);
  RUN_TEST(test_probe_success);

  // Recovery
  RUN_TEST(test_recover_success);
  RUN_TEST(test_recover_with_hard_reset);

  // Command table
  RUN_TEST(test_command_table_constants);
  RUN_TEST(test_channel_bit_values);

  // Cached mask
  RUN_TEST(test_last_known_mask_after_select);
  RUN_TEST(test_last_known_mask_after_write);
  RUN_TEST(test_last_known_mask_zero_after_end);

  // No-op optimizations
  RUN_TEST(test_enable_channels_noop_when_already_set);
  RUN_TEST(test_disable_channels_noop_when_already_clear);

  // Version
  RUN_TEST(test_version_constants);

  return UNITY_END();
}
