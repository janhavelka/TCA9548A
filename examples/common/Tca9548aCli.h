#pragma once

// Example-only terminal logic. One application task owns the CLI and I2C bus.
#include <cstddef>
#include <cstdint>

#include "CliLineBuffer.h"
#include "ConsoleOutput.h"
#include "TCA9548A/TCA9548A.h"

namespace tca9548a_cli {

struct TransferStats {
  uint32_t attempts = 0;
  uint32_t successes = 0;
  uint32_t failures = 0;
  uint32_t reads = 0;
  uint32_t writes = 0;
  uint32_t probes = 0;
  enum class Kind : uint8_t { READ, WRITE, PROBE };
  // Call only after an actual controller attempt, including diagnostic probes.
  void record(Kind kind, bool ok);
};

struct BusInfo {
  bool ready = false;
  bool receiveFaulted = false;
  int sdaPin = -1;
  int sclPin = -1;
  int resetPin = -1;
  uint32_t frequencyHz = 0;
  uint8_t address = TCA9548A::cmd::DEFAULT_ADDRESS;
};

struct Platform {
  // Text is borrowed only until the callback returns; each piece is <=383 bytes.
  void (*writeText)(const char* text, void* user) = nullptr;
  uint32_t (*nowMs)(void* user) = nullptr;
  // Optional extra MCU/framework/build identity, printed after library version.
  void (*printVersion)(void* user) = nullptr;
  BusInfo (*busInfo)(void* user) = nullptr;
  TransferStats (*transferStats)(void* user) = nullptr;
  ConsoleStats (*consoleStats)(void* user) = nullptr;
  void (*resetTransferStats)(void* user) = nullptr;
  // Explicit owner retargeting before begin; must retain any receive-fault latch.
  TCA9548A::Status (*prepareAddress)(uint8_t address, void* user) = nullptr;
  // One timeout-bounded address-only attempt. I2C_NACK_ADDR means absent.
  TCA9548A::Status (*probeAddress)(uint8_t address, uint32_t timeoutMs,
                                  void* user) = nullptr;
  void* user = nullptr;
};

class Cli {
 public:
  Cli(TCA9548A::TCA9548A& device, TCA9548A::Config& stagedConfig)
      : _device(device), _config(stagedConfig) {}
  // Bus-silent. The platform and configuration outlive the CLI.
  void setup(const Platform& platform);
  void feed(char value);
  // Discard any partial line and all following bytes through its next CR/LF.
  // Call after observable RX loss or disconnect before accepting more input.
  void discardInput();
  void processCommand(const char* command);
  // At most one scheduled I2C primitive per tick; immediate commands are bounded.
  void tick();
  void printHelp(const char* topic = nullptr);
  void printPrompt();
  void beginDriver();
  bool active() const;

 private:
  enum class Run : uint8_t { NONE, SCAN, DISCOVER, WATCH, STRESS, MIX, SWEEP, SOAK };
  struct Session {
    Run kind = Run::NONE;
    bool available = false;
    bool running = false;
    bool cancelled = false;
    bool restoring = false;
    bool captured = false;
    uint8_t phase = 0;
    uint16_t address = 0;
    uint16_t found = 0;
    uint16_t scanErrors = 0;
    uint8_t targetAddress = 0;
    uint8_t candidateTested = 0;
    uint8_t candidateAcks = 0;
    uint8_t candidateErrors = 0;
    uint32_t target = 0;
    uint32_t completed = 0;
    uint32_t intervalMs = 0;
    uint32_t nextMs = 0;
    uint32_t startedMs = 0;
    uint32_t endedMs = 0;
    uint32_t durationMs = 0;
    uint32_t successBefore = 0;
    uint32_t failureBefore = 0;
    uint32_t successAfter = 0;
    uint32_t failureAfter = 0;
    uint32_t transferSuccesses = 0;
    uint32_t transferFailures = 0;
    TCA9548A::ChannelMask original{};
    TCA9548A::ChannelMask expected{};
    TCA9548A::Status primary = TCA9548A::Status::Ok();
    TCA9548A::Status cleanup = TCA9548A::Status::Ok();
  };
  struct HilCounts { unsigned passed = 0; unsigned failed = 0; unsigned skipped = 0; };

  void print(const char* format, ...);
  const char* color(const char* escape) const;
  void section(const char* title);
  void helpItem(const char* command, const char* description);
  void status(TCA9548A::Status value);
  void result(const char* operation, TCA9548A::Status value);
  void printMask(TCA9548A::ChannelMask mask);
  void printObservation();
  void printVersion();
  void printHealth();
  void printConfig();
  void printBus();
  void printConsole();
  void printTransferStats();
  void printRunStats();
  void invalid(const char* usage);
  uint32_t now() const;
  BusInfo busInfo() const;
  TCA9548A::Status verifyMask(TCA9548A::ChannelMask expected);
  TCA9548A::Status restoreMask(TCA9548A::ChannelMask mask);
  void runHil(bool dry, bool reset, bool parser);
  void runHilMaskChecks(HilCounts& counts, bool reset);
  void check(HilCounts& counts, const char* label, bool passed, const char* detail = "");
  void skip(HilCounts& counts, const char* label, const char* detail);
  void start(Run kind, uint32_t count, uint32_t intervalMs = 0);
  void stop();
  void finish();
  void fail(TCA9548A::Status value);
  TCA9548A::Status recordTransfer(TCA9548A::Status value);
  void tickCleanup();
  void tickScan();
  void tickMasks();
  const char* runName() const;

  TCA9548A::TCA9548A& _device;
  TCA9548A::Config& _config;
  Platform _platform{};
  cli_shell::FixedLineBuffer _lineBuffer{};
  Session _session{};
  bool _color = true;
  bool _verbose = false;
  bool _inCommand = false;
};

}  // namespace tca9548a_cli
