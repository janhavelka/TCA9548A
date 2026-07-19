/// @file main.cpp
/// @brief Bounded bring-up CLI for the TCA9548A typed primitive API.

#include <Arduino.h>
#include <Wire.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "examples/common/BoardConfig.h"
#include "examples/common/CommandHandler.h"
#include "examples/common/I2cScanner.h"
#include "examples/common/I2cTransport.h"
#include "examples/common/Log.h"
#include "TCA9548A/TCA9548A.h"
#include "TCA9548A/Version.h"

namespace {

TCA9548A::TCA9548A device;
TCA9548A::Config config;

const char* errorName(TCA9548A::Err error) {
  using TCA9548A::Err;
  switch (error) {
    case Err::OK: return "OK";
    case Err::NOT_INITIALIZED: return "NOT_INITIALIZED";
    case Err::INVALID_CONFIG: return "INVALID_CONFIG";
    case Err::I2C_ERROR: return "I2C_ERROR";
    case Err::TIMEOUT: return "TIMEOUT";
    case Err::INVALID_PARAM: return "INVALID_PARAM";
    case Err::DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
    case Err::UNSUPPORTED: return "UNSUPPORTED";
    case Err::I2C_NACK_ADDR: return "I2C_NACK_ADDR";
    case Err::I2C_NACK_DATA: return "I2C_NACK_DATA";
    case Err::I2C_TIMEOUT: return "I2C_TIMEOUT";
    case Err::I2C_BUS: return "I2C_BUS";
    case Err::BUSY: return "BUSY";
    case Err::IN_PROGRESS: return "IN_PROGRESS";
    case Err::RESET_STATE_MISMATCH: return "RESET_STATE_MISMATCH";
  }
  return "UNKNOWN";
}

const char* stateName(TCA9548A::DriverState state) {
  using TCA9548A::DriverState;
  switch (state) {
    case DriverState::UNINIT: return "UNINIT";
    case DriverState::READY: return "READY";
    case DriverState::DEGRADED: return "DEGRADED";
    case DriverState::OFFLINE: return "OFFLINE";
  }
  return "UNKNOWN";
}

const char* provenanceName(TCA9548A::MaskProvenance provenance) {
  using TCA9548A::MaskProvenance;
  switch (provenance) {
    case MaskProvenance::UNKNOWN: return "UNKNOWN";
    case MaskProvenance::WRITE_COMPLETED: return "WRITE_COMPLETED";
    case MaskProvenance::READBACK_OBSERVED: return "READBACK_OBSERVED";
  }
  return "UNKNOWN";
}

void printStatus(const TCA9548A::Status& status) {
  if (status.ok()) {
    Serial.print(F("OK"));
    return;
  }
  Serial.printf("%s (detail=%ld, %s)", errorName(status.code),
                static_cast<long>(status.detail), status.msg);
}

void printMask(TCA9548A::ChannelMask mask) {
  Serial.printf("0x%02X [", mask.raw());
  bool first = true;
  for (uint8_t index = 0; index < TCA9548A::cmd::NUM_CHANNELS; ++index) {
    const auto channel = static_cast<TCA9548A::Channel>(index);
    if (!mask.contains(channel)) {
      continue;
    }
    Serial.printf("%s%u", first ? "" : ",", static_cast<unsigned>(index));
    first = false;
  }
  if (first) {
    Serial.print(F("none"));
  }
  Serial.print(F("]"));
}

void printObservation() {
  const auto observation = device.channelMaskObservation();
  Serial.printf("Mask cache: %s known=%s verified=%s value=",
                provenanceName(observation.provenance),
                observation.known() ? "yes" : "no",
                observation.verified() ? "yes" : "no");
  printMask(observation.mask);
  Serial.println();
}

void printVersionInfo() {
  Serial.printf("%s=== Version Info ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);
  Serial.printf("  Library: %s\n", TCA9548A::VERSION);
  Serial.printf("  Full: %s\n", TCA9548A::VERSION_FULL);
  Serial.printf("  Code: %lu\n",
                static_cast<unsigned long>(TCA9548A::VERSION_CODE));
}

void printHealth() {
  Serial.printf("%s=== Driver Health ===%s\n", LOG_COLOR_CYAN,
                LOG_COLOR_RESET);
  Serial.printf("  State: %s (passive; never gates I2C)\n",
                stateName(device.state()));
  Serial.printf("  Bound: %s\n", device.isBound() ? "yes" : "no");
  Serial.printf("  Initialized: %s\n",
                device.isInitialized() ? "yes" : "no");
  Serial.printf("  Consecutive failures: %u\n",
                static_cast<unsigned>(device.consecutiveFailures()));
  Serial.printf("  Total success/failure: %lu/%lu\n",
                static_cast<unsigned long>(device.totalSuccess()),
                static_cast<unsigned long>(device.totalFailures()));
  Serial.printf("  Last OK/error ms: %lu/%lu\n",
                static_cast<unsigned long>(device.lastOkMs()),
                static_cast<unsigned long>(device.lastErrorMs()));
  Serial.printf("  Last error: %s\n", errorName(device.lastError().code));
  printObservation();
}

void printConfig() {
  TCA9548A::SettingsSnapshot snapshot;
  const auto status = device.getSettings(snapshot);
  Serial.printf("%s=== Configuration ===%s\n", LOG_COLOR_CYAN,
                LOG_COLOR_RESET);
  Serial.printf("  I2C address: 0x%02X\n", config.i2cAddress);
  Serial.printf("  I2C timeout: %lu ms\n",
                static_cast<unsigned long>(config.i2cTimeoutMs));
  Serial.printf("  RESET timeout: %lu ms\n",
                static_cast<unsigned long>(config.resetTimeoutMs));
  Serial.printf("  RESET callback: %s\n",
                config.hardReset != nullptr ? "configured" : "not configured");
  Serial.printf("  Offline threshold: %u (diagnostic only)\n",
                static_cast<unsigned>(config.offlineThreshold));
  Serial.print(F("  Snapshot: "));
  printStatus(status);
  Serial.println();
}

void printHelp() {
  Serial.printf("%s=== TCA9548A CLI Help ===%s\n", LOG_COLOR_CYAN,
                LOG_COLOR_RESET);
  Serial.println(F("  version / ver                  Version information"));
  Serial.println(F("  cfg                            Bound configuration"));
  Serial.println(F("  health / drv / state           Passive diagnostics"));
  Serial.println(F("  read / dump                    Read and verify mask"));
  Serial.println(F("  select <0-7>                   Select one channel"));
  Serial.println(F("  mask <0-255>                   Write an arbitrary mask"));
  Serial.println(F("  off                            Disable all channels"));
  Serial.println(F("  probe                          Raw diagnostic read"));
  Serial.println(F("  recover                        One safe-off write"));
  Serial.println(F("  reset / hardreset              RESET then verify 0x00"));
  Serial.println(F("  begin / end                    Bind+probe / bus-silent unbind"));
  Serial.println(F("  scan                           Upstream address scan"));
  Serial.println(F("  stress <1-1000>                Bounded select sample"));
  Serial.println(F("  stress_mix <1-1000>            Bounded primitive mix"));
  Serial.println(F("  selftest                       Live primitive contract checks"));
  Serial.println(F("  hil [dry|parser|run|run reset] HIL contract entry point"));
  Serial.println(F("  help                           This help"));
}

uint32_t nowMs(void*) {
  return millis();
}

TCA9548A::Status pulseReset(uint32_t timeoutMs, void*) {
  if (board::TCA_RESET < 0) {
    return TCA9548A::Status::Error(TCA9548A::Err::UNSUPPORTED,
                                  "RESET pin is not configured");
  }
  if (timeoutMs == 0U) {
    return TCA9548A::Status::Error(TCA9548A::Err::TIMEOUT,
                                  "RESET timeout is zero");
  }

  digitalWrite(board::TCA_RESET, LOW);
  delayMicroseconds(1U);
  digitalWrite(board::TCA_RESET, HIGH);
  return TCA9548A::Status::Ok();
}

void configureDriver() {
  config.i2cWrite = transport::wireWrite;
  config.i2cWriteRead = transport::wireWriteRead;
  config.i2cUser = &Wire;
  config.nowMs = nowMs;
  config.i2cAddress = TCA9548A::cmd::DEFAULT_ADDRESS;
  config.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
  config.resetTimeoutMs = board::TCA_RESET_TIMEOUT_MS;
  config.offlineThreshold = 5;

  if (board::TCA_RESET >= 0) {
    pinMode(board::TCA_RESET, OUTPUT);
    digitalWrite(board::TCA_RESET, HIGH);
    config.hardReset = pulseReset;
  }
}

void beginDriver() {
  const auto status = device.begin(config);
  Serial.print(F("begin: "));
  printStatus(status);
  Serial.printf(" (bound=%s)\n", device.isBound() ? "yes" : "no");
}

bool parseUnsignedArgument(const char* command, const char* prefix,
                           unsigned long maximum, unsigned long& output) {
  const size_t prefixLength = std::strlen(prefix);
  if (std::strncmp(command, prefix, prefixLength) != 0 ||
      command[prefixLength] != ' ') {
    return false;
  }

  const char* text = command + prefixLength + 1U;
  if (*text == '\0') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long value = std::strtoul(text, &end, 0);
  if (errno != 0 || end == text || *end != '\0' || value > maximum) {
    return false;
  }
  output = value;
  return true;
}

bool restoreVerified(TCA9548A::ChannelMask desired) {
  auto status = device.writeChannelMask(desired);
  if (!status.ok()) {
    Serial.print(F("restore write: "));
    printStatus(status);
    Serial.println();
    return false;
  }

  TCA9548A::ChannelMask observed;
  status = device.readChannelMask(observed);
  if (!status.ok() || observed.raw() != desired.raw()) {
    Serial.print(F("restore readback: "));
    printStatus(status);
    if (status.ok()) {
      Serial.print(F(" expected="));
      printMask(desired);
      Serial.print(F(" observed="));
      printMask(observed);
    }
    Serial.println();
    return false;
  }
  return true;
}

struct HilCounts {
  uint16_t passed = 0;
  uint16_t failed = 0;
  uint16_t skipped = 0;
};

void reportCheck(HilCounts& counts, const char* name, bool passed,
                 const char* detail = "") {
  if (passed) {
    ++counts.passed;
  } else {
    ++counts.failed;
  }
  Serial.printf("  [%s] %s", passed ? "PASS" : "FAIL", name);
  if (detail != nullptr && detail[0] != '\0') {
    Serial.printf(" - %s", detail);
  }
  Serial.println();
}

void reportSkip(HilCounts& counts, const char* name, const char* detail) {
  ++counts.skipped;
  Serial.printf("  [SKIP] %s - %s\n", name, detail);
}

void printHilResult(const HilCounts& counts) {
  Serial.printf("HIL result: pass=%u fail=%u skip=%u\n",
                static_cast<unsigned>(counts.passed),
                static_cast<unsigned>(counts.failed),
                static_cast<unsigned>(counts.skipped));
}

void runHil(bool dryRun, bool includeReset) {
  Serial.printf("%s=== TCA9548A HIL %s ===%s\n", LOG_COLOR_CYAN,
                dryRun ? "DRY-RUN" : "RUN", LOG_COLOR_RESET);
  HilCounts counts;

  reportCheck(counts, "typed ChannelMask is one byte",
              sizeof(TCA9548A::ChannelMask) == sizeof(uint8_t));
  reportCheck(counts, "address helper",
              TCA9548A::cmd::addressFromPins(false, false, false) == 0x70U &&
                  TCA9548A::cmd::addressFromPins(true, true, true) == 0x77U);
  reportCheck(counts, "version", TCA9548A::VERSION[0] != '\0',
              TCA9548A::VERSION);

  if (dryRun) {
    reportSkip(counts, "probe", "dry-run");
    reportSkip(counts, "mask I/O", "dry-run");
    reportSkip(counts, "hardReset",
               includeReset ? "dry-run" : "not requested");
    printHilResult(counts);
    return;
  }

  const uint32_t successBeforeProbe = device.totalSuccess();
  const uint32_t failureBeforeProbe = device.totalFailures();
  auto status = device.probe();
  reportCheck(counts, "probe", status.ok(), errorName(status.code));
  reportCheck(counts, "probe no-health-side-effects",
              device.totalSuccess() == successBeforeProbe &&
                  device.totalFailures() == failureBeforeProbe);

  TCA9548A::ChannelMask original;
  status = device.readChannelMask(original);
  if (!status.ok()) {
    reportCheck(counts, "capture original mask", false,
                errorName(status.code));
    printHilResult(counts);
    return;
  }
  reportCheck(counts, "capture verified original mask",
              device.channelMaskObservation().verified());

  status = device.disableAll();
  reportCheck(counts, "disableAll write", status.ok(), errorName(status.code));
  TCA9548A::ChannelMask observed;
  status = device.readChannelMask(observed);
  reportCheck(counts, "disableAll readback",
              status.ok() && observed.isNone(), errorName(status.code));

  status = device.selectChannel(TCA9548A::Channel::CH3);
  reportCheck(counts, "select CH3", status.ok(), errorName(status.code));
  status = device.readChannelMask(observed);
  reportCheck(counts, "select CH3 readback",
              status.ok() &&
                  observed.raw() ==
                      TCA9548A::ChannelMask::one(TCA9548A::Channel::CH3).raw(),
              errorName(status.code));

  status = device.writeChannelMask(TCA9548A::ChannelMask::fromRaw(0xA5U));
  reportCheck(counts, "write mask 0xA5", status.ok(), errorName(status.code));
  status = device.readChannelMask(observed);
  reportCheck(counts, "read mask 0xA5",
              status.ok() && observed.raw() == 0xA5U,
              errorName(status.code));

  status = device.recover();
  reportCheck(counts, "recover safe-off write", status.ok(),
              errorName(status.code));
  status = device.readChannelMask(observed);
  reportCheck(counts, "recover readback 0x00",
              status.ok() && observed.isNone(), errorName(status.code));

  if (includeReset) {
    if (config.hardReset == nullptr) {
      reportCheck(counts, "hardReset", false, "callback not configured");
    } else {
      status = device.hardReset();
      reportCheck(counts, "hardReset exact-zero verification", status.ok(),
                  errorName(status.code));
      const auto resetObservation = device.channelMaskObservation();
      reportCheck(counts, "hardReset leaves verified all-off",
                  resetObservation.verified() &&
                      resetObservation.mask.isNone());
    }
  } else {
    reportSkip(counts, "hardReset", "use 'hil run reset' to include RESET");
  }

  reportCheck(counts, "restore original mask", restoreVerified(original));
  printHilResult(counts);
}

void runStress(unsigned long count, bool mixed) {
  TCA9548A::ChannelMask original;
  auto status = device.readChannelMask(original);
  if (!status.ok()) {
    Serial.print(F("stress baseline: "));
    printStatus(status);
    Serial.println();
    return;
  }

  const uint32_t successesBefore = device.totalSuccess();
  const uint32_t failuresBefore = device.totalFailures();
  const uint32_t startedMs = millis();
  unsigned long completed = 0;

  for (; completed < count; ++completed) {
    if (!mixed) {
      status = device.selectChannel(
          static_cast<TCA9548A::Channel>(completed % 8U));
    } else {
      switch (completed % 4U) {
        case 0:
          status = device.selectChannel(
              static_cast<TCA9548A::Channel>(completed % 8U));
          break;
        case 1:
          status = device.writeChannelMask(TCA9548A::ChannelMask::fromRaw(
              static_cast<uint8_t>(completed)));
          break;
        case 2: {
          TCA9548A::ChannelMask observed;
          status = device.readChannelMask(observed);
          break;
        }
        default: status = device.disableAll(); break;
      }
    }
    if (!status.ok()) {
      break;
    }
  }

  const bool restored = restoreVerified(original);
  const uint32_t durationMs = millis() - startedMs;
  if (mixed) {
    Serial.println(F("=== stress_mix summary ==="));
  }
  Serial.printf("Stress results: completed=%lu requested=%lu status=%s restore=%s\n",
                completed, count, errorName(status.code),
                restored ? "OK" : "FAILED");
  Serial.printf("Duration: %lu ms\n", static_cast<unsigned long>(durationMs));
  Serial.printf("Health delta: success=%lu failure=%lu\n",
                static_cast<unsigned long>(device.totalSuccess() -
                                           successesBefore),
                static_cast<unsigned long>(device.totalFailures() -
                                           failuresBefore));
  if (!restored) {
    Serial.println(F("  [FAIL] original mask restore was not verified"));
  }
}

void processCommand(const char* command) {
  if (std::strcmp(command, "help") == 0) {
    printHelp();
  } else if (std::strcmp(command, "version") == 0 ||
             std::strcmp(command, "ver") == 0) {
    printVersionInfo();
  } else if (std::strcmp(command, "cfg") == 0) {
    printConfig();
  } else if (std::strcmp(command, "health") == 0 ||
             std::strcmp(command, "drv") == 0 ||
             std::strcmp(command, "state") == 0) {
    printHealth();
  } else if (std::strcmp(command, "read") == 0 ||
             std::strcmp(command, "dump") == 0) {
    TCA9548A::ChannelMask mask;
    const auto status = device.readChannelMask(mask);
    Serial.print(F("read: "));
    printStatus(status);
    if (status.ok()) {
      Serial.print(F(" mask="));
      printMask(mask);
    }
    Serial.println();
  } else if (std::strcmp(command, "off") == 0) {
    const auto status = device.disableAll();
    Serial.print(F("off: "));
    printStatus(status);
    Serial.println();
  } else if (std::strcmp(command, "probe") == 0) {
    const auto status = device.probe();
    Serial.print(F("probe: "));
    printStatus(status);
    Serial.println();
  } else if (std::strcmp(command, "recover") == 0) {
    const auto status = device.recover();
    Serial.print(F("recover (safe-off write): "));
    printStatus(status);
    Serial.println();
  } else if (std::strcmp(command, "reset") == 0 ||
             std::strcmp(command, "hardreset") == 0) {
    const auto status = device.hardReset();
    Serial.print(F("hardreset: "));
    printStatus(status);
    Serial.println();
    printObservation();
  } else if (std::strcmp(command, "begin") == 0) {
    beginDriver();
  } else if (std::strcmp(command, "end") == 0) {
    device.end();
    Serial.println(F("end: OK (no bus I/O)"));
  } else if (std::strcmp(command, "scan") == 0) {
    (void)i2c::scan();
  } else if (std::strcmp(command, "selftest") == 0 ||
             std::strcmp(command, "hil run") == 0) {
    runHil(false, false);
  } else if (std::strcmp(command, "hil run reset") == 0) {
    runHil(false, true);
  } else if (std::strcmp(command, "hil dry") == 0 ||
             std::strcmp(command, "hil parser") == 0 ||
             std::strcmp(command, "hil") == 0) {
    runHil(true, false);
  } else {
    unsigned long value = 0;
    if (parseUnsignedArgument(command, "select", 7U, value)) {
      const auto status = device.selectChannel(
          static_cast<TCA9548A::Channel>(value));
      Serial.printf("select %lu: ", value);
      printStatus(status);
      Serial.println();
    } else if (parseUnsignedArgument(command, "mask", 255U, value)) {
      const auto status = device.writeChannelMask(
          TCA9548A::ChannelMask::fromRaw(static_cast<uint8_t>(value)));
      Serial.printf("mask 0x%02lX: ", value);
      printStatus(status);
      Serial.println();
    } else if (parseUnsignedArgument(command, "stress_mix", 1000U, value) &&
               value > 0U) {
      runStress(value, true);
    } else if (parseUnsignedArgument(command, "stress", 1000U, value) &&
               value > 0U) {
      runStress(value, false);
    } else {
      LOGE("Unknown or invalid command: %s", command);
    }
  }
}

}  // namespace

void setup() {
  log_begin(115200);
  delay(1000);
  Serial.println(F("\n============================="));
  Serial.println(F("  TCA9548A Bring-up CLI"));
  Serial.println(F("============================="));

  printVersionInfo();
  if (!board::initI2c()) {
    LOGE("I2C controller initialization failed");
  }
  configureDriver();
  beginDriver();
  printHelp();
  Serial.print(F("\n> "));
}

void loop() {
  device.tick(millis());

  char command[128];
  if (cmd::readLine(command, sizeof(command))) {
    processCommand(command);
    Serial.print(F("\n> "));
  }
}
