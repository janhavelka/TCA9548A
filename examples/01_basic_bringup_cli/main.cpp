/// @file main.cpp
/// @brief Bounded bring-up CLI for the TCA9548A typed primitive API.

#include <Arduino.h>
#include <Wire.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "examples/common/BoardConfig.h"
#include "examples/common/CliShell.h"
#include "examples/common/CliStyle.h"
#include "examples/common/I2cScanner.h"
#include "examples/common/I2cTransport.h"
#include "examples/common/Log.h"
#include "TCA9548A/TCA9548A.h"
#include "TCA9548A/Version.h"

namespace {

TCA9548A::TCA9548A device;
TCA9548A::Config config;
bool i2cReady = false;

bool safeOffVerified();
using TCA9548A::errorName;
using TCA9548A::driverStateName;
using TCA9548A::maskProvenanceName;

void printStatus(const TCA9548A::Status& status) {
  Serial.printf("%s%s%s", cli::resultColor(status.ok()),
                errorName(status.code), LOG_COLOR_RESET);
  if (!status.ok()) {
    Serial.printf(" (detail=%ld, %s)", static_cast<long>(status.detail),
                  status.msg);
  }
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
                maskProvenanceName(observation.provenance),
                observation.known() ? "yes" : "no",
                observation.verified() ? "yes" : "no");
  printMask(observation.mask);
  Serial.println();
}

void printVersionInfo() {
  cli::printSection("Version Info");
  Serial.printf("  MCU: %s rev %u, flash %lu bytes, PSRAM %s (%lu bytes)\n",
                ESP.getChipModel(),
                static_cast<unsigned>(ESP.getChipRevision()),
                static_cast<unsigned long>(ESP.getFlashChipSize()),
                psramFound() ? "ready" : "not available",
                static_cast<unsigned long>(ESP.getPsramSize()));
  Serial.printf("  Arduino-ESP32: %s\n", ESP.getCoreVersion());
  Serial.printf("  ESP-IDF: %s\n", ESP.getSdkVersion());
  Serial.printf("  Library: %s\n", TCA9548A::VERSION);
  Serial.printf("  Full: %s\n", TCA9548A::VERSION_FULL);
  Serial.printf("  Code: %lu\n",
                static_cast<unsigned long>(TCA9548A::VERSION_CODE));
}

void printHealth() {
  cli::printSection("Driver Health");
  Serial.printf("  State: %s%s%s (passive; never gates I2C)\n",
                cli::stateColor(device.isInitialized(), device.isOnline(),
                                device.consecutiveFailures()),
                driverStateName(device.state()), LOG_COLOR_RESET);
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
  cli::printSection("Configuration");
  Serial.printf("  Bound: %s\n", snapshot.bound ? "yes" : "no");
  Serial.printf("  Initialized: %s\n", snapshot.initialized ? "yes" : "no");
  Serial.printf("  I2C address: 0x%02X\n", snapshot.i2cAddress);
  Serial.printf("  I2C timeout: %lu ms\n",
                static_cast<unsigned long>(snapshot.i2cTimeoutMs));
  Serial.printf("  RESET timeout: %lu ms\n",
                static_cast<unsigned long>(snapshot.resetTimeoutMs));
  Serial.printf("  nowMs hook: %s\n",
                snapshot.hasNowMsHook ? "configured" : "not configured");
  Serial.printf("  RESET callback: %s\n",
                snapshot.hasHardReset ? "configured" : "not configured");
  Serial.printf("  Offline threshold: %u (diagnostic only)\n",
                static_cast<unsigned>(snapshot.offlineThreshold));
  Serial.print(F("  Snapshot: "));
  printStatus(status);
  Serial.println();
}

void printHelp() {
  cli::printSection("TCA9548A CLI Help");
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
  Serial.println(F("  invalidate                     Mark cached mask unknown"));
  Serial.println(F("  begin / end                    Bind+probe / bus-silent unbind"));
  Serial.println(F("  scan                           Maintenance: 126 probes"));
  Serial.println(F("  stress <1-1000>                Maintenance select sample"));
  Serial.println(F("  stress_mix <1-1000>            Maintenance primitive mix"));
  Serial.println(F("  selftest                       Live primitive contract checks"));
  Serial.println(F("  hil [dry|parser|run|run reset] HIL contract entry point"));
  Serial.println(F("  help / ?                       This help"));
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
  if (!i2cReady) {
    Serial.println(F("begin: NOT_INITIALIZED (I2C controller unavailable)"));
    return;
  }

  const bool wasBound = device.isBound();
  const auto status = device.begin(config);
  Serial.print(F("begin: "));
  printStatus(status);
  Serial.printf(" (bound=%s)\n", device.isBound() ? "yes" : "no");
  if (!wasBound && device.isBound()) {
    Serial.printf("startup safe-off: %s\n",
                  safeOffVerified() ? "OK (verified 0x00)" : "FAILED");
  }
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

bool safeOffVerified() {
  auto status = device.disableAll();
  if (!status.ok()) {
    Serial.print(F("safe-off write: "));
    printStatus(status);
    Serial.println();
    return false;
  }

  TCA9548A::ChannelMask observed;
  status = device.readChannelMask(observed);
  if (!status.ok() || !observed.isNone()) {
    Serial.print(F("safe-off readback: "));
    printStatus(status);
    if (status.ok()) {
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
  Serial.printf("  [%s%s%s] %s", cli::resultColor(passed),
                passed ? "PASS" : "FAIL", LOG_COLOR_RESET, name);
  if (detail != nullptr && detail[0] != '\0') {
    Serial.printf(" - %s", detail);
  }
  Serial.println();
}

void reportSkip(HilCounts& counts, const char* name, const char* detail) {
  ++counts.skipped;
  Serial.printf("  [%sSKIP%s] %s - %s\n", LOG_COLOR_YELLOW,
                LOG_COLOR_RESET, name, detail);
}

void printHilResult(const HilCounts& counts) {
  Serial.printf("HIL result: pass=%u fail=%u skip=%u\n",
                static_cast<unsigned>(counts.passed),
                static_cast<unsigned>(counts.failed),
                static_cast<unsigned>(counts.skipped));
}

void finishHilSafe(HilCounts& counts) {
  reportCheck(counts, "final verified safe-off", safeOffVerified());
  printHilResult(counts);
}

void runHil(bool dryRun, bool includeReset) {
  char title[40];
  std::snprintf(title, sizeof(title), "TCA9548A HIL %s",
                dryRun ? "DRY-RUN" : "RUN");
  cli::printSection(title);
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
  const bool probeOk = status.ok();
  reportCheck(counts, "probe", probeOk, errorName(status.code));
  const bool probeHealthUnchanged =
      device.totalSuccess() == successBeforeProbe &&
      device.totalFailures() == failureBeforeProbe;
  reportCheck(counts, "probe no-health-side-effects", probeHealthUnchanged);
  if (!probeOk || !probeHealthUnchanged) {
    finishHilSafe(counts);
    return;
  }

  status = device.disableAll();
  const bool disableOk = status.ok();
  reportCheck(counts, "disableAll write", disableOk, errorName(status.code));
  if (!disableOk) {
    finishHilSafe(counts);
    return;
  }
  TCA9548A::ChannelMask observed;
  status = device.readChannelMask(observed);
  const bool disableVerified = status.ok() && observed.isNone();
  reportCheck(counts, "disableAll readback", disableVerified,
              errorName(status.code));
  if (!disableVerified) {
    finishHilSafe(counts);
    return;
  }

  status = device.selectChannel(TCA9548A::Channel::CH3);
  const bool selectOk = status.ok();
  reportCheck(counts, "select CH3", selectOk, errorName(status.code));
  if (!selectOk) {
    finishHilSafe(counts);
    return;
  }
  status = device.readChannelMask(observed);
  const bool selectVerified =
      status.ok() &&
      observed.raw() ==
          TCA9548A::ChannelMask::one(TCA9548A::Channel::CH3).raw();
  reportCheck(counts, "select CH3 readback", selectVerified,
              errorName(status.code));
  if (!selectVerified) {
    finishHilSafe(counts);
    return;
  }

  status = device.writeChannelMask(TCA9548A::ChannelMask::fromRaw(0xA5U));
  const bool maskWriteOk = status.ok();
  reportCheck(counts, "write mask 0xA5", maskWriteOk,
              errorName(status.code));
  if (!maskWriteOk) {
    finishHilSafe(counts);
    return;
  }
  status = device.readChannelMask(observed);
  const bool maskVerified = status.ok() && observed.raw() == 0xA5U;
  reportCheck(counts, "read mask 0xA5", maskVerified,
              errorName(status.code));
  if (!maskVerified) {
    finishHilSafe(counts);
    return;
  }

  status = device.recover();
  const bool recoverOk = status.ok();
  reportCheck(counts, "recover safe-off write", recoverOk,
              errorName(status.code));
  if (!recoverOk) {
    finishHilSafe(counts);
    return;
  }
  status = device.readChannelMask(observed);
  const bool recoverVerified = status.ok() && observed.isNone();
  reportCheck(counts, "recover readback 0x00", recoverVerified,
              errorName(status.code));
  if (!recoverVerified) {
    finishHilSafe(counts);
    return;
  }

  if (includeReset) {
    if (config.hardReset == nullptr) {
      reportCheck(counts, "hardReset", false, "callback not configured");
    } else {
      status = device.hardReset();
      const bool resetOk = status.ok();
      reportCheck(counts, "hardReset exact-zero verification", resetOk,
                  errorName(status.code));
      const auto resetObservation = device.channelMaskObservation();
      const bool resetVerified = resetObservation.verified() &&
                                 resetObservation.mask.isNone();
      reportCheck(counts, "hardReset leaves verified all-off", resetVerified);
      if (!resetOk || !resetVerified) {
        finishHilSafe(counts);
        return;
      }
    }
  } else {
    reportSkip(counts, "hardReset", "use 'hil run reset' to include RESET");
  }

  finishHilSafe(counts);
}

void runStress(unsigned long count, bool mixed) {
  TCA9548A::Status status = TCA9548A::Status::Ok();
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
    yield();
  }

  const bool safeOff = safeOffVerified();
  const uint32_t durationMs = millis() - startedMs;
  if (mixed) {
    Serial.println(F("=== stress_mix summary ==="));
  }
  Serial.printf("Stress results: completed=%lu requested=%lu status=%s safe_off=%s\n",
                completed, count, errorName(status.code),
                safeOff ? "OK" : "FAILED");
  Serial.printf("Duration: %lu ms\n", static_cast<unsigned long>(durationMs));
  Serial.printf("Health delta: success=%lu failure=%lu\n",
                static_cast<unsigned long>(device.totalSuccess() -
                                           successesBefore),
                static_cast<unsigned long>(device.totalFailures() -
                                           failuresBefore));
  if (!safeOff) {
    Serial.println(F("  [FAIL] final safe-off was not verified"));
  }
}

void processCommand(const char* command) {
  if (std::strcmp(command, "help") == 0 || std::strcmp(command, "?") == 0) {
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
    printObservation();
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
  } else if (std::strcmp(command, "invalidate") == 0) {
    device.invalidateChannelMask();
    Serial.println(F("invalidate: OK (no bus I/O)"));
    printObservation();
  } else if (std::strcmp(command, "begin") == 0) {
    beginDriver();
  } else if (std::strcmp(command, "end") == 0) {
    device.end();
    Serial.println(F("end: OK (no bus I/O)"));
  } else if (std::strcmp(command, "scan") == 0) {
    if (i2cReady) {
      (void)i2c::scan();
    } else {
      Serial.println(F("scan: NOT_INITIALIZED (I2C controller unavailable)"));
    }
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
  i2cReady = board::initI2c();
  if (!i2cReady) {
    LOGE("I2C controller initialization failed");
  }
  configureDriver();
  beginDriver();
  printHelp();
  Serial.println();
  cli::printPrompt();
}

void loop() {
  device.tick(millis());

  char command[128];
  if (cli_shell::readLine(command, sizeof(command))) {
    processCommand(command);
    Serial.println();
    cli::printPrompt();
  }
}
