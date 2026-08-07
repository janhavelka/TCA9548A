#include <cstdint>
#include <limits>

#include <unity.h>

#include "TCA9548A/TCA9548A.h"
#include "support/ScriptedTransport.h"

namespace {

using TCA9548A::Channel;
using TCA9548A::ChannelMask;
using TCA9548A::Config;
using TCA9548A::DriverState;
using TCA9548A::Err;
using TCA9548A::MaskProvenance;
using TCA9548A::Status;
using Driver = TCA9548A::TCA9548A;
using TCA9548A::TransportErr;
using test_support::CallKind;
using test_support::ResetHarness;
using test_support::ScriptedTransport;
using test_support::TransportCall;

ScriptedTransport gTransport;
ResetHarness gReset;
uint32_t gNowMs = 0;

uint32_t nowMs(void* user) {
  return user != nullptr ? *static_cast<uint32_t*>(user) : 0;
}

Config makeConfig(bool withReset = false) {
  Config config;
  config.i2cWrite = ScriptedTransport::write;
  config.i2cWriteRead = ScriptedTransport::read;
  config.i2cUser = &gTransport;
  config.nowMs = nowMs;
  config.timeUser = &gNowMs;
  config.i2cAddress = 0x72;
  config.i2cTimeoutMs = 7;
  config.resetTimeoutMs = 13;
  config.offlineThreshold = 3;
  if (withReset) {
    config.hardReset = ResetHarness::reset;
    config.resetUser = &gReset;
  }
  return config;
}

void assertStatus(const Status& status, Err expected, int32_t detail = 0) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected),
                        static_cast<int>(status.code));
  TEST_ASSERT_EQUAL_INT32(detail, status.detail);
}

void assertReadCall(const TransportCall& call, uint8_t address = 0x72,
                    uint32_t timeoutMs = 7) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CallKind::READ),
                        static_cast<int>(call.kind));
  TEST_ASSERT_EQUAL_HEX8(address, call.address);
  TEST_ASSERT_TRUE(call.txWasNull);
  TEST_ASSERT_FALSE(call.rxWasNull);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(call.txLen));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(call.rxLen));
  TEST_ASSERT_EQUAL_UINT32(timeoutMs, call.timeoutMs);
  TEST_ASSERT_EQUAL_PTR(&gTransport, call.user);
}

void assertWriteCall(const TransportCall& call, uint8_t value,
                     uint8_t address = 0x72, uint32_t timeoutMs = 7) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CallKind::WRITE),
                        static_cast<int>(call.kind));
  TEST_ASSERT_EQUAL_HEX8(address, call.address);
  TEST_ASSERT_FALSE(call.txWasNull);
  TEST_ASSERT_TRUE(call.rxWasNull);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(call.txLen));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(call.rxLen));
  TEST_ASSERT_EQUAL_HEX8(value, call.txBytes[0]);
  TEST_ASSERT_EQUAL_UINT32(timeoutMs, call.timeoutMs);
  TEST_ASSERT_EQUAL_PTR(&gTransport, call.user);
}

void beginOk(Driver& mux, const Config& config = makeConfig()) {
  const Status status = mux.begin(config);
  TEST_ASSERT_TRUE_MESSAGE(status.ok(), status.msg);
}

} // namespace

void setUp() {
  gTransport.reset();
  gReset = ResetHarness{};
  gReset.transport = &gTransport;
  gNowMs = 0;
}

void tearDown() {}

namespace {

void test_status_and_transport_helpers() {
  TEST_ASSERT_TRUE(Status::Ok().ok());
  TEST_ASSERT_TRUE(Status::Ok().is(Err::OK));
  TEST_ASSERT_FALSE(Status::Error(Err::I2C_BUS, "bus", 9).ok());
  TEST_ASSERT_TRUE(TCA9548A::TransportStatus::Ok().ok());
  TEST_ASSERT_FALSE(
      TCA9548A::TransportStatus::Error(TransportErr::TIMEOUT, 4).ok());
  const Err errors[] = {
      Err::OK,               Err::NOT_INITIALIZED, Err::INVALID_CONFIG,
      Err::I2C_ERROR,        Err::TIMEOUT,          Err::INVALID_PARAM,
      Err::DEVICE_NOT_FOUND, Err::UNSUPPORTED,      Err::I2C_NACK_ADDR,
      Err::I2C_NACK_DATA,    Err::I2C_TIMEOUT,      Err::I2C_BUS,
      Err::BUSY,             Err::IN_PROGRESS,      Err::RESET_STATE_MISMATCH,
      Err::RESET_ERROR,
  };
  const char* names[] = {
      "OK",               "NOT_INITIALIZED", "INVALID_CONFIG",
      "I2C_ERROR",        "TIMEOUT",          "INVALID_PARAM",
      "DEVICE_NOT_FOUND", "UNSUPPORTED",      "I2C_NACK_ADDR",
      "I2C_NACK_DATA",    "I2C_TIMEOUT",      "I2C_BUS",
      "BUSY",             "IN_PROGRESS",      "RESET_STATE_MISMATCH",
      "RESET_ERROR",
  };
  for (size_t index = 0; index < sizeof(errors) / sizeof(errors[0]); ++index) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(index),
                          static_cast<int>(errors[index]));
    TEST_ASSERT_EQUAL_STRING(names[index], TCA9548A::errorName(errors[index]));
    TEST_ASSERT_EQUAL_STRING(names[index], TCA9548A::toString(errors[index]));
  }
  TEST_ASSERT_EQUAL_STRING(
      "UNKNOWN", TCA9548A::errorName(static_cast<Err>(0xFFU)));

  const DriverState states[] = {DriverState::UNINIT, DriverState::READY,
                                DriverState::DEGRADED, DriverState::OFFLINE};
  const char* stateNames[] = {"UNINIT", "READY", "DEGRADED", "OFFLINE"};
  for (size_t index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(index),
                          static_cast<int>(states[index]));
    TEST_ASSERT_EQUAL_STRING(stateNames[index],
                             TCA9548A::driverStateName(states[index]));
    TEST_ASSERT_EQUAL_STRING(stateNames[index], TCA9548A::toString(states[index]));
  }
  TEST_ASSERT_EQUAL_STRING(
      "UNKNOWN",
      TCA9548A::driverStateName(static_cast<DriverState>(0xFFU)));

  const MaskProvenance provenance[] = {
      MaskProvenance::UNKNOWN, MaskProvenance::WRITE_COMPLETED,
      MaskProvenance::READBACK_OBSERVED};
  const char* provenanceNames[] = {"UNKNOWN", "WRITE_COMPLETED",
                                   "READBACK_OBSERVED"};
  for (size_t index = 0; index < sizeof(provenance) / sizeof(provenance[0]);
       ++index) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(index),
                          static_cast<int>(provenance[index]));
    TEST_ASSERT_EQUAL_STRING(
        provenanceNames[index],
        TCA9548A::maskProvenanceName(provenance[index]));
    TEST_ASSERT_EQUAL_STRING(provenanceNames[index],
                             TCA9548A::toString(provenance[index]));
  }
  TEST_ASSERT_EQUAL_STRING(
      "UNKNOWN",
      TCA9548A::maskProvenanceName(static_cast<MaskProvenance>(0xFFU)));
}

void test_address_helpers_cover_all_straps_and_boundaries() {
  TEST_ASSERT_FALSE(TCA9548A::cmd::isValidAddress(0x6F));
  for (uint8_t address = 0x70; address <= 0x77; ++address) {
    TEST_ASSERT_TRUE(TCA9548A::cmd::isValidAddress(address));
  }
  TEST_ASSERT_FALSE(TCA9548A::cmd::isValidAddress(0x78));

  for (uint8_t pins = 0; pins < 8; ++pins) {
    const uint8_t address = TCA9548A::cmd::addressFromPins(
        (pins & 0x04U) != 0, (pins & 0x02U) != 0, (pins & 0x01U) != 0);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(0x70U + pins), address);
  }
}

void test_channel_mask_helpers_are_exact() {
  TEST_ASSERT_EQUAL_HEX8(0x00, ChannelMask::none().raw());
  TEST_ASSERT_EQUAL_HEX8(0xFF, ChannelMask::all().raw());

  for (uint8_t index = 0; index < 8; ++index) {
    const Channel channel = static_cast<Channel>(index);
    const ChannelMask mask = ChannelMask::one(channel);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(1U << index), mask.raw());
    TEST_ASSERT_TRUE(mask.isOneHot());
    TEST_ASSERT_TRUE(mask.contains(channel));
  }

  TEST_ASSERT_TRUE(ChannelMask::none().isNone());
  TEST_ASSERT_FALSE(ChannelMask::none().isOneHot());
  TEST_ASSERT_FALSE(ChannelMask::fromRaw(0xA5).isOneHot());
  TEST_ASSERT_EQUAL_HEX8(
      0xA5, ChannelMask::fromRaw(0xA1)
                .withEnabled(ChannelMask::fromRaw(0x04))
                .raw());
  TEST_ASSERT_EQUAL_HEX8(
      0xA1, ChannelMask::fromRaw(0xA5)
                .withDisabled(ChannelMask::fromRaw(0x04))
                .raw());
  TEST_ASSERT_TRUE(ChannelMask::one(static_cast<Channel>(8)).isNone());
  TEST_ASSERT_FALSE(ChannelMask::all().contains(static_cast<Channel>(8)));
}

void test_config_defaults_are_bounded() {
  const Config config;
  TEST_ASSERT_EQUAL_HEX8(TCA9548A::cmd::DEFAULT_ADDRESS, config.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50, config.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(10, config.resetTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5, config.offlineThreshold);
  TEST_ASSERT_NULL(config.i2cWrite);
  TEST_ASSERT_NULL(config.i2cWriteRead);
}

void test_begin_rejects_invalid_config_without_io() {
  Config config = makeConfig();
  Driver mux;

  config.i2cWrite = nullptr;
  assertStatus(mux.begin(config), Err::INVALID_CONFIG);
  config = makeConfig();
  config.i2cWriteRead = nullptr;
  assertStatus(mux.begin(config), Err::INVALID_CONFIG);
  config = makeConfig();
  config.i2cTimeoutMs = 0;
  assertStatus(mux.begin(config), Err::INVALID_CONFIG);
  config.i2cTimeoutMs = 60001;
  assertStatus(mux.begin(config), Err::INVALID_CONFIG);
  config = makeConfig();
  config.resetTimeoutMs = 0;
  assertStatus(mux.begin(config), Err::INVALID_CONFIG);
  config.resetTimeoutMs = 60001;
  assertStatus(mux.begin(config), Err::INVALID_CONFIG);
  config = makeConfig();
  config.offlineThreshold = 0;
  assertStatus(mux.begin(config), Err::INVALID_CONFIG);
  config = makeConfig();
  config.i2cAddress = 0x6F;
  assertStatus(mux.begin(config), Err::INVALID_CONFIG);
  config.i2cAddress = 0x78;
  assertStatus(mux.begin(config), Err::INVALID_CONFIG);

  TEST_ASSERT_EQUAL_UINT32(0,
                           static_cast<uint32_t>(gTransport.callCount()));
  TEST_ASSERT_FALSE(mux.isBound());

  config = makeConfig();
  config.i2cTimeoutMs = 60000;
  config.resetTimeoutMs = 60000;
  beginOk(mux, config);
  TEST_ASSERT_EQUAL_UINT32(1,
                           static_cast<uint32_t>(gTransport.callCount()));
  assertReadCall(gTransport.call(0), 0x72, 60000);
}

void test_begin_accepts_every_valid_address_with_one_read() {
  for (uint8_t address = 0x70; address <= 0x77; ++address) {
    gTransport.reset(static_cast<uint8_t>(address - 0x70U));
    Config config = makeConfig();
    config.i2cAddress = address;
    Driver mux;
    beginOk(mux, config);
    TEST_ASSERT_EQUAL_UINT32(1,
                             static_cast<uint32_t>(gTransport.callCount()));
    assertReadCall(gTransport.call(0), address);
    mux.end();
  }
}

void test_begin_records_readback_and_tracked_health() {
  gTransport.reset(0xA5);
  gNowMs = 100;
  Driver mux;
  beginOk(mux);

  TEST_ASSERT_TRUE(mux.isBound());
  TEST_ASSERT_TRUE(mux.isInitialized());
  TEST_ASSERT_TRUE(mux.isOnline());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DriverState::READY),
                        static_cast<int>(mux.state()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(mux.state()),
                        static_cast<int>(mux.driverState()));
  TEST_ASSERT_EQUAL_UINT32(1, mux.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0, mux.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(100, mux.lastOkMs());
  const auto observation = mux.channelMaskObservation();
  TEST_ASSERT_TRUE(observation.known());
  TEST_ASSERT_TRUE(observation.verified());
  TEST_ASSERT_EQUAL_HEX8(0xA5, observation.mask.raw());
  assertReadCall(gTransport.call(0));
}

void test_failed_begin_preserves_binding_and_exact_error() {
  TEST_ASSERT_TRUE(gTransport.pushError(TransportErr::TIMEOUT, 77));
  gNowMs = 25;
  Driver mux;
  const Status status = mux.begin(makeConfig());
  assertStatus(status, Err::I2C_TIMEOUT, 77);

  TEST_ASSERT_TRUE(mux.isBound());
  TEST_ASSERT_FALSE(mux.isInitialized());
  TEST_ASSERT_FALSE(mux.isOnline());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DriverState::UNINIT),
                        static_cast<int>(mux.state()));
  TEST_ASSERT_EQUAL_UINT32(1, mux.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(1, mux.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(25, mux.lastErrorMs());
  assertStatus(mux.lastError(), Err::I2C_TIMEOUT, 77);
  TEST_ASSERT_FALSE(mux.channelMaskObservation().known());

  gTransport.clearHistory();
  gTransport.hardwareMask = 0x42;
  ChannelMask observed = ChannelMask::none();
  TEST_ASSERT_TRUE(mux.readChannelMask(observed).ok());
  TEST_ASSERT_EQUAL_HEX8(0x42, observed.raw());
  TEST_ASSERT_TRUE(mux.isInitialized());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DriverState::READY),
                        static_cast<int>(mux.state()));
  TEST_ASSERT_EQUAL_UINT8(0, mux.consecutiveFailures());
  assertReadCall(gTransport.call(0));
}

void test_rebind_is_rejected_transactionally() {
  Driver mux;
  beginOk(mux);
  gTransport.clearHistory();

  Config replacement = makeConfig();
  replacement.i2cAddress = 0x77;
  assertStatus(mux.begin(replacement), Err::BUSY);
  replacement.i2cWrite = nullptr;
  assertStatus(mux.begin(replacement), Err::BUSY);

  TEST_ASSERT_EQUAL_UINT32(0,
                           static_cast<uint32_t>(gTransport.callCount()));
  TEST_ASSERT_EQUAL_HEX8(0x72, mux.getConfig().i2cAddress);
  TEST_ASSERT_TRUE(mux.isBound());
}

void test_end_is_bus_silent_and_rebinding_is_repeatable() {
  Driver mux;
  beginOk(mux);
  gTransport.clearHistory();
  mux.end();
  mux.end();

  TEST_ASSERT_EQUAL_UINT32(0,
                           static_cast<uint32_t>(gTransport.callCount()));
  TEST_ASSERT_FALSE(mux.isBound());
  TEST_ASSERT_FALSE(mux.isInitialized());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DriverState::UNINIT),
                        static_cast<int>(mux.state()));
  TEST_ASSERT_FALSE(mux.channelMaskObservation().known());

  gTransport.hardwareMask = 0x11;
  beginOk(mux);
  TEST_ASSERT_TRUE(mux.channelMaskObservation().verified());
  TEST_ASSERT_EQUAL_HEX8(0x11,
                         mux.channelMaskObservation().mask.raw());
}

void test_lifetime_counters_survive_end_and_rebind() {
  Driver mux;
  beginOk(mux);
  TEST_ASSERT_TRUE(gTransport.pushError(TransportErr::BUS, 31));
  assertStatus(mux.disableAll(), Err::I2C_BUS, 31);
  TEST_ASSERT_EQUAL_UINT32(1, mux.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(1, mux.totalFailures());

  mux.end();
  TEST_ASSERT_EQUAL_UINT32(1, mux.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(1, mux.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(0, mux.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(0, mux.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(0, mux.lastErrorMs());
  assertStatus(mux.lastError(), Err::OK);

  beginOk(mux);
  TEST_ASSERT_EQUAL_UINT32(2, mux.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(1, mux.totalFailures());
}

void test_unbound_hardware_apis_do_no_work() {
  Driver mux;
  ChannelMask output = ChannelMask::fromRaw(0xA5);
  assertStatus(mux.probe(), Err::NOT_INITIALIZED);
  assertStatus(mux.recover(), Err::NOT_INITIALIZED);
  assertStatus(mux.hardReset(), Err::NOT_INITIALIZED);
  assertStatus(mux.selectChannel(Channel::CH0), Err::NOT_INITIALIZED);
  assertStatus(mux.writeChannelMask(ChannelMask::all()),
               Err::NOT_INITIALIZED);
  assertStatus(mux.disableAll(), Err::NOT_INITIALIZED);
  assertStatus(mux.readChannelMask(output), Err::NOT_INITIALIZED);
  TEST_ASSERT_EQUAL_HEX8(0xA5, output.raw());
  TEST_ASSERT_EQUAL_UINT32(0,
                           static_cast<uint32_t>(gTransport.callCount()));
}

void test_select_channel_encodes_every_one_hot_value() {
  Driver mux;
  beginOk(mux);
  gTransport.clearHistory();

  for (uint8_t index = 0; index < 8; ++index) {
    const Status status = mux.selectChannel(static_cast<Channel>(index));
    TEST_ASSERT_TRUE(status.ok());
    TEST_ASSERT_EQUAL_UINT32(
        static_cast<uint32_t>(index + 1U),
        static_cast<uint32_t>(gTransport.callCount()));
    assertWriteCall(gTransport.call(index),
                    static_cast<uint8_t>(1U << index));
  }

  const auto observation = mux.channelMaskObservation();
  TEST_ASSERT_TRUE(observation.known());
  TEST_ASSERT_FALSE(observation.verified());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MaskProvenance::WRITE_COMPLETED),
                        static_cast<int>(observation.provenance));
  TEST_ASSERT_EQUAL_HEX8(0x80, observation.mask.raw());
}

void test_invalid_channel_is_rejected_without_io_or_cache_change() {
  gTransport.reset(0x33);
  Driver mux;
  beginOk(mux);
  const auto before = mux.channelMaskObservation();
  const DriverState stateBefore = mux.state();
  const uint32_t successBefore = mux.totalSuccess();
  const uint32_t failureBefore = mux.totalFailures();
  const uint8_t consecutiveBefore = mux.consecutiveFailures();
  const uint32_t lastOkBefore = mux.lastOkMs();
  const uint32_t lastErrorMsBefore = mux.lastErrorMs();
  const Status lastErrorBefore = mux.lastError();
  gTransport.clearHistory();

  assertStatus(mux.selectChannel(static_cast<Channel>(8)),
               Err::INVALID_PARAM);
  TEST_ASSERT_EQUAL_UINT32(0,
                           static_cast<uint32_t>(gTransport.callCount()));
  const auto after = mux.channelMaskObservation();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(before.provenance),
                        static_cast<int>(after.provenance));
  TEST_ASSERT_EQUAL_HEX8(before.mask.raw(), after.mask.raw());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(stateBefore),
                        static_cast<int>(mux.state()));
  TEST_ASSERT_EQUAL_UINT32(successBefore, mux.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(failureBefore, mux.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(consecutiveBefore, mux.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(lastOkBefore, mux.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(lastErrorMsBefore, mux.lastErrorMs());
  assertStatus(mux.lastError(), lastErrorBefore.code, lastErrorBefore.detail);
}

void test_write_masks_are_exact_one_byte_transactions() {
  const uint8_t masks[] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10,
                           0x20, 0x40, 0x80, 0xA5, 0xFF};
  Driver mux;
  beginOk(mux);
  gTransport.clearHistory();

  for (size_t index = 0; index < sizeof(masks); ++index) {
    TEST_ASSERT_TRUE(
        mux.writeChannelMask(ChannelMask::fromRaw(masks[index])).ok());
    assertWriteCall(gTransport.call(index), masks[index]);
  }
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(sizeof(masks)),
                           static_cast<uint32_t>(gTransport.callCount()));
}

void test_disable_all_is_one_safe_off_write() {
  gTransport.reset(0xFF);
  Driver mux;
  beginOk(mux);
  gTransport.clearHistory();
  TEST_ASSERT_TRUE(mux.disableAll().ok());
  TEST_ASSERT_EQUAL_UINT32(1,
                           static_cast<uint32_t>(gTransport.callCount()));
  assertWriteCall(gTransport.call(0), 0x00);
  TEST_ASSERT_EQUAL_HEX8(0x00, gTransport.hardwareMask);
}

void test_read_is_read_only_and_assigns_output_only_on_success() {
  gTransport.reset(0x5A);
  Driver mux;
  beginOk(mux);
  gTransport.clearHistory();
  ChannelMask output = ChannelMask::fromRaw(0xC3);

  TEST_ASSERT_TRUE(mux.readChannelMask(output).ok());
  TEST_ASSERT_EQUAL_HEX8(0x5A, output.raw());
  TEST_ASSERT_EQUAL_UINT32(1,
                           static_cast<uint32_t>(gTransport.callCount()));
  assertReadCall(gTransport.call(0));

  // Model a controller that copied a byte before reporting terminal failure.
  TEST_ASSERT_TRUE(
      gTransport.pushError(TransportErr::BUS, 41, false, true, 0xEE));
  assertStatus(mux.readChannelMask(output), Err::I2C_BUS, 41);
  TEST_ASSERT_EQUAL_HEX8(0x5A, output.raw());
  TEST_ASSERT_FALSE(mux.channelMaskObservation().known());
}

void test_all_transport_errors_remain_distinct() {
  struct Case {
    TransportErr transport;
    Err driver;
    int32_t detail;
  };
  const Case cases[] = {
      {TransportErr::NACK_ADDR, Err::I2C_NACK_ADDR, 11},
      {TransportErr::NACK_DATA, Err::I2C_NACK_DATA, 12},
      {TransportErr::TIMEOUT, Err::I2C_TIMEOUT, 13},
      {TransportErr::BUS, Err::I2C_BUS, 14},
      {TransportErr::OTHER, Err::I2C_ERROR, 15},
  };

  Driver mux;
  beginOk(mux);
  gTransport.clearHistory();
  for (const Case& item : cases) {
    TEST_ASSERT_TRUE(gTransport.pushError(item.transport, item.detail));
    assertStatus(mux.writeChannelMask(ChannelMask::fromRaw(0xA5)),
                 item.driver, item.detail);
  }
  TEST_ASSERT_EQUAL_UINT32(
      static_cast<uint32_t>(sizeof(cases) / sizeof(cases[0])),
      static_cast<uint32_t>(gTransport.callCount()));
  TEST_ASSERT_FALSE(mux.channelMaskObservation().known());
}

void test_ambiguous_failed_write_invalidates_and_read_reconciles() {
  gTransport.reset(0x01);
  Driver mux;
  beginOk(mux);
  gTransport.clearHistory();
  TEST_ASSERT_TRUE(
      gTransport.pushError(TransportErr::TIMEOUT, 99, true));

  assertStatus(mux.writeChannelMask(ChannelMask::fromRaw(0x80)),
               Err::I2C_TIMEOUT, 99);
  TEST_ASSERT_EQUAL_HEX8(0x80, gTransport.hardwareMask);
  TEST_ASSERT_FALSE(mux.channelMaskObservation().known());

  ChannelMask observed = ChannelMask::none();
  TEST_ASSERT_TRUE(mux.readChannelMask(observed).ok());
  TEST_ASSERT_EQUAL_HEX8(0x80, observed.raw());
  TEST_ASSERT_TRUE(mux.channelMaskObservation().verified());
}

void test_successful_write_requires_later_read_for_verification() {
  Driver mux;
  beginOk(mux);
  gTransport.clearHistory();
  TEST_ASSERT_TRUE(
      mux.writeChannelMask(ChannelMask::fromRaw(0xA5)).ok());
  auto observation = mux.channelMaskObservation();
  TEST_ASSERT_TRUE(observation.known());
  TEST_ASSERT_FALSE(observation.verified());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MaskProvenance::WRITE_COMPLETED),
                        static_cast<int>(observation.provenance));

  ChannelMask readback;
  TEST_ASSERT_TRUE(mux.readChannelMask(readback).ok());
  observation = mux.channelMaskObservation();
  TEST_ASSERT_TRUE(observation.verified());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(MaskProvenance::READBACK_OBSERVED),
      static_cast<int>(observation.provenance));
}

void test_explicit_external_invalidation_preserves_only_historical_byte() {
  gTransport.reset(0x42);
  Driver mux;
  beginOk(mux);
  mux.invalidateChannelMask();
  const auto observation = mux.channelMaskObservation();
  TEST_ASSERT_FALSE(observation.known());
  TEST_ASSERT_FALSE(observation.verified());
  TEST_ASSERT_EQUAL_HEX8(0x42, observation.mask.raw());
}

void test_probe_updates_observation_but_not_health() {
  Driver mux;
  beginOk(mux);
  const uint32_t successBefore = mux.totalSuccess();
  const uint32_t failureBefore = mux.totalFailures();
  const uint32_t lastOkBefore = mux.lastOkMs();
  gTransport.clearHistory();
  gTransport.hardwareMask = 0x66;
  gNowMs = 1000;

  TEST_ASSERT_TRUE(mux.probe().ok());
  TEST_ASSERT_EQUAL_UINT32(successBefore, mux.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(failureBefore, mux.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(lastOkBefore, mux.lastOkMs());
  TEST_ASSERT_TRUE(mux.channelMaskObservation().verified());
  TEST_ASSERT_EQUAL_HEX8(0x66,
                         mux.channelMaskObservation().mask.raw());
  assertReadCall(gTransport.call(0));

  const DriverState stateBeforeFailure = mux.state();
  const uint8_t consecutiveBeforeFailure = mux.consecutiveFailures();
  const uint32_t lastErrorMsBeforeFailure = mux.lastErrorMs();
  const Status lastErrorBeforeFailure = mux.lastError();
  gNowMs = 1001;
  TEST_ASSERT_TRUE(gTransport.pushError(TransportErr::NACK_ADDR, 8));
  assertStatus(mux.probe(), Err::I2C_NACK_ADDR, 8);
  TEST_ASSERT_EQUAL_UINT32(successBefore, mux.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(failureBefore, mux.totalFailures());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(stateBeforeFailure),
                        static_cast<int>(mux.state()));
  TEST_ASSERT_EQUAL_UINT8(consecutiveBeforeFailure,
                          mux.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(lastOkBefore, mux.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(lastErrorMsBeforeFailure, mux.lastErrorMs());
  assertStatus(mux.lastError(), lastErrorBeforeFailure.code,
               lastErrorBeforeFailure.detail);
  TEST_ASSERT_FALSE(mux.channelMaskObservation().known());
}

void test_offline_is_passive_and_success_recovers_health() {
  Config config = makeConfig();
  config.offlineThreshold = 2;
  Driver mux;
  beginOk(mux, config);
  gTransport.clearHistory();

  TEST_ASSERT_TRUE(gTransport.pushError(TransportErr::BUS, 1));
  assertStatus(mux.disableAll(), Err::I2C_BUS, 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DriverState::DEGRADED),
                        static_cast<int>(mux.state()));
  TEST_ASSERT_TRUE(gTransport.pushError(TransportErr::BUS, 2));
  assertStatus(mux.disableAll(), Err::I2C_BUS, 2);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DriverState::OFFLINE),
                        static_cast<int>(mux.state()));
  TEST_ASSERT_FALSE(mux.isOnline());

  // OFFLINE is diagnostic only: this third request reaches the transport.
  TEST_ASSERT_TRUE(mux.disableAll().ok());
  TEST_ASSERT_EQUAL_UINT32(3,
                           static_cast<uint32_t>(gTransport.callCount()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DriverState::READY),
                        static_cast<int>(mux.state()));
  TEST_ASSERT_EQUAL_UINT8(0, mux.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(2, mux.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(2, mux.totalSuccess());

  mux.end();
  gTransport.reset();
  config = makeConfig();
  config.offlineThreshold = std::numeric_limits<uint8_t>::max();
  beginOk(mux, config);
  gTransport.clearHistory();
  gTransport.setDefaultResponse(
      {TCA9548A::TransportStatus::Error(TransportErr::BUS, 9)});

  for (uint16_t failure = 0; failure < 254U; ++failure) {
    assertStatus(mux.disableAll(), Err::I2C_BUS, 9);
  }
  TEST_ASSERT_EQUAL_UINT8(254, mux.consecutiveFailures());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DriverState::DEGRADED),
                        static_cast<int>(mux.state()));

  assertStatus(mux.disableAll(), Err::I2C_BUS, 9);
  TEST_ASSERT_EQUAL_UINT8(std::numeric_limits<uint8_t>::max(),
                          mux.consecutiveFailures());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DriverState::OFFLINE),
                        static_cast<int>(mux.state()));

  for (uint8_t failure = 0; failure < 45U; ++failure) {
    assertStatus(mux.disableAll(), Err::I2C_BUS, 9);
  }
  TEST_ASSERT_EQUAL_UINT8(std::numeric_limits<uint8_t>::max(),
                          mux.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(302, mux.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(300,
                           static_cast<uint32_t>(gTransport.callCount()));
  TEST_ASSERT_FALSE(gTransport.overflowed());
}

void test_health_timestamps_accept_clock_wrap_without_deadline_math() {
  gNowMs = std::numeric_limits<uint32_t>::max();
  Driver mux;
  beginOk(mux);
  TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<uint32_t>::max(),
                           mux.lastOkMs());

  gNowMs = 0;
  TEST_ASSERT_TRUE(gTransport.pushError(TransportErr::TIMEOUT, 3));
  assertStatus(mux.disableAll(), Err::I2C_TIMEOUT, 3);
  TEST_ASSERT_EQUAL_UINT32(0, mux.lastErrorMs());

  gNowMs = 1;
  TEST_ASSERT_TRUE(mux.disableAll().ok());
  TEST_ASSERT_EQUAL_UINT32(1, mux.lastOkMs());
}

void test_recover_is_one_safe_off_write_even_after_failed_begin() {
  TEST_ASSERT_TRUE(gTransport.pushError(TransportErr::NACK_ADDR, 5));
  Driver mux;
  assertStatus(mux.begin(makeConfig()), Err::I2C_NACK_ADDR, 5);
  gTransport.clearHistory();
  gTransport.hardwareMask = 0x80;

  TEST_ASSERT_TRUE(mux.recover().ok());
  TEST_ASSERT_EQUAL_UINT32(1,
                           static_cast<uint32_t>(gTransport.callCount()));
  assertWriteCall(gTransport.call(0), 0x00);
  TEST_ASSERT_EQUAL_HEX8(0x00, gTransport.hardwareMask);
  TEST_ASSERT_TRUE(mux.isInitialized());
  TEST_ASSERT_FALSE(mux.channelMaskObservation().verified());
}

void test_recover_failure_never_restores_or_retries() {
  gTransport.reset(0x40);
  Driver mux;
  beginOk(mux);
  gTransport.clearHistory();
  TEST_ASSERT_TRUE(gTransport.pushError(TransportErr::BUS, 6, true));

  assertStatus(mux.recover(), Err::I2C_BUS, 6);
  TEST_ASSERT_EQUAL_UINT32(1,
                           static_cast<uint32_t>(gTransport.callCount()));
  assertWriteCall(gTransport.call(0), 0x00);
  TEST_ASSERT_EQUAL_HEX8(0x00, gTransport.hardwareMask);
  TEST_ASSERT_FALSE(mux.channelMaskObservation().known());
  TEST_ASSERT_EQUAL_UINT32(1, mux.totalFailures());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DriverState::DEGRADED),
                        static_cast<int>(mux.state()));
}

void test_hard_reset_requires_callback_without_hidden_io() {
  Driver mux;
  beginOk(mux);
  gTransport.clearHistory();
  assertStatus(mux.hardReset(), Err::UNSUPPORTED);
  TEST_ASSERT_EQUAL_UINT32(0,
                           static_cast<uint32_t>(gTransport.callCount()));
}

void test_hard_reset_callback_failure_is_terminal_and_indeterminate() {
  gTransport.reset(0x80);
  Driver mux;
  beginOk(mux, makeConfig(true));
  gTransport.clearHistory();
  gReset.result = Status::Error(Err::TIMEOUT, "reset timeout", 17);
  gReset.applyEffect = true;
  gReset.appliedMask = 0x00;

  assertStatus(mux.hardReset(), Err::TIMEOUT, 17);
  TEST_ASSERT_EQUAL_INT(1, gReset.calls);
  TEST_ASSERT_EQUAL_UINT32(13, gReset.timeoutSeen);
  TEST_ASSERT_EQUAL_UINT32(0,
                           static_cast<uint32_t>(gTransport.callCount()));
  TEST_ASSERT_EQUAL_HEX8(0x00, gTransport.hardwareMask);
  TEST_ASSERT_FALSE(mux.channelMaskObservation().known());

  gReset = ResetHarness{};
  gReset.transport = &gTransport;
  gReset.result = Status::Error(Err::RESET_ERROR, "reset GPIO error", 18);
  gReset.applyEffect = false;
  assertStatus(mux.hardReset(), Err::RESET_ERROR, 18);
  TEST_ASSERT_EQUAL_INT(1, gReset.calls);
  TEST_ASSERT_EQUAL_UINT32(0,
                           static_cast<uint32_t>(gTransport.callCount()));
  TEST_ASSERT_FALSE(mux.channelMaskObservation().known());

  gReset = ResetHarness{};
  gReset.transport = &gTransport;
  gReset.result = Status::Error(Err::IN_PROGRESS, "invalid async reset");
  gReset.applyEffect = false;
  assertStatus(mux.hardReset(), Err::INVALID_CONFIG);
  TEST_ASSERT_EQUAL_INT(1, gReset.calls);
  TEST_ASSERT_EQUAL_UINT32(0,
                           static_cast<uint32_t>(gTransport.callCount()));
  TEST_ASSERT_FALSE(mux.channelMaskObservation().known());
}

void test_hard_reset_exact_zero_is_verified_without_restore() {
  gTransport.reset(0x80);
  Driver mux;
  beginOk(mux, makeConfig(true));
  TEST_ASSERT_TRUE(
      mux.writeChannelMask(ChannelMask::fromRaw(0x80)).ok());
  gTransport.clearHistory();
  gReset.appliedMask = 0x00;

  TEST_ASSERT_TRUE(mux.hardReset().ok());
  TEST_ASSERT_EQUAL_INT(1, gReset.calls);
  TEST_ASSERT_EQUAL_UINT32(1,
                           static_cast<uint32_t>(gTransport.callCount()));
  assertReadCall(gTransport.call(0));
  TEST_ASSERT_EQUAL_HEX8(0x00, gTransport.hardwareMask);
  TEST_ASSERT_TRUE(mux.channelMaskObservation().verified());
  TEST_ASSERT_EQUAL_HEX8(0x00,
                         mux.channelMaskObservation().mask.raw());
}

void test_hard_reset_mismatch_retains_truthful_readback() {
  gTransport.reset(0x01);
  Driver mux;
  beginOk(mux, makeConfig(true));
  gTransport.clearHistory();
  gReset.appliedMask = 0xA5;

  assertStatus(mux.hardReset(), Err::RESET_STATE_MISMATCH, 0xA5);
  TEST_ASSERT_EQUAL_UINT32(1,
                           static_cast<uint32_t>(gTransport.callCount()));
  const auto observation = mux.channelMaskObservation();
  TEST_ASSERT_TRUE(observation.verified());
  TEST_ASSERT_EQUAL_HEX8(0xA5, observation.mask.raw());
}

void test_hard_reset_read_failure_is_not_retried() {
  Driver mux;
  beginOk(mux, makeConfig(true));
  gTransport.clearHistory();
  TEST_ASSERT_TRUE(gTransport.pushError(TransportErr::TIMEOUT, 21));

  assertStatus(mux.hardReset(), Err::I2C_TIMEOUT, 21);
  TEST_ASSERT_EQUAL_INT(1, gReset.calls);
  TEST_ASSERT_EQUAL_UINT32(1,
                           static_cast<uint32_t>(gTransport.callCount()));
  assertReadCall(gTransport.call(0));
  TEST_ASSERT_FALSE(mux.channelMaskObservation().known());
}

void test_settings_snapshot_is_io_free_and_truthful() {
  gTransport.reset(0x24);
  Driver mux;
  beginOk(mux, makeConfig(true));
  gTransport.clearHistory();
  TCA9548A::SettingsSnapshot snapshot;

  TEST_ASSERT_TRUE(mux.getSettings(snapshot).ok());
  TEST_ASSERT_EQUAL_UINT32(0,
                           static_cast<uint32_t>(gTransport.callCount()));
  TEST_ASSERT_TRUE(snapshot.bound);
  TEST_ASSERT_TRUE(snapshot.initialized);
  TEST_ASSERT_EQUAL_HEX8(0x72, snapshot.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(7, snapshot.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(13, snapshot.resetTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(3, snapshot.offlineThreshold);
  TEST_ASSERT_TRUE(snapshot.hasNowMsHook);
  TEST_ASSERT_TRUE(snapshot.hasHardReset);
  TEST_ASSERT_TRUE(snapshot.maskObservation.verified());
  TEST_ASSERT_EQUAL_HEX8(0x24, snapshot.maskObservation.mask.raw());
}

} // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_status_and_transport_helpers);
  RUN_TEST(test_address_helpers_cover_all_straps_and_boundaries);
  RUN_TEST(test_channel_mask_helpers_are_exact);
  RUN_TEST(test_config_defaults_are_bounded);
  RUN_TEST(test_begin_rejects_invalid_config_without_io);
  RUN_TEST(test_begin_accepts_every_valid_address_with_one_read);
  RUN_TEST(test_begin_records_readback_and_tracked_health);
  RUN_TEST(test_failed_begin_preserves_binding_and_exact_error);
  RUN_TEST(test_rebind_is_rejected_transactionally);
  RUN_TEST(test_end_is_bus_silent_and_rebinding_is_repeatable);
  RUN_TEST(test_lifetime_counters_survive_end_and_rebind);
  RUN_TEST(test_unbound_hardware_apis_do_no_work);
  RUN_TEST(test_select_channel_encodes_every_one_hot_value);
  RUN_TEST(test_invalid_channel_is_rejected_without_io_or_cache_change);
  RUN_TEST(test_write_masks_are_exact_one_byte_transactions);
  RUN_TEST(test_disable_all_is_one_safe_off_write);
  RUN_TEST(test_read_is_read_only_and_assigns_output_only_on_success);
  RUN_TEST(test_all_transport_errors_remain_distinct);
  RUN_TEST(test_ambiguous_failed_write_invalidates_and_read_reconciles);
  RUN_TEST(test_successful_write_requires_later_read_for_verification);
  RUN_TEST(test_explicit_external_invalidation_preserves_only_historical_byte);
  RUN_TEST(test_probe_updates_observation_but_not_health);
  RUN_TEST(test_offline_is_passive_and_success_recovers_health);
  RUN_TEST(test_health_timestamps_accept_clock_wrap_without_deadline_math);
  RUN_TEST(test_recover_is_one_safe_off_write_even_after_failed_begin);
  RUN_TEST(test_recover_failure_never_restores_or_retries);
  RUN_TEST(test_hard_reset_requires_callback_without_hidden_io);
  RUN_TEST(test_hard_reset_callback_failure_is_terminal_and_indeterminate);
  RUN_TEST(test_hard_reset_exact_zero_is_verified_without_restore);
  RUN_TEST(test_hard_reset_mismatch_retains_truthful_readback);
  RUN_TEST(test_hard_reset_read_failure_is_not_retried);
  RUN_TEST(test_settings_snapshot_is_io_free_and_truthful);
  return UNITY_END();
}
