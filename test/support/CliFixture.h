#pragma once

// Stateful host-only fixture for the actual example CLI and driver. These
// containers deliberately stay in test code; embedded paths use fixed storage.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../../examples/common/Tca9548aCli.h"

namespace test_support {

struct CliTransfer {
  tca9548a_cli::TransferStats::Kind kind;
  uint8_t address;
  uint8_t value;
  uint32_t timeoutMs;
};

struct CliFixture {
  using Kind = tca9548a_cli::TransferStats::Kind;

  TCA9548A::TCA9548A device;
  TCA9548A::Config config;
  tca9548a_cli::Cli cli{device, config};
  std::string output;
  std::vector<CliTransfer> transfers;
  tca9548a_cli::TransferStats stats;
  uint8_t strapAddress = 0x70;
  uint8_t ownerAddress = 0x70;
  uint8_t hardwareMask = 0;
  uint32_t clockMs = 100;
  uint32_t prepareCalls = 0;
  uint32_t resetCalls = 0;
  uint32_t resetTimeoutSeen = 0;
  bool ready = true;
  bool receiveFaulted = false;
  bool retireReadFailures = true;
  bool resetChangesMask = true;
  bool shapeValid = true;
  // One-based physical attempt indices; zero leaves injection disabled.
  size_t failAt = 0;
  size_t corruptReadAt = 0;
  uint8_t corruptReadValue = 0;
  TCA9548A::TransportErr failure = TCA9548A::TransportErr::TIMEOUT;
  TCA9548A::Status prepareResult = TCA9548A::Status::Ok();
  TCA9548A::Status resetResult = TCA9548A::Status::Ok();

  explicit CliFixture(bool withReset = true) {
    config.i2cWrite = write;
    config.i2cWriteRead = read;
    config.i2cUser = this;
    config.nowMs = now;
    config.timeUser = this;
    if (withReset) {
      config.hardReset = reset;
      config.resetUser = this;
    }
    tca9548a_cli::Platform platform;
    platform.writeText = print;
    platform.nowMs = now;
    platform.printVersion = version;
    platform.busInfo = busInfo;
    platform.transferStats = transferStats;
    platform.resetTransferStats = resetTransferStats;
    platform.prepareAddress = prepare;
    platform.probeAddress = probe;
    platform.user = this;
    cli.setup(platform);
  }

  void command(const char* text) { cli.processCommand(text); }

  void line(const std::string& text) {
    for (const char value : text) cli.feed(value);
    cli.feed('\n');
  }

  void clearHistory() {
    output.clear();
    transfers.clear();
    failAt = 0;
    corruptReadAt = 0;
  }

  size_t count(Kind kind) const {
    size_t result = 0;
    for (const auto& transfer : transfers) {
      if (transfer.kind == kind) ++result;
    }
    return result;
  }

  bool contains(const char* text) const {
    return output.find(text) != std::string::npos;
  }

  TCA9548A::TransportStatus attempt(Kind kind, uint8_t address, uint8_t value,
                                   uint32_t timeoutMs) {
    transfers.push_back({kind, address, value, timeoutMs});
    const auto error = transfers.size() == failAt ? failure :
        (address == strapAddress ? TCA9548A::TransportErr::OK :
                                  TCA9548A::TransportErr::NACK_ADDR);
    stats.record(kind, error == TCA9548A::TransportErr::OK);
    return error == TCA9548A::TransportErr::OK ?
        TCA9548A::TransportStatus::Ok() :
        TCA9548A::TransportStatus::Error(error, -77);
  }

  static void print(const char* text, void* user) {
    static_cast<CliFixture*>(user)->output += text;
  }
  static uint32_t now(void* user) {
    return static_cast<CliFixture*>(user)->clockMs;
  }
  static void version(void* user) {
    print("  Host fixture: actual CLI and driver\n", user);
  }
  static tca9548a_cli::BusInfo busInfo(void* user) {
    const auto& fixture = *static_cast<CliFixture*>(user);
    tca9548a_cli::BusInfo info;
    info.ready = fixture.ready;
    info.receiveFaulted = fixture.receiveFaulted;
    info.sdaPin = 8;
    info.sclPin = 9;
    info.resetPin = fixture.config.hardReset == nullptr ? -1 : 10;
    info.frequencyHz = 400000;
    info.address = fixture.ownerAddress;
    return info;
  }
  static tca9548a_cli::TransferStats transferStats(void* user) {
    return static_cast<CliFixture*>(user)->stats;
  }
  static void resetTransferStats(void* user) {
    static_cast<CliFixture*>(user)->stats = {};
  }
  static TCA9548A::Status prepare(uint8_t address, void* user) {
    auto& fixture = *static_cast<CliFixture*>(user);
    ++fixture.prepareCalls;
    if (fixture.receiveFaulted) {
      return TCA9548A::Status::Error(TCA9548A::Err::I2C_BUS,
                                    "Owner requires restart", -19);
    }
    if (fixture.prepareResult.ok()) fixture.ownerAddress = address;
    return fixture.prepareResult;
  }
  static TCA9548A::Status probe(uint8_t address, uint32_t timeoutMs, void* user) {
    auto& fixture = *static_cast<CliFixture*>(user);
    if (fixture.receiveFaulted || !fixture.ready) {
      return TCA9548A::Status::Error(TCA9548A::Err::I2C_BUS,
                                    "Owner requires restart", -19);
    }
    const auto result = fixture.attempt(Kind::PROBE, address, 0, timeoutMs);
    if (result.ok()) return TCA9548A::Status::Ok();
    return TCA9548A::Status::Error(
        result.code == TCA9548A::TransportErr::NACK_ADDR ?
            TCA9548A::Err::I2C_NACK_ADDR : TCA9548A::Err::I2C_TIMEOUT,
        "Address probe failed", result.detail);
  }
  static TCA9548A::TransportStatus write(uint8_t address, const uint8_t* data,
                                        size_t length, uint32_t timeoutMs,
                                        void* user) {
    auto& fixture = *static_cast<CliFixture*>(user);
    fixture.shapeValid &= address == fixture.ownerAddress && data != nullptr &&
        length == 1 && timeoutMs > 0 && timeoutMs <= 60000;
    if (fixture.receiveFaulted) {
      return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::BUS, -19);
    }
    const auto result = fixture.attempt(Kind::WRITE, address,
                                        data == nullptr ? 0 : data[0], timeoutMs);
    if (result.ok() && data != nullptr) fixture.hardwareMask = data[0];
    return result;
  }
  static TCA9548A::TransportStatus read(uint8_t address, const uint8_t* txData,
                                       size_t txLength, uint8_t* rxData,
                                       size_t rxLength, uint32_t timeoutMs,
                                       void* user) {
    auto& fixture = *static_cast<CliFixture*>(user);
    fixture.shapeValid &= address == fixture.ownerAddress && txData == nullptr &&
        txLength == 0 && rxData != nullptr && rxLength == 1 &&
        timeoutMs > 0 && timeoutMs <= 60000;
    if (fixture.receiveFaulted) {
      return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::BUS, -19);
    }
    const auto result = fixture.attempt(Kind::READ, address, fixture.hardwareMask,
                                        timeoutMs);
    if (result.ok() && rxData != nullptr) {
      rxData[0] = fixture.transfers.size() == fixture.corruptReadAt ?
          fixture.corruptReadValue : fixture.hardwareMask;
    } else if (fixture.retireReadFailures) {
      fixture.receiveFaulted = true;
    }
    return result;
  }
  static TCA9548A::Status reset(uint32_t timeoutMs, void* user) {
    auto& fixture = *static_cast<CliFixture*>(user);
    ++fixture.resetCalls;
    fixture.resetTimeoutSeen = timeoutMs;
    if (fixture.resetResult.ok() && fixture.resetChangesMask) {
      fixture.hardwareMask = 0;
    }
    return fixture.resetResult;
  }
};

}  // namespace test_support
