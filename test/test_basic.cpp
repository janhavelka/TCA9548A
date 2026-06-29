/**
 * @file test_basic.cpp
 * @brief Unit tests for TCA9548A driver (native platform)
 */

#include <unity.h>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include "Arduino.h"
#include "Wire.h"

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

uint32_t fakeNowMs(void*) {
  return gMillis;
}

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

void assertOfflineBusyNoIo(const TCA9548A::Status& st,
                           const TCA9548A::TCA9548A& dev,
                           int writeCallsBefore,
                           int readCallsBefore) {
  TEST_ASSERT_EQUAL(TCA9548A::Err::BUSY, st.code);
  TEST_ASSERT_EQUAL(writeCallsBefore, gFake.writeCalls);
  TEST_ASSERT_EQUAL(readCallsBefore, gFake.readCalls);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));
}

struct DownstreamContext {
  int calls = 0;
  uint8_t instructionCost = 1;
  uint8_t maxInstructionsSeen = 0;
  uint8_t observedMask = 0;
  TCA9548A::Status terminal = TCA9548A::Status::Ok();
};

TCA9548A::Status fakeDownstreamPoll(uint32_t nowMs, uint8_t maxInstructions,
                                    uint8_t& instructionsUsed, void* user) {
  (void)nowMs;
  auto* ctx = static_cast<DownstreamContext*>(user);
  ctx->calls++;
  ctx->maxInstructionsSeen = maxInstructions;
  ctx->observedMask = gFake.readValue;
  instructionsUsed = ctx->instructionCost;
  return ctx->terminal;
}

TCA9548A::Status fakeResetToZero(void* user) {
  if (user != nullptr) {
    *static_cast<bool*>(user) = true;
  }
  gFake.readValue = 0x00;
  return TCA9548A::Status::Ok();
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

void test_status_helpers() {
  TCA9548A::Status st = TCA9548A::Status::Error(TCA9548A::Err::I2C_ERROR, "fail", 7);
  TEST_ASSERT_TRUE(st.is(TCA9548A::Err::I2C_ERROR));
  TEST_ASSERT_FALSE(st.is(TCA9548A::Err::TIMEOUT));
  TEST_ASSERT_FALSE(static_cast<bool>(st));
  TEST_ASSERT_FALSE(st.inProgress());

  TCA9548A::Status inProgress{TCA9548A::Err::IN_PROGRESS, 0, "queued"};
  TEST_ASSERT_TRUE(inProgress.inProgress());
  TEST_ASSERT_TRUE(static_cast<bool>(TCA9548A::Status::Ok()));
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
  TEST_ASSERT_NULL(cfg.resetUser);
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

void test_begin_caches_existing_control_mask() {
  gFake.reset();
  gFake.readValue = 0xA5;

  TCA9548A::TCA9548A dev;
  TCA9548A::Status st = dev.begin(makeConfig());
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xA5, dev.lastKnownMask());
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

void test_end_skips_bus_io_when_offline() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.offlineThreshold = 1;
  dev.begin(cfg);

  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  (void)dev.setChannelMask(0x01);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));

  const int writesBefore = gFake.writeCalls;
  const int readsBefore = gFake.readCalls;
  dev.end();

  TEST_ASSERT_EQUAL(writesBefore, gFake.writeCalls);
  TEST_ASSERT_EQUAL(readsBefore, gFake.readCalls);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::UNINIT),
                    static_cast<int>(dev.state()));
  TEST_ASSERT_EQUAL(0, dev.consecutiveFailures());
}

void test_get_settings_snapshot() {
  gFake.reset();
  gMillis = 77;

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig(0x75);
  cfg.nowMs = fakeNowMs;
  cfg.offlineThreshold = 4;
  cfg.recoverBackoffMs = 321;
  dev.begin(cfg);
  dev.setChannelMask(0x12);

  TCA9548A::SettingsSnapshot snap;
  TCA9548A::Status st = dev.getSettings(snap);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(snap.initialized);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::READY),
                    static_cast<int>(snap.state));
  TEST_ASSERT_EQUAL_HEX8(0x75, snap.i2cAddress);
  TEST_ASSERT_EQUAL(50u, snap.i2cTimeoutMs);
  TEST_ASSERT_EQUAL(4, snap.offlineThreshold);
  TEST_ASSERT_EQUAL(321u, snap.recoverBackoffMs);
  TEST_ASSERT_TRUE(snap.hasNowMsHook);
  TEST_ASSERT_FALSE(snap.hasHardReset);
  TEST_ASSERT_TRUE(snap.recoverUseHardReset);
  TEST_ASSERT_EQUAL_HEX8(0x12, snap.lastKnownMask);
  TEST_ASSERT_TRUE(dev.isInitialized());
  TEST_ASSERT_EQUAL_HEX8(0x75, dev.getConfig().i2cAddress);
}

void test_driver_state_alias_matches_state() {
  gFake.reset();
  TCA9548A::TCA9548A dev;

  TEST_ASSERT_EQUAL(static_cast<int>(dev.state()),
                    static_cast<int>(dev.driverState()));

  TEST_ASSERT_TRUE(dev.begin(makeConfig()).ok());
  TEST_ASSERT_EQUAL(static_cast<int>(dev.state()),
                    static_cast<int>(dev.driverState()));

  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  (void)dev.setChannelMask(0x01);
  TEST_ASSERT_EQUAL(static_cast<int>(dev.state()),
                    static_cast<int>(dev.driverState()));
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

void test_register_helpers() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());

  uint8_t value = 0;
  TCA9548A::Status st = dev.writeRegister(TCA9548A::cmd::CONTROL_REG, 0x3C);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x3C, dev.lastKnownMask());

  st = dev.readRegister(TCA9548A::cmd::CONTROL_REG, value);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x3C, value);

  st = dev.readRegister(0x01, value);
  TEST_ASSERT_EQUAL(TCA9548A::Err::INVALID_PARAM, st.code);
  st = dev.writeRegister(0x01, 0xAA);
  TEST_ASSERT_EQUAL(TCA9548A::Err::INVALID_PARAM, st.code);
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
  TEST_ASSERT_EQUAL(TCA9548A::Err::NOT_INITIALIZED, dev.probe().code);
  TEST_ASSERT_EQUAL(TCA9548A::Err::NOT_INITIALIZED, dev.recover().code);
  TEST_ASSERT_EQUAL(TCA9548A::Err::NOT_INITIALIZED, dev.hardReset().code);
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

void test_offline_latch_blocks_normal_io() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.offlineThreshold = 1;
  dev.begin(cfg);

  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  (void)dev.setChannelMask(0x01);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));

  gFake.writeResult = TCA9548A::Err::OK;
  gFake.readResult = TCA9548A::Err::OK;
  const int writesBefore = gFake.writeCalls;
  const int readsBefore = gFake.readCalls;
  uint8_t mask = 0;
  bool enabled = false;

  assertOfflineBusyNoIo(dev.selectChannel(0), dev, writesBefore, readsBefore);
  assertOfflineBusyNoIo(dev.setChannelMask(0x02), dev, writesBefore, readsBefore);
  assertOfflineBusyNoIo(dev.disableAll(), dev, writesBefore, readsBefore);
  assertOfflineBusyNoIo(dev.readChannelMask(mask), dev, writesBefore, readsBefore);
  assertOfflineBusyNoIo(dev.enableChannels(0x04), dev, writesBefore, readsBefore);
  assertOfflineBusyNoIo(dev.disableChannels(0x01), dev, writesBefore, readsBefore);
  assertOfflineBusyNoIo(dev.isChannelEnabled(0, enabled), dev, writesBefore, readsBefore);
  TEST_ASSERT_EQUAL(1, dev.consecutiveFailures());
}

void test_recover_is_allowed_while_offline_latched() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.offlineThreshold = 1;
  cfg.recoverUseHardReset = false;
  dev.begin(cfg);

  TEST_ASSERT_TRUE(dev.setChannelMask(0x12).ok());
  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  (void)dev.setChannelMask(0x12);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));

  gFake.writeResult = TCA9548A::Err::OK;
  gFake.readResult = TCA9548A::Err::OK;
  gFake.readValue = 0x12;
  const int readsBefore = gFake.readCalls;

  TCA9548A::Status st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_GREATER_THAN(readsBefore, gFake.readCalls);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::READY),
                    static_cast<int>(dev.state()));
  TEST_ASSERT_EQUAL(0, dev.consecutiveFailures());
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

void test_probe_maps_device_not_found() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());

  gFake.readResult = TCA9548A::Err::I2C_NACK_ADDR;
  TCA9548A::Status st = dev.probe();
  TEST_ASSERT_EQUAL(TCA9548A::Err::DEVICE_NOT_FOUND, st.code);
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

  struct ResetContext {
    uint32_t tag;
    bool called;
  };
  static constexpr uint32_t RESET_TAG = 0xA55A1234u;
  static constexpr uint32_t TRANSPORT_TAG = 0x13572468u;
  ResetContext resetCtx{RESET_TAG, false};
  ResetContext transportCtx{TRANSPORT_TAG, false};
  auto resetFn = [](void* user) -> TCA9548A::Status {
    auto* ctx = static_cast<ResetContext*>(user);
    if (ctx == nullptr || ctx->tag != RESET_TAG) {
      return TCA9548A::Status::Error(TCA9548A::Err::INVALID_CONFIG,
                                     "wrong reset context");
    }
    ctx->called = true;
    return TCA9548A::Status::Ok();
  };

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.hardReset = resetFn;
  cfg.i2cUser = &transportCtx;
  cfg.resetUser = &resetCtx;
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
  TEST_ASSERT_TRUE(resetCtx.called);
  TEST_ASSERT_FALSE(transportCtx.called);
}

void test_recover_restores_last_known_mask_after_reset() {
  gFake.reset();

  auto resetFn = [](void* user) -> TCA9548A::Status {
    (void)user;
    gFake.readValue = 0x00;
    return TCA9548A::Status::Ok();
  };

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.hardReset = resetFn;
  dev.begin(cfg);
  TEST_ASSERT_TRUE(dev.setChannelMask(0x24).ok());

  gFake.readValue = 0x00;
  TCA9548A::Status st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x24, gFake.readValue);
  TEST_ASSERT_EQUAL_HEX8(0x24, dev.lastKnownMask());
}

void test_recover_restore_failure_keeps_offline_latch() {
  gFake.reset();

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.offlineThreshold = 1;
  cfg.recoverUseHardReset = false;
  dev.begin(cfg);
  TEST_ASSERT_TRUE(dev.setChannelMask(0x24).ok());

  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  (void)dev.setChannelMask(0x24);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));

  gFake.readResult = TCA9548A::Err::OK;
  gFake.readValue = 0x00;
  TCA9548A::Status st = dev.recover();

  TEST_ASSERT_EQUAL(TCA9548A::Err::I2C_ERROR, st.code);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));
  TEST_ASSERT_EQUAL_HEX8(0x24, dev.lastKnownMask());
  TEST_ASSERT_EQUAL(1, dev.consecutiveFailures());
}

void test_recover_hard_reset_restore_failure_keeps_offline_latch() {
  gFake.reset();

  bool resetCalled = false;
  auto resetFn = [](void* user) -> TCA9548A::Status {
    *static_cast<bool*>(user) = true;
    gFake.readValue = 0x00;
    return TCA9548A::Status::Ok();
  };

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.offlineThreshold = 1;
  cfg.hardReset = resetFn;
  cfg.resetUser = &resetCalled;
  dev.begin(cfg);
  TEST_ASSERT_TRUE(dev.setChannelMask(0x24).ok());

  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  (void)dev.setChannelMask(0x24);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));

  gFake.readResult = TCA9548A::Err::OK;
  TCA9548A::Status st = dev.recover();

  TEST_ASSERT_TRUE(resetCalled);
  TEST_ASSERT_EQUAL(TCA9548A::Err::I2C_ERROR, st.code);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));
  TEST_ASSERT_EQUAL_HEX8(0x24, dev.lastKnownMask());
  TEST_ASSERT_EQUAL(1, dev.consecutiveFailures());
}

void test_recover_hard_reset_callback_failure_does_not_fall_through() {
  gFake.reset();

  bool resetCalled = false;
  auto resetFn = [](void* user) -> TCA9548A::Status {
    *static_cast<bool*>(user) = true;
    return TCA9548A::Status::Error(TCA9548A::Err::I2C_BUS,
                                   "reset gpio failed");
  };

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.offlineThreshold = 1;
  cfg.hardReset = resetFn;
  cfg.resetUser = &resetCalled;
  dev.begin(cfg);

  gFake.writeResult = TCA9548A::Err::I2C_ERROR;
  (void)dev.setChannelMask(0x01);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));

  gFake.writeResult = TCA9548A::Err::OK;
  gFake.readResult = TCA9548A::Err::OK;
  const int writesBefore = gFake.writeCalls;
  const int readsBefore = gFake.readCalls;
  TCA9548A::Status st = dev.recover();

  TEST_ASSERT_TRUE(resetCalled);
  TEST_ASSERT_EQUAL(TCA9548A::Err::I2C_BUS, st.code);
  TEST_ASSERT_EQUAL(writesBefore, gFake.writeCalls);
  TEST_ASSERT_EQUAL(readsBefore, gFake.readCalls);
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::OFFLINE),
                    static_cast<int>(dev.state()));
}

void test_recover_backoff_enforced() {
  gFake.reset();
  gMillis = 100;

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.nowMs = fakeNowMs;
  cfg.recoverBackoffMs = 50;
  dev.begin(cfg);

  TEST_ASSERT_TRUE(dev.recover().ok());
  TEST_ASSERT_EQUAL(TCA9548A::Err::TIMEOUT, dev.recover().code);

  gMillis = 151;
  TEST_ASSERT_TRUE(dev.recover().ok());
}

void test_recover_backoff_blocks_i2c_and_reset_while_gated() {
  gFake.reset();
  gMillis = 100;

  int resetCalls = 0;
  auto resetFn = [](void* user) -> TCA9548A::Status {
    ++(*static_cast<int*>(user));
    gFake.readValue = 0x00;
    return TCA9548A::Status::Ok();
  };

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.nowMs = fakeNowMs;
  cfg.recoverBackoffMs = 50;
  cfg.hardReset = resetFn;
  cfg.resetUser = &resetCalls;
  cfg.recoverUseHardReset = true;
  dev.begin(cfg);

  TEST_ASSERT_TRUE(dev.recover().ok());
  const int readsBefore = gFake.readCalls;
  const int writesBefore = gFake.writeCalls;
  const int resetBefore = resetCalls;

  TCA9548A::Status st = dev.recover();
  TEST_ASSERT_EQUAL(TCA9548A::Err::TIMEOUT, st.code);
  TEST_ASSERT_EQUAL(readsBefore, gFake.readCalls);
  TEST_ASSERT_EQUAL(writesBefore, gFake.writeCalls);
  TEST_ASSERT_EQUAL(resetBefore, resetCalls);
}

void test_recover_backoff_not_enforced_without_now_hook() {
  gFake.reset();

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.recoverBackoffMs = 50;
  dev.begin(cfg);

  TEST_ASSERT_TRUE(dev.recover().ok());
  TEST_ASSERT_TRUE(dev.recover().ok());
}

void test_hard_reset_requires_callback() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TEST_ASSERT_EQUAL(TCA9548A::Err::UNSUPPORTED, dev.hardReset().code);
}

void test_hard_reset_uses_reset_user_not_i2c_user() {
  gFake.reset();

  struct UserContext {
    uint32_t tag;
    bool touched;
  };
  static constexpr uint32_t RESET_TAG = 0xABCD0001u;
  static constexpr uint32_t I2C_TAG = 0xABCD0002u;
  UserContext resetCtx{RESET_TAG, false};
  UserContext i2cCtx{I2C_TAG, false};

  auto resetFn = [](void* user) -> TCA9548A::Status {
    auto* ctx = static_cast<UserContext*>(user);
    if (ctx == nullptr || ctx->tag != RESET_TAG) {
      return TCA9548A::Status::Error(TCA9548A::Err::INVALID_CONFIG,
                                     "wrong reset context");
    }
    ctx->touched = true;
    gFake.readValue = 0x00;
    return TCA9548A::Status::Ok();
  };

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.i2cUser = &i2cCtx;
  cfg.hardReset = resetFn;
  cfg.resetUser = &resetCtx;
  dev.begin(cfg);
  TEST_ASSERT_TRUE(dev.setChannelMask(0x55).ok());

  TCA9548A::Status st = dev.hardReset();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(resetCtx.touched);
  TEST_ASSERT_FALSE(i2cCtx.touched);
  TEST_ASSERT_EQUAL_HEX8(0x00, dev.lastKnownMask());
}

void test_hard_reset_reads_back_control_register() {
  gFake.reset();

  auto resetFn = [](void* user) -> TCA9548A::Status {
    (void)user;
    gFake.readValue = 0x00;
    return TCA9548A::Status::Ok();
  };

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.hardReset = resetFn;
  dev.begin(cfg);
  dev.setChannelMask(0x7F);

  TCA9548A::Status st = dev.hardReset();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x00, dev.lastKnownMask());
  TEST_ASSERT_EQUAL(static_cast<int>(TCA9548A::DriverState::READY),
                    static_cast<int>(dev.state()));
}

// ============================================================================
// Command Table Tests
// ============================================================================

void test_command_table_constants() {
  TEST_ASSERT_EQUAL(0x70, TCA9548A::cmd::DEFAULT_ADDRESS);
  TEST_ASSERT_EQUAL(0x70, TCA9548A::cmd::I2C_ADDR_MIN);
  TEST_ASSERT_EQUAL(0x77, TCA9548A::cmd::I2C_ADDR_MAX);
  TEST_ASSERT_EQUAL(8, TCA9548A::cmd::NUM_ADDRESSES);
  TEST_ASSERT_EQUAL(0x07, TCA9548A::cmd::ADDRESS_PIN_MASK);
  TEST_ASSERT_EQUAL(0x00, TCA9548A::cmd::CONTROL_REG);
  TEST_ASSERT_EQUAL(0x00, TCA9548A::cmd::CONTROL_REG_DEFAULT);
  TEST_ASSERT_EQUAL(8, TCA9548A::cmd::NUM_CHANNELS);
  TEST_ASSERT_EQUAL(0, TCA9548A::cmd::FIRST_CHANNEL);
  TEST_ASSERT_EQUAL(7, TCA9548A::cmd::LAST_CHANNEL);
  TEST_ASSERT_EQUAL(8, TCA9548A::cmd::MAX_DEVICES_PER_BUS);
  TEST_ASSERT_EQUAL(0x01, TCA9548A::cmd::CH0);
  TEST_ASSERT_EQUAL(0x80, TCA9548A::cmd::CH7);
  TEST_ASSERT_EQUAL(0xFF, TCA9548A::cmd::ALL_CHANNELS);
  TEST_ASSERT_EQUAL(0x00, TCA9548A::cmd::NO_CHANNELS);
  TEST_ASSERT_EQUAL(100000u, TCA9548A::cmd::I2C_STANDARD_MODE_HZ);
  TEST_ASSERT_EQUAL(400000u, TCA9548A::cmd::I2C_FAST_MODE_HZ);
  TEST_ASSERT_EQUAL(400u, TCA9548A::cmd::MAX_BUS_CAPACITANCE_PF);
  TEST_ASSERT_EQUAL(6u, TCA9548A::cmd::RESET_MIN_LOW_NS);
  TEST_ASSERT_EQUAL(500u, TCA9548A::cmd::RESET_SDA_RELEASE_MAX_NS);
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
// Poll-Chunked Job Tests
// ============================================================================

void test_poll_set_mask_respects_zero_budget() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());

  TEST_ASSERT_TRUE(dev.startSetTca9548aMaskJob(0x5A).ok());
  const int writesBefore = gFake.writeCalls;

  TCA9548A::PollJobResult result;
  TCA9548A::Status st = dev.pollJob(0, 0, result);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_FALSE(result.complete);
  TEST_ASSERT_EQUAL(0, result.instructionsUsed);
  TEST_ASSERT_EQUAL(writesBefore, gFake.writeCalls);

  st = dev.pollJob(0, 1, result);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(result.active);
  TEST_ASSERT_TRUE(result.complete);
  TEST_ASSERT_EQUAL(1, result.instructionsUsed);
  TEST_ASSERT_EQUAL_HEX8(0x5A, dev.lastKnownMask());
}

void test_poll_read_mask_is_one_instruction() {
  gFake.reset();
  gFake.readValue = 0xA6;
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());

  uint8_t mask = 0;
  const int readsBefore = gFake.readCalls;
  TEST_ASSERT_TRUE(dev.startReadTca9548aMaskJob(mask).ok());

  TCA9548A::PollJobResult result;
  TCA9548A::Status st = dev.pollJob(0, 1, result);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(1, result.instructionsUsed);
  TEST_ASSERT_EQUAL(readsBefore + 1, gFake.readCalls);
  TEST_ASSERT_EQUAL_HEX8(0xA6, mask);
}

void test_poll_enable_channels_splits_read_modify_write_with_budget_one() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TEST_ASSERT_TRUE(dev.setChannelMask(0x01).ok());

  TEST_ASSERT_TRUE(dev.startEnableTca9548aChannelsJob(TCA9548A::cmd::CH2).ok());
  const int readsBefore = gFake.readCalls;
  const int writesBefore = gFake.writeCalls;

  TCA9548A::PollJobResult result;
  TCA9548A::Status st = dev.pollJob(0, 1, result);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_EQUAL(1, result.instructionsUsed);
  TEST_ASSERT_EQUAL(readsBefore + 1, gFake.readCalls);
  TEST_ASSERT_EQUAL(writesBefore, gFake.writeCalls);

  st = dev.pollJob(0, 1, result);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(result.complete);
  TEST_ASSERT_EQUAL(1, result.instructionsUsed);
  TEST_ASSERT_EQUAL(writesBefore + 1, gFake.writeCalls);
  TEST_ASSERT_EQUAL_HEX8(0x05, dev.lastKnownMask());
}

void test_poll_enable_channels_can_share_read_and_write_when_budget_allows() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TEST_ASSERT_TRUE(dev.setChannelMask(0x01).ok());

  TEST_ASSERT_TRUE(dev.startEnableTca9548aChannelsJob(TCA9548A::cmd::CH2).ok());
  const int readsBefore = gFake.readCalls;
  const int writesBefore = gFake.writeCalls;

  TCA9548A::PollJobResult result;
  TCA9548A::Status st = dev.pollJob(0, 2, result);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(result.complete);
  TEST_ASSERT_EQUAL(2, result.instructionsUsed);
  TEST_ASSERT_EQUAL(readsBefore + 1, gFake.readCalls);
  TEST_ASSERT_EQUAL(writesBefore + 1, gFake.writeCalls);
  TEST_ASSERT_EQUAL_HEX8(0x05, dev.lastKnownMask());
}

void test_poll_disable_channels_noop_consumes_only_read() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TEST_ASSERT_TRUE(dev.setChannelMask(0x0F).ok());

  TEST_ASSERT_TRUE(dev.startDisableTca9548aChannelsJob(0xF0).ok());
  const int readsBefore = gFake.readCalls;
  const int writesBefore = gFake.writeCalls;

  TCA9548A::PollJobResult result;
  TCA9548A::Status st = dev.pollJob(0, 2, result);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(result.complete);
  TEST_ASSERT_EQUAL(1, result.instructionsUsed);
  TEST_ASSERT_EQUAL(readsBefore + 1, gFake.readCalls);
  TEST_ASSERT_EQUAL(writesBefore, gFake.writeCalls);
  TEST_ASSERT_EQUAL_HEX8(0x0F, dev.lastKnownMask());
}

void test_poll_select_downstream_restore_are_separate_polls() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TEST_ASSERT_TRUE(dev.setChannelMask(0xAA).ok());

  DownstreamContext downstream;
  TEST_ASSERT_TRUE(dev.startSelectTca9548aChannelJob(
      2, 0xAA, fakeDownstreamPoll, &downstream).ok());
  const int writesBefore = gFake.writeCalls;

  TCA9548A::PollJobResult result;
  TCA9548A::Status st = dev.pollJob(10, 3, result);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL(1, result.instructionsUsed);
  TEST_ASSERT_EQUAL(writesBefore + 1, gFake.writeCalls);
  TEST_ASSERT_EQUAL(0, downstream.calls);
  TEST_ASSERT_EQUAL_HEX8(TCA9548A::cmd::CH2, gFake.readValue);

  st = dev.pollJob(11, 3, result);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL(1, result.instructionsUsed);
  TEST_ASSERT_EQUAL(1, downstream.calls);
  TEST_ASSERT_EQUAL(3, downstream.maxInstructionsSeen);
  TEST_ASSERT_EQUAL_HEX8(TCA9548A::cmd::CH2, downstream.observedMask);
  TEST_ASSERT_EQUAL(writesBefore + 1, gFake.writeCalls);

  st = dev.pollJob(12, 3, result);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(result.complete);
  TEST_ASSERT_EQUAL(1, result.instructionsUsed);
  TEST_ASSERT_EQUAL(writesBefore + 2, gFake.writeCalls);
  TEST_ASSERT_EQUAL_HEX8(0xAA, dev.lastKnownMask());
}

void test_poll_select_restore_runs_after_downstream_failure() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TEST_ASSERT_TRUE(dev.setChannelMask(0xAA).ok());

  DownstreamContext downstream;
  downstream.terminal =
      TCA9548A::Status::Error(TCA9548A::Err::I2C_ERROR, "downstream failed");
  TEST_ASSERT_TRUE(dev.startSelectTca9548aMaskJob(
      TCA9548A::cmd::CH1, 0xAA, fakeDownstreamPoll, &downstream).ok());

  TCA9548A::PollJobResult result;
  TEST_ASSERT_TRUE(dev.pollJob(10, 1, result).inProgress());
  TEST_ASSERT_TRUE(dev.pollJob(11, 1, result).inProgress());

  TCA9548A::Status st = dev.pollJob(12, 1, result);
  TEST_ASSERT_EQUAL(TCA9548A::Err::I2C_ERROR, st.code);
  TEST_ASSERT_TRUE(result.complete);
  TEST_ASSERT_EQUAL_HEX8(0xAA, dev.lastKnownMask());
  TEST_ASSERT_FALSE(dev.pollJobActive());
}

void test_poll_recover_backoff_gate_consumes_no_instructions_or_io() {
  gFake.reset();
  gMillis = 100;

  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.nowMs = fakeNowMs;
  cfg.recoverBackoffMs = 50;
  cfg.recoverUseHardReset = false;
  dev.begin(cfg);
  TEST_ASSERT_TRUE(dev.recover().ok());

  TEST_ASSERT_TRUE(dev.startRecoverTca9548aJob().ok());
  gMillis = 120;
  const int readsBefore = gFake.readCalls;
  const int writesBefore = gFake.writeCalls;

  TCA9548A::PollJobResult result;
  TCA9548A::Status st = dev.pollJob(gMillis, 5, result);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_EQUAL(0, result.instructionsUsed);
  TEST_ASSERT_EQUAL(readsBefore, gFake.readCalls);
  TEST_ASSERT_EQUAL(writesBefore, gFake.writeCalls);

  gMillis = 151;
  st = dev.pollJob(gMillis, 5, result);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(result.complete);
  TEST_ASSERT_EQUAL(2, result.instructionsUsed);
  TEST_ASSERT_EQUAL(readsBefore + 1, gFake.readCalls);
  TEST_ASSERT_EQUAL(writesBefore + 1, gFake.writeCalls);
}

void test_poll_recover_hard_reset_counts_reset_verify_restore() {
  gFake.reset();

  bool resetCalled = false;
  TCA9548A::TCA9548A dev;
  TCA9548A::Config cfg = makeConfig();
  cfg.hardReset = fakeResetToZero;
  cfg.resetUser = &resetCalled;
  cfg.recoverBackoffMs = 0;
  dev.begin(cfg);
  TEST_ASSERT_TRUE(dev.setChannelMask(0x24).ok());

  TEST_ASSERT_TRUE(dev.startRecoverTca9548aJob().ok());
  const int readsBefore = gFake.readCalls;
  const int writesBefore = gFake.writeCalls;

  TCA9548A::PollJobResult result;
  TCA9548A::Status st = dev.pollJob(0, 2, result);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_TRUE(resetCalled);
  TEST_ASSERT_EQUAL(2, result.instructionsUsed);
  TEST_ASSERT_EQUAL(readsBefore + 1, gFake.readCalls);
  TEST_ASSERT_EQUAL(writesBefore, gFake.writeCalls);

  st = dev.pollJob(0, 1, result);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(result.complete);
  TEST_ASSERT_EQUAL(1, result.instructionsUsed);
  TEST_ASSERT_EQUAL(writesBefore + 1, gFake.writeCalls);
  TEST_ASSERT_EQUAL_HEX8(0x24, dev.lastKnownMask());
}

void test_sync_io_returns_busy_while_poll_job_active() {
  gFake.reset();
  TCA9548A::TCA9548A dev;
  dev.begin(makeConfig());
  TEST_ASSERT_TRUE(dev.startSetTca9548aMaskJob(0x33).ok());

  uint8_t mask = 0;
  TEST_ASSERT_EQUAL(TCA9548A::Err::BUSY, dev.setChannelMask(0x44).code);
  TEST_ASSERT_EQUAL(TCA9548A::Err::BUSY, dev.readChannelMask(mask).code);
  TEST_ASSERT_TRUE(dev.pollJobActive());
  dev.cancelPollJob();
  TEST_ASSERT_FALSE(dev.pollJobActive());
  TEST_ASSERT_TRUE(dev.setChannelMask(0x44).ok());
}

// ============================================================================
// Version Tests
// ============================================================================

void test_version_constants() {
  TEST_ASSERT_EQUAL(
      static_cast<uint32_t>(TCA9548A::VERSION_MAJOR) * 10000u +
          static_cast<uint32_t>(TCA9548A::VERSION_MINOR) * 100u +
          static_cast<uint32_t>(TCA9548A::VERSION_PATCH),
      TCA9548A::VERSION_CODE);
  TEST_ASSERT_EQUAL(TCA9548A::VERSION_CODE,
                    static_cast<uint32_t>(TCA9548A::VERSION_INT));
  TEST_ASSERT_NOT_NULL(TCA9548A::VERSION);
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
  RUN_TEST(test_status_helpers);

  // Config
  RUN_TEST(test_config_defaults);
  RUN_TEST(test_begin_rejects_null_callbacks);
  RUN_TEST(test_begin_rejects_invalid_address);
  RUN_TEST(test_begin_rejects_zero_timeout);
  RUN_TEST(test_begin_rejects_huge_timeout);
  RUN_TEST(test_begin_accepts_all_valid_addresses);
  RUN_TEST(test_begin_caches_existing_control_mask);

  // Lifecycle
  RUN_TEST(test_begin_success);
  RUN_TEST(test_begin_device_not_found);
  RUN_TEST(test_end_resets_state);
  RUN_TEST(test_end_disables_all_channels);
  RUN_TEST(test_end_skips_bus_io_when_offline);
  RUN_TEST(test_get_settings_snapshot);
  RUN_TEST(test_driver_state_alias_matches_state);

  // Channel control
  RUN_TEST(test_select_channel_0);
  RUN_TEST(test_select_channel_7);
  RUN_TEST(test_select_channel_invalid);
  RUN_TEST(test_set_channel_mask);
  RUN_TEST(test_set_all_channels);
  RUN_TEST(test_disable_all_channels);
  RUN_TEST(test_read_channel_mask);
  RUN_TEST(test_register_helpers);
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
  RUN_TEST(test_offline_latch_blocks_normal_io);
  RUN_TEST(test_recover_is_allowed_while_offline_latched);
  RUN_TEST(test_health_recovery_on_success);
  RUN_TEST(test_total_counters_increment);

  // Probe
  RUN_TEST(test_probe_does_not_affect_health);
  RUN_TEST(test_probe_success);
  RUN_TEST(test_probe_maps_device_not_found);

  // Recovery
  RUN_TEST(test_recover_success);
  RUN_TEST(test_recover_with_hard_reset);
  RUN_TEST(test_recover_restores_last_known_mask_after_reset);
  RUN_TEST(test_recover_restore_failure_keeps_offline_latch);
  RUN_TEST(test_recover_hard_reset_restore_failure_keeps_offline_latch);
  RUN_TEST(test_recover_hard_reset_callback_failure_does_not_fall_through);
  RUN_TEST(test_recover_backoff_enforced);
  RUN_TEST(test_recover_backoff_blocks_i2c_and_reset_while_gated);
  RUN_TEST(test_recover_backoff_not_enforced_without_now_hook);
  RUN_TEST(test_hard_reset_requires_callback);
  RUN_TEST(test_hard_reset_uses_reset_user_not_i2c_user);
  RUN_TEST(test_hard_reset_reads_back_control_register);

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

  // Poll-chunked jobs
  RUN_TEST(test_poll_set_mask_respects_zero_budget);
  RUN_TEST(test_poll_read_mask_is_one_instruction);
  RUN_TEST(test_poll_enable_channels_splits_read_modify_write_with_budget_one);
  RUN_TEST(test_poll_enable_channels_can_share_read_and_write_when_budget_allows);
  RUN_TEST(test_poll_disable_channels_noop_consumes_only_read);
  RUN_TEST(test_poll_select_downstream_restore_are_separate_polls);
  RUN_TEST(test_poll_select_restore_runs_after_downstream_failure);
  RUN_TEST(test_poll_recover_backoff_gate_consumes_no_instructions_or_io);
  RUN_TEST(test_poll_recover_hard_reset_counts_reset_verify_restore);
  RUN_TEST(test_sync_io_returns_busy_while_poll_job_active);

  // Version
  RUN_TEST(test_version_constants);

  return UNITY_END();
}
