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
constexpr unsigned long MAX_STRESS_COUNT = 1000UL;

bool safeOffVerified();
using TCA9548A::errorName;
using TCA9548A::driverStateName;
using TCA9548A::maskProvenanceName;

void printStatus(const TCA9548A::Status& status) {
  LOG_SERIAL.printf("%s%s%s", cli::resultColor(status.ok()),
                errorName(status.code), LOG_COLOR_RESET);
  if (!status.ok()) {
    LOG_SERIAL.printf(" (detail=%ld, %s)", static_cast<long>(status.detail),
                  status.msg);
  }
}

void printMask(TCA9548A::ChannelMask mask) {
  LOG_SERIAL.printf("0x%02X [", mask.raw());
  bool first = true;
  for (uint8_t index = 0; index < TCA9548A::cmd::NUM_CHANNELS; ++index) {
    const auto channel = static_cast<TCA9548A::Channel>(index);
    if (!mask.contains(channel)) {
      continue;
    }
    LOG_SERIAL.printf("%s%u", first ? "" : ",", static_cast<unsigned>(index));
    first = false;
  }
  if (first) {
    LOG_SERIAL.print(F("none"));
  }
  LOG_SERIAL.print(F("]"));
}

void printObservation() {
  const auto observation = device.channelMaskObservation();
  LOG_SERIAL.printf("Mask cache: %s known=%s verified=%s value=",
                maskProvenanceName(observation.provenance),
                observation.known() ? "yes" : "no",
                observation.verified() ? "yes" : "no");
  printMask(observation.mask);
  LOG_SERIAL.println();
}

void printVersionInfo() {
  cli::printSection("Version Info");
  LOG_SERIAL.printf("  MCU: %s rev %u, flash %lu bytes, PSRAM %s (%lu bytes)\n",
                ESP.getChipModel(),
                static_cast<unsigned>(ESP.getChipRevision()),
                static_cast<unsigned long>(ESP.getFlashChipSize()),
                psramFound() ? "ready" : "not available",
                static_cast<unsigned long>(ESP.getPsramSize()));
  LOG_SERIAL.printf("  Arduino-ESP32: %s\n", ESP.getCoreVersion());
  LOG_SERIAL.printf("  ESP-IDF: %s\n", ESP.getSdkVersion());
  LOG_SERIAL.printf("  Library: %s\n", TCA9548A::VERSION);
  LOG_SERIAL.printf("  Full: %s\n", TCA9548A::VERSION_FULL);
  LOG_SERIAL.printf("  Code: %lu\n",
                static_cast<unsigned long>(TCA9548A::VERSION_CODE));
}

void printHealth() {
  cli::printSection("Driver Health");
  const bool stateAliasMatches = device.driverState() == device.state();
  LOG_SERIAL.printf("  State: %s%s%s (passive; never gates I2C)\n",
                cli::stateColor(device.isInitialized(), device.isOnline(),
                                device.consecutiveFailures()),
                driverStateName(device.state()), LOG_COLOR_RESET);
  LOG_SERIAL.printf("  State alias parity: %s\n",
                stateAliasMatches ? "yes" : "no");
  LOG_SERIAL.printf("  Bound: %s\n", device.isBound() ? "yes" : "no");
  LOG_SERIAL.printf("  Initialized: %s\n",
                device.isInitialized() ? "yes" : "no");
  LOG_SERIAL.printf("  Consecutive failures: %u\n",
                static_cast<unsigned>(device.consecutiveFailures()));
  LOG_SERIAL.printf("  Total success/failure: %lu/%lu\n",
                static_cast<unsigned long>(device.totalSuccess()),
                static_cast<unsigned long>(device.totalFailures()));
  LOG_SERIAL.printf("  Last OK/error ms: %lu/%lu\n",
                static_cast<unsigned long>(device.lastOkMs()),
                static_cast<unsigned long>(device.lastErrorMs()));
  LOG_SERIAL.printf("  Last error: %s\n", errorName(device.lastError().code));
  printObservation();
}

void printConfig() {
  TCA9548A::SettingsSnapshot snapshot;
  const auto status = device.getSettings(snapshot);
  const TCA9548A::Config& boundConfig = device.getConfig();
  const bool configReferenceMatches =
      !snapshot.bound ||
      (boundConfig.i2cAddress == snapshot.i2cAddress &&
       boundConfig.i2cTimeoutMs == snapshot.i2cTimeoutMs &&
       boundConfig.resetTimeoutMs == snapshot.resetTimeoutMs &&
       boundConfig.offlineThreshold == snapshot.offlineThreshold);
  cli::printSection("Configuration");
  LOG_SERIAL.printf("  Bound: %s\n", snapshot.bound ? "yes" : "no");
  LOG_SERIAL.printf("  Initialized: %s\n", snapshot.initialized ? "yes" : "no");
  LOG_SERIAL.printf("  I2C address: 0x%02X\n", snapshot.i2cAddress);
  LOG_SERIAL.printf("  I2C timeout: %lu ms\n",
                static_cast<unsigned long>(snapshot.i2cTimeoutMs));
  LOG_SERIAL.printf("  RESET timeout: %lu ms\n",
                static_cast<unsigned long>(snapshot.resetTimeoutMs));
  LOG_SERIAL.printf("  nowMs hook: %s\n",
                snapshot.hasNowMsHook ? "configured" : "not configured");
  LOG_SERIAL.printf("  RESET callback: %s\n",
                snapshot.hasHardReset ? "configured" : "not configured");
  LOG_SERIAL.printf("  Offline threshold: %u (diagnostic only)\n",
                static_cast<unsigned>(snapshot.offlineThreshold));
  LOG_SERIAL.printf("  Config reference parity: %s\n",
                configReferenceMatches ? "yes" : "no");
  LOG_SERIAL.print(F("  Snapshot: "));
  printStatus(status);
  LOG_SERIAL.println();
}

void printHelp() {
  cli::printSection("TCA9548A CLI Help");
  LOG_SERIAL.println(F("  version / ver                  Version information"));
  LOG_SERIAL.println(F("  cfg                            Bound configuration"));
  LOG_SERIAL.println(F("  health / drv / state           Passive diagnostics"));
  LOG_SERIAL.println(F("  read / dump                    Read and verify mask"));
  LOG_SERIAL.println(F("  select <0-7>                   Select one channel"));
  LOG_SERIAL.println(F("  mask <0-255>                   Write an arbitrary mask"));
  LOG_SERIAL.println(F("  off                            Disable all channels"));
  LOG_SERIAL.println(F("  probe                          Raw diagnostic read"));
  LOG_SERIAL.println(F("  recover                        One safe-off write"));
  LOG_SERIAL.println(F("  reset / hardreset              RESET then verify 0x00"));
  LOG_SERIAL.println(F("  invalidate                     Mark cached mask unknown"));
  LOG_SERIAL.println(F("  begin / end                    Bind+probe / bus-silent unbind"));
  LOG_SERIAL.println(F("  scan                           Scan active topology: 126 probes"));
  LOG_SERIAL.println(F("  stress <1-1000>                Select sample, finish all-off"));
  LOG_SERIAL.println(F("  stress_mix <1-1000>            Primitive mix, finish all-off"));
  LOG_SERIAL.println(F("  selftest                       Live checks, restore entry mask"));
  LOG_SERIAL.println(F("  hil [dry|parser|run|run reset] HIL contract entry point"));
  LOG_SERIAL.println(F("  help / ?                       This help"));
}

uint32_t nowMs(void*) {
  return millis();
}

TCA9548A::Status pulseReset(uint32_t timeoutMs, void*) {
  if (board::TCA_RESET < 0) {
    return TCA9548A::Status::Error(TCA9548A::Err::RESET_ERROR,
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
    // RESET is active low. On Arduino-ESP32 3.x digitalWrite() is ignored until
    // pinMode() has registered the pin, so register it as an input pull-up
    // first: that holds the line high, makes the following write reach the
    // output latch, and switching to OUTPUT then cannot emit a reset pulse.
    pinMode(board::TCA_RESET, INPUT_PULLUP);
    digitalWrite(board::TCA_RESET, HIGH);
    pinMode(board::TCA_RESET, OUTPUT);
    config.hardReset = pulseReset;
  }
}

void beginDriver() {
  if (!i2cReady) {
    LOG_SERIAL.println(F("begin: NOT_INITIALIZED (I2C controller unavailable)"));
    return;
  }

  const bool wasBound = device.isBound();
  const auto status = device.begin(config);
  LOG_SERIAL.print(F("begin: "));
  printStatus(status);
  LOG_SERIAL.printf(" (bound=%s)\n", device.isBound() ? "yes" : "no");
  if (!wasBound && device.isBound()) {
    const bool safeOff = safeOffVerified();
    LOG_SERIAL.printf("startup safe-off: %s%s%s\n", cli::resultColor(safeOff),
                  safeOff ? "OK (verified 0x00)" : "FAILED",
                  LOG_COLOR_RESET);
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
  if (errno == ERANGE || end == text || *end != '\0' || value > maximum) {
    return false;
  }
  output = value;
  return true;
}

bool safeOffVerified() {
  auto status = device.disableAll();
  if (!status.ok()) {
    LOG_SERIAL.print(F("safe-off write: "));
    printStatus(status);
    LOG_SERIAL.println();
    return false;
  }

  TCA9548A::ChannelMask observed;
  status = device.readChannelMask(observed);
  if (!status.ok() || !observed.isNone()) {
    LOG_SERIAL.print(F("safe-off readback: "));
    printStatus(status);
    if (status.ok()) {
      LOG_SERIAL.print(F(" observed="));
      printMask(observed);
    }
    LOG_SERIAL.println();
    return false;
  }
  return true;
}

void scanBus() {
  if (!i2cReady) {
    LOG_SERIAL.println(F("scan: NOT_INITIALIZED (I2C controller unavailable)"));
    return;
  }

  TCA9548A::ChannelMask visibleMask;
  const auto topologyStatus = device.readChannelMask(visibleMask);
  LOG_SERIAL.print(F("Scan topology: "));
  printStatus(topologyStatus);
  if (topologyStatus.ok()) {
    LOG_SERIAL.print(F(" active_mask="));
    printMask(visibleMask);
  } else {
    LOG_SERIAL.print(F(" active_mask=unknown"));
  }
  LOG_SERIAL.println(F(" (select a one-hot mask before scan to isolate a branch)"));
  (void)i2c::scan();
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
  LOG_SERIAL.printf("  [%s%s%s] %s", cli::resultColor(passed),
                passed ? "PASS" : "FAIL", LOG_COLOR_RESET, name);
  if (detail != nullptr && detail[0] != '\0') {
    LOG_SERIAL.printf(" - %s", detail);
  }
  LOG_SERIAL.println();
}

void reportSkip(HilCounts& counts, const char* name, const char* detail) {
  ++counts.skipped;
  LOG_SERIAL.printf("  [%sSKIP%s] %s - %s\n", LOG_COLOR_YELLOW,
                LOG_COLOR_RESET, name, detail);
}

void printHilResult(const HilCounts& counts) {
  LOG_SERIAL.printf("HIL result: pass=%u fail=%u skip=%u\n",
                static_cast<unsigned>(counts.passed),
                static_cast<unsigned>(counts.failed),
                static_cast<unsigned>(counts.skipped));
}

bool restoreMaskVerified(TCA9548A::ChannelMask originalMask) {
  const auto writeStatus = device.writeChannelMask(originalMask);
  if (!writeStatus.ok()) {
    LOG_SERIAL.print(F("restore write: "));
    printStatus(writeStatus);
    LOG_SERIAL.println();
  }

  TCA9548A::ChannelMask observed;
  const auto readStatus = device.readChannelMask(observed);
  const bool matched = readStatus.ok() && observed.raw() == originalMask.raw();
  if (!matched) {
    LOG_SERIAL.print(F("restore readback: "));
    printStatus(readStatus);
    if (readStatus.ok()) {
      LOG_SERIAL.print(F(" expected="));
      printMask(originalMask);
      LOG_SERIAL.print(F(" observed="));
      printMask(observed);
    }
    LOG_SERIAL.println();
  }
  return writeStatus.ok() && matched;
}

void finishHilRestored(HilCounts& counts,
                       TCA9548A::ChannelMask originalMask) {
  reportCheck(counts, "final verified mask restore",
              restoreMaskVerified(originalMask));
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
    printHilResult(counts);
    return;
  }

  TCA9548A::ChannelMask originalMask;
  status = device.readChannelMask(originalMask);
  const bool originalCaptured = status.ok();
  reportCheck(counts, "capture original mask readback", originalCaptured,
              errorName(status.code));
  if (!originalCaptured) {
    printHilResult(counts);
    return;
  }

  status = device.disableAll();
  const bool disableOk = status.ok();
  reportCheck(counts, "disableAll write", disableOk, errorName(status.code));
  if (!disableOk) {
    finishHilRestored(counts, originalMask);
    return;
  }
  TCA9548A::ChannelMask observed;
  status = device.readChannelMask(observed);
  const bool disableVerified = status.ok() && observed.isNone();
  reportCheck(counts, "disableAll readback", disableVerified,
              errorName(status.code));
  if (!disableVerified) {
    finishHilRestored(counts, originalMask);
    return;
  }

  bool allOneHotVerified = true;
  char oneHotDetail[48] = {};
  for (uint8_t index = 0U; index < TCA9548A::cmd::NUM_CHANNELS; ++index) {
    const auto channel = static_cast<TCA9548A::Channel>(index);
    status = device.selectChannel(channel);
    if (!status.ok()) {
      std::snprintf(oneHotDetail, sizeof(oneHotDetail), "CH%u write: %s",
                    static_cast<unsigned>(index), errorName(status.code));
      allOneHotVerified = false;
      break;
    }
    status = device.readChannelMask(observed);
    if (!status.ok() ||
        observed.raw() != TCA9548A::ChannelMask::one(channel).raw()) {
      std::snprintf(oneHotDetail, sizeof(oneHotDetail),
                    "CH%u readback: %s", static_cast<unsigned>(index),
                    status.ok() ? "MISMATCH" : errorName(status.code));
      allOneHotVerified = false;
      break;
    }
  }
  reportCheck(counts, "all eight one-hot channels", allOneHotVerified,
              oneHotDetail);
  if (!allOneHotVerified) {
    finishHilRestored(counts, originalMask);
    return;
  }

  status = device.writeChannelMask(TCA9548A::ChannelMask::fromRaw(0xA5U));
  const bool maskWriteOk = status.ok();
  reportCheck(counts, "write mask 0xA5", maskWriteOk,
              errorName(status.code));
  if (!maskWriteOk) {
    finishHilRestored(counts, originalMask);
    return;
  }
  status = device.readChannelMask(observed);
  const bool maskVerified = status.ok() && observed.raw() == 0xA5U;
  reportCheck(counts, "read mask 0xA5", maskVerified,
              errorName(status.code));
  if (!maskVerified) {
    finishHilRestored(counts, originalMask);
    return;
  }

  status = device.recover();
  const bool recoverOk = status.ok();
  reportCheck(counts, "recover safe-off write", recoverOk,
              errorName(status.code));
  if (!recoverOk) {
    finishHilRestored(counts, originalMask);
    return;
  }
  status = device.readChannelMask(observed);
  const bool recoverVerified = status.ok() && observed.isNone();
  reportCheck(counts, "recover readback 0x00", recoverVerified,
              errorName(status.code));
  if (!recoverVerified) {
    finishHilRestored(counts, originalMask);
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
        finishHilRestored(counts, originalMask);
        return;
      }
    }
  } else {
    reportSkip(counts, "hardReset", "use 'hil run reset' to include RESET");
  }

  finishHilRestored(counts, originalMask);
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
    LOG_SERIAL.println(F("=== stress_mix summary ==="));
  }
  LOG_SERIAL.printf("Stress results: completed=%lu requested=%lu status=%s safe_off=%s\n",
                completed, count, errorName(status.code),
                safeOff ? "OK" : "FAILED");
  LOG_SERIAL.printf("Duration: %lu ms\n", static_cast<unsigned long>(durationMs));
  LOG_SERIAL.printf("Health delta: success=%lu failure=%lu\n",
                static_cast<unsigned long>(device.totalSuccess() -
                                           successesBefore),
                static_cast<unsigned long>(device.totalFailures() -
                                           failuresBefore));
  if (!safeOff) {
    LOG_SERIAL.println(F("  [FAIL] final safe-off was not verified"));
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
    LOG_SERIAL.print(F("read: "));
    printStatus(status);
    if (status.ok()) {
      LOG_SERIAL.print(F(" mask="));
      printMask(mask);
    }
    LOG_SERIAL.println();
  } else if (std::strcmp(command, "off") == 0) {
    const auto status = device.disableAll();
    LOG_SERIAL.print(F("off: "));
    printStatus(status);
    LOG_SERIAL.println();
  } else if (std::strcmp(command, "probe") == 0) {
    const auto status = device.probe();
    LOG_SERIAL.print(F("probe: "));
    printStatus(status);
    LOG_SERIAL.println();
    printObservation();
  } else if (std::strcmp(command, "recover") == 0) {
    const auto status = device.recover();
    LOG_SERIAL.print(F("recover (safe-off write): "));
    printStatus(status);
    LOG_SERIAL.println();
  } else if (std::strcmp(command, "reset") == 0 ||
             std::strcmp(command, "hardreset") == 0) {
    const auto status = device.hardReset();
    LOG_SERIAL.print(F("hardreset: "));
    printStatus(status);
    LOG_SERIAL.println();
    printObservation();
  } else if (std::strcmp(command, "invalidate") == 0) {
    device.invalidateChannelMask();
    LOG_SERIAL.println(F("invalidate: OK (no bus I/O)"));
    printObservation();
  } else if (std::strcmp(command, "begin") == 0) {
    beginDriver();
  } else if (std::strcmp(command, "end") == 0) {
    device.end();
    LOG_SERIAL.println(F("end: OK (no bus I/O)"));
  } else if (std::strcmp(command, "scan") == 0) {
    scanBus();
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
      LOG_SERIAL.printf("select %lu: ", value);
      printStatus(status);
      LOG_SERIAL.println();
    } else if (parseUnsignedArgument(command, "mask", 255U, value)) {
      const auto status = device.writeChannelMask(
          TCA9548A::ChannelMask::fromRaw(static_cast<uint8_t>(value)));
      LOG_SERIAL.printf("mask 0x%02lX: ", value);
      printStatus(status);
      LOG_SERIAL.println();
    } else if (parseUnsignedArgument(command, "stress_mix", MAX_STRESS_COUNT,
                                     value) &&
               value > 0U) {
      runStress(value, true);
    } else if (parseUnsignedArgument(command, "stress", MAX_STRESS_COUNT,
                                     value) &&
               value > 0U) {
      runStress(value, false);
    } else {
      // Printed unconditionally so it does not depend on LOG_LEVEL, matching
      // the native CLI.
      LOG_SERIAL.printf("%s[E]%s Unknown or invalid command: %s\n", LOG_COLOR_RED,
                    LOG_COLOR_RESET, command);
    }
  }
}

}  // namespace

void setup() {
  log_begin(115200);
  delay(1000);
  LOG_SERIAL.println(F("\n============================="));
  LOG_SERIAL.println(F("  TCA9548A Bring-up CLI"));
  LOG_SERIAL.println(F("============================="));

  printVersionInfo();
  i2cReady = board::initI2c();
  if (!i2cReady) {
    LOGE("I2C controller initialization failed");
  }
  configureDriver();
  beginDriver();
  printHelp();
  LOG_SERIAL.println();
  cli::printPrompt();
}

void loop() {
  device.tick(millis());

  char command[128];
  const cli_shell::LineResult lineResult =
      cli_shell::pollLine(command, sizeof(command));
  if (lineResult == cli_shell::LineResult::READY) {
    processCommand(command);
    LOG_SERIAL.println();
    cli::printPrompt();
  } else if (lineResult == cli_shell::LineResult::TOO_LONG ||
             lineResult == cli_shell::LineResult::OUTPUT_TOO_SMALL) {
    LOG_SERIAL.println();
    cli::printPrompt();
  }
}
