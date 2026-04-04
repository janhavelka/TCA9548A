/// @file main.cpp
/// @brief Basic bringup example for TCA9548A 8-channel I2C switch
/// @note This is an EXAMPLE, not part of the library

#include <Arduino.h>
#include <cstdlib>

#include "examples/common/Log.h"
#include "examples/common/BoardConfig.h"
#include "examples/common/BusDiag.h"
#include "examples/common/I2cScanner.h"
#include "examples/common/I2cTransport.h"
#include "examples/common/CliShell.h"
#include "examples/common/HealthView.h"

#include "TCA9548A/TCA9548A.h"
#include "TCA9548A/Version.h"

// ============================================================================
// Globals
// ============================================================================

TCA9548A::TCA9548A device;
TCA9548A::Config gConfig;
bool gConfigReady = false;
bool verboseMode = false;

// ============================================================================
// Color Helpers
// ============================================================================

inline const char* goodIfZeroColor(uint32_t val) {
  return (val == 0) ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}
inline const char* goodIfNonZeroColor(uint32_t val) {
  return (val > 0) ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW;
}
inline const char* onOffColor(bool on) {
  return on ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW;
}
inline const char* successRateColor(float pct) {
  if (pct >= 99.9f) return LOG_COLOR_GREEN;
  if (pct >= 80.0f) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
}
inline const char* skipCountColor(uint32_t count) {
  return (count == 0) ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW;
}

// ============================================================================
// Conversion Helpers
// ============================================================================

const char* errToStr(TCA9548A::Err err) {
  using namespace TCA9548A;
  switch (err) {
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
    default: return "UNKNOWN";
  }
}

const char* stateToStr(TCA9548A::DriverState st) {
  using namespace TCA9548A;
  switch (st) {
    case DriverState::UNINIT: return "UNINIT";
    case DriverState::READY: return "READY";
    case DriverState::DEGRADED: return "DEGRADED";
    case DriverState::OFFLINE: return "OFFLINE";
    default: return "UNKNOWN";
  }
}

// ============================================================================
// Print Helpers
// ============================================================================

void printStatus(const TCA9548A::Status& st) {
  Serial.printf("  %s%s%s (detail=%ld)\n",
                LOG_COLOR_RESULT(st.ok()), errToStr(st.code), LOG_COLOR_RESET,
                static_cast<long>(st.detail));
  if (st.msg && st.msg[0]) {
    Serial.printf("  msg: %s\n", st.msg);
  }
}

void printMask(uint8_t mask) {
  Serial.printf("  Mask: 0x%02X [", mask);
  for (uint8_t i = 0; i < 8; i++) {
    Serial.printf("%c", (mask & (1U << i)) ? '1' : '0');
    if (i < 7) Serial.print(' ');
  }
  Serial.println("]");
}

void printVersionInfo() {
  Serial.printf("%s=== Version Info ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);
  Serial.printf("  Library: TCA9548A v%u.%u.%u (code %u)\n",
                TCA9548A::VERSION_MAJOR, TCA9548A::VERSION_MINOR,
                TCA9548A::VERSION_PATCH, TCA9548A::VERSION_CODE);
  Serial.printf("  Build:   %s %s\n", __DATE__, __TIME__);
#ifdef ARDUINO
  Serial.printf("  Arduino: %d\n", ARDUINO);
#endif
}

void printDriverHealth() {
  const auto st = device.state();
  const bool online = device.isOnline();
  const uint8_t consFail = device.consecutiveFailures();
  const uint32_t totalOk = device.totalSuccess();
  const uint32_t totalFail = device.totalFailures();
  const uint32_t total = totalOk + totalFail;
  const float pct = (total > 0) ? (100.0f * static_cast<float>(totalOk) / static_cast<float>(total)) : 0.0f;

  Serial.printf("%s=== Driver Health ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);
  Serial.printf("  State:              %s%s%s\n",
                LOG_COLOR_STATE(online, consFail), stateToStr(st), LOG_COLOR_RESET);
  Serial.printf("  Online:             %s%s%s\n",
                onOffColor(online), online ? "yes" : "no", LOG_COLOR_RESET);
  Serial.printf("  Consecutive fail:   %s%u%s\n",
                goodIfZeroColor(consFail), consFail, LOG_COLOR_RESET);
  Serial.printf("  Total success:      %s%lu%s\n",
                goodIfNonZeroColor(totalOk), static_cast<unsigned long>(totalOk), LOG_COLOR_RESET);
  Serial.printf("  Total failures:     %s%lu%s\n",
                goodIfZeroColor(totalFail), static_cast<unsigned long>(totalFail), LOG_COLOR_RESET);
  Serial.printf("  Success rate:       %s%.2f%%%s\n",
                successRateColor(pct), pct, LOG_COLOR_RESET);
  Serial.printf("  Last OK:            %lu ms\n", static_cast<unsigned long>(device.lastOkMs()));
  Serial.printf("  Last error:         %lu ms\n", static_cast<unsigned long>(device.lastErrorMs()));
  Serial.printf("  Last error code:    %s\n", errToStr(device.lastError().code));
  Serial.printf("  Last known mask:    0x%02X\n", device.lastKnownMask());
  Serial.printf("  Address:            0x%02X\n", gConfig.i2cAddress);
}

void printConfig() {
  Serial.printf("%s=== Configuration ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);
  Serial.printf("  I2C address:        0x%02X\n", gConfig.i2cAddress);
  Serial.printf("  I2C timeout:        %lu ms\n", static_cast<unsigned long>(gConfig.i2cTimeoutMs));
  Serial.printf("  Offline threshold:  %u\n", gConfig.offlineThreshold);
  Serial.printf("  Recover backoff:    %lu ms\n", static_cast<unsigned long>(gConfig.recoverBackoffMs));
  Serial.printf("  Hard reset:         %s\n", gConfig.hardReset ? "configured" : "none");
  Serial.printf("  Use hard reset:     %s\n", log_bool_str(gConfig.recoverUseHardReset));
}

void printHelp() {
  auto helpSection = [](const char* title) {
    Serial.printf("\n%s[%s]%s\n", LOG_COLOR_GREEN, title, LOG_COLOR_RESET);
  };
  auto helpItem = [](const char* cmd, const char* desc) {
    Serial.printf("  %s%-32s%s - %s\n", LOG_COLOR_CYAN, cmd, LOG_COLOR_RESET, desc);
  };

  Serial.println();
  Serial.printf("%s=== TCA9548A CLI Help ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);
  helpSection("Common");
  helpItem("help / ?", "Show this help");
  helpItem("version / ver", "Print firmware and library version info");
  helpItem("scan", "Scan I2C bus");

  helpSection("Channel Control");
  helpItem("select <0-7>", "Select single channel (disables others)");
  helpItem("mask <hex>", "Set channel bitmask (e.g., mask 0F)");
  helpItem("enable <0-7>", "Enable one channel (preserves others)");
  helpItem("disable <0-7>", "Disable one channel (preserves others)");
  helpItem("off", "Disable all channels (write 0x00)");
  helpItem("read", "Read current channel mask from device");
  helpItem("check <0-7>", "Check if a specific channel is enabled");
  helpItem("scanch <0-7>", "Select channel then scan downstream bus");

  helpSection("Lifecycle");
  helpItem("begin", "Initialize driver");
  helpItem("end", "Shutdown driver");
  helpItem("addr <hex>", "Change device address and reinitialize");

  helpSection("Diagnostics");
  helpItem("drv", "Show driver state and health");
  helpItem("probe", "Probe device (no health tracking)");
  helpItem("recover", "Manual recovery attempt");
  helpItem("cfg / settings", "Print active configuration");
  helpItem("verbose [0|1]", "Enable/disable verbose output");
  helpItem("stress [N]", "Run N channel-sweep cycles");
  helpItem("stress_mix [N]", "Run N mixed-operation cycles");
  helpItem("selftest", "Run safe command self-test report");
}

// ============================================================================
// Configuration
// ============================================================================

void initConfig() {
  gConfig.i2cWrite = transport::wireWrite;
  gConfig.i2cWriteRead = transport::wireWriteRead;
  gConfig.i2cUser = &Wire;
  gConfig.i2cAddress = 0x70;
  gConfig.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
  gConfig.offlineThreshold = 5;
  gConfig.recoverBackoffMs = 100;
  gConfigReady = true;
}

void doBegin() {
  if (!gConfigReady) {
    initConfig();
  }
  TCA9548A::Status st = device.begin(gConfig);
  Serial.print("begin: ");
  printStatus(st);
}

// ============================================================================
// Stress Tests
// ============================================================================

void runStress(int count) {
  if (count <= 0 || count > 100000) {
    LOGW("Invalid count (1-100000)");
    return;
  }
  int ok = 0;
  int fail = 0;
  bool hasFailure = false;
  TCA9548A::Status firstFailure = TCA9548A::Status::Ok();
  TCA9548A::Status lastFailure = TCA9548A::Status::Ok();
  const uint32_t startMs = millis();

  for (int i = 0; i < count; ++i) {
    const uint8_t ch = static_cast<uint8_t>(i % 8);
    auto st = device.selectChannel(ch);
    if (st.ok()) {
      ok++;
      LOGV(verboseMode, "  %d: ch%u OK", i + 1, ch);
    } else {
      fail++;
      if (!hasFailure) { firstFailure = st; hasFailure = true; }
      lastFailure = st;
      if (verboseMode) { printStatus(st); }
    }
  }
  const uint32_t elapsed = millis() - startMs;
  const float pct = (count > 0) ? (100.0f * static_cast<float>(ok) / static_cast<float>(count)) : 0.0f;
  Serial.printf("  Stress results: %s%d ok%s, %s%d failed%s (%s%.2f%%%s)\n",
                goodIfNonZeroColor(static_cast<uint32_t>(ok)), ok, LOG_COLOR_RESET,
                goodIfZeroColor(static_cast<uint32_t>(fail)), fail, LOG_COLOR_RESET,
                successRateColor(pct), pct, LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(elapsed));
  if (elapsed > 0) {
    Serial.printf("  Rate: %.2f ops/s\n", (1000.0f * static_cast<float>(count)) / elapsed);
  }
  if (hasFailure) {
    Serial.println("  First failure:");
    printStatus(firstFailure);
    if (fail > 1) {
      Serial.println("  Last failure:");
      printStatus(lastFailure);
    }
  }
}

void runStressMix(int count) {
  if (count <= 0 || count > 100000) {
    LOGW("Invalid count (1-100000)");
    return;
  }

  struct OpStats {
    const char* name;
    uint32_t ok;
    uint32_t fail;
  };
  OpStats stats[] = {
    {"selectCh",    0, 0},
    {"setMask",     0, 0},
    {"readMask",    0, 0},
    {"enableCh",    0, 0},
    {"disableCh",   0, 0},
    {"disableAll",  0, 0},
  };
  const int opCount = static_cast<int>(sizeof(stats) / sizeof(stats[0]));

  const uint32_t succBefore = device.totalSuccess();
  const uint32_t failBefore = device.totalFailures();
  const uint32_t startMs = millis();

  for (int i = 0; i < count; ++i) {
    const int op = i % opCount;
    TCA9548A::Status st = TCA9548A::Status::Ok();

    switch (op) {
      case 0: st = device.selectChannel(static_cast<uint8_t>(i % 8)); break;
      case 1: st = device.setChannelMask(static_cast<uint8_t>(i & 0xFF)); break;
      case 2: { uint8_t m = 0; st = device.readChannelMask(m); break; }
      case 3: st = device.enableChannels(static_cast<uint8_t>(1U << (i % 8))); break;
      case 4: st = device.disableChannels(static_cast<uint8_t>(1U << (i % 8))); break;
      case 5: st = device.disableAll(); break;
      default: break;
    }

    if (st.ok()) {
      stats[op].ok++;
    } else {
      stats[op].fail++;
      if (verboseMode) {
        Serial.printf("  [%d] %s failed: %s\n", i, stats[op].name, errToStr(st.code));
      }
    }
  }

  const uint32_t elapsed = millis() - startMs;
  uint32_t okTotal = 0;
  uint32_t failTotal = 0;
  for (int i = 0; i < opCount; ++i) {
    okTotal += stats[i].ok;
    failTotal += stats[i].fail;
  }

  Serial.println("=== stress_mix summary ===");
  const float successPct =
      (count > 0) ? (100.0f * static_cast<float>(okTotal) / static_cast<float>(count)) : 0.0f;
  Serial.printf("  Total: %sok=%lu%s %sfail=%lu%s (%s%.2f%%%s)\n",
                goodIfNonZeroColor(okTotal), static_cast<unsigned long>(okTotal), LOG_COLOR_RESET,
                goodIfZeroColor(failTotal), static_cast<unsigned long>(failTotal), LOG_COLOR_RESET,
                successRateColor(successPct), successPct, LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(elapsed));
  if (elapsed > 0) {
    Serial.printf("  Rate: %.2f ops/s\n", (1000.0f * static_cast<float>(count)) / elapsed);
  }
  for (int i = 0; i < opCount; ++i) {
    Serial.printf("  %-12s %sok=%lu%s %sfail=%lu%s\n",
                  stats[i].name,
                  goodIfNonZeroColor(stats[i].ok), static_cast<unsigned long>(stats[i].ok), LOG_COLOR_RESET,
                  goodIfZeroColor(stats[i].fail), static_cast<unsigned long>(stats[i].fail), LOG_COLOR_RESET);
  }
  const uint32_t successDelta = device.totalSuccess() - succBefore;
  const uint32_t failDelta = device.totalFailures() - failBefore;
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                goodIfNonZeroColor(successDelta), static_cast<unsigned long>(successDelta), LOG_COLOR_RESET,
                goodIfZeroColor(failDelta), static_cast<unsigned long>(failDelta), LOG_COLOR_RESET);
}

// ============================================================================
// Self-Test
// ============================================================================

void runSelfTest() {
  struct TestStats {
    uint32_t pass = 0;
    uint32_t fail = 0;
    uint32_t skip = 0;
  } stats;

  enum class SelftestOutcome : uint8_t { PASS, FAIL, SKIP };
  auto report = [&](const char* name, SelftestOutcome outcome, const char* note) {
    const bool passed = (outcome == SelftestOutcome::PASS);
    const bool skipped = (outcome == SelftestOutcome::SKIP);
    const char* color = skipped ? LOG_COLOR_YELLOW : LOG_COLOR_RESULT(passed);
    const char* tag = skipped ? "SKIP" : (passed ? "PASS" : "FAIL");
    Serial.printf("  [%s%s%s] %s", color, tag, LOG_COLOR_RESET, name);
    if (note && note[0]) {
      Serial.printf(" - %s", note);
    }
    Serial.println();
    if (skipped) { stats.skip++; }
    else if (passed) { stats.pass++; }
    else { stats.fail++; }
  };
  auto reportCheck = [&](const char* name, bool passed, const char* note) {
    report(name, passed ? SelftestOutcome::PASS : SelftestOutcome::FAIL, note);
  };
  auto reportSkip = [&](const char* name, const char* note) {
    report(name, SelftestOutcome::SKIP, note);
  };

  Serial.println("=== TCA9548A selftest (safe commands) ===");

  const uint32_t succBefore = device.totalSuccess();
  const uint32_t failBefore = device.totalFailures();
  const uint8_t consBefore = device.consecutiveFailures();

  // Check probe
  const TCA9548A::Status pst = device.probe();
  if (pst.code == TCA9548A::Err::NOT_INITIALIZED) {
    reportSkip("probe responds", "driver not initialized");
    reportSkip("remaining checks", "selftest aborted");
    Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                  goodIfNonZeroColor(stats.pass), static_cast<unsigned long>(stats.pass), LOG_COLOR_RESET,
                  goodIfZeroColor(stats.fail), static_cast<unsigned long>(stats.fail), LOG_COLOR_RESET,
                  skipCountColor(stats.skip), static_cast<unsigned long>(stats.skip), LOG_COLOR_RESET);
    return;
  }
  const bool probeHealthUnchanged =
      device.totalSuccess() == succBefore &&
      device.totalFailures() == failBefore &&
      device.consecutiveFailures() == consBefore;
  reportCheck("probe responds", pst.ok(), pst.ok() ? "" : errToStr(pst.code));
  reportCheck("probe no-health-side-effects", probeHealthUnchanged, "");

  // Save initial mask
  uint8_t origMask = 0;
  TCA9548A::Status st = device.readChannelMask(origMask);
  reportCheck("readChannelMask", st.ok(), st.ok() ? "" : errToStr(st.code));

  // disableAll
  st = device.disableAll();
  reportCheck("disableAll", st.ok(), st.ok() ? "" : errToStr(st.code));
  if (st.ok()) {
    uint8_t m = 0xFF;
    st = device.readChannelMask(m);
    reportCheck("verify all off", st.ok() && m == 0x00, "");
  }

  // selectChannel
  st = device.selectChannel(3);
  reportCheck("selectChannel(3)", st.ok(), st.ok() ? "" : errToStr(st.code));
  if (st.ok()) {
    uint8_t m = 0;
    st = device.readChannelMask(m);
    reportCheck("verify ch3 only", st.ok() && m == 0x08, "");
  }

  // setChannelMask
  st = device.setChannelMask(0xA5);
  reportCheck("setChannelMask(0xA5)", st.ok(), st.ok() ? "" : errToStr(st.code));
  if (st.ok()) {
    uint8_t m = 0;
    st = device.readChannelMask(m);
    reportCheck("verify mask 0xA5", st.ok() && m == 0xA5, "");
  }

  // enableChannels
  st = device.setChannelMask(0x01);
  if (st.ok()) {
    st = device.enableChannels(0x04);
    reportCheck("enableChannels(0x04)", st.ok(), st.ok() ? "" : errToStr(st.code));
    if (st.ok()) {
      uint8_t m = 0;
      st = device.readChannelMask(m);
      reportCheck("verify enable result 0x05", st.ok() && m == 0x05, "");
    }
  } else {
    reportCheck("enableChannels prep", false, errToStr(st.code));
  }

  // disableChannels
  st = device.setChannelMask(0x0F);
  if (st.ok()) {
    st = device.disableChannels(0x0A);
    reportCheck("disableChannels(0x0A)", st.ok(), st.ok() ? "" : errToStr(st.code));
    if (st.ok()) {
      uint8_t m = 0;
      st = device.readChannelMask(m);
      reportCheck("verify disable result 0x05", st.ok() && m == 0x05, "");
    }
  } else {
    reportCheck("disableChannels prep", false, errToStr(st.code));
  }

  // isChannelEnabled
  st = device.setChannelMask(0x42);
  if (st.ok()) {
    bool en = false;
    st = device.isChannelEnabled(1, en);
    reportCheck("isChannelEnabled(1)=true", st.ok() && en, st.ok() ? "" : errToStr(st.code));
    st = device.isChannelEnabled(0, en);
    reportCheck("isChannelEnabled(0)=false", st.ok() && !en, st.ok() ? "" : errToStr(st.code));
  } else {
    reportCheck("isChannelEnabled prep", false, errToStr(st.code));
  }

  // Invalid channel
  st = device.selectChannel(8);
  reportCheck("selectChannel(8) rejects", st.code == TCA9548A::Err::INVALID_PARAM, "");

  // Recovery
  st = device.recover();
  reportCheck("recover", st.ok(), st.ok() ? "" : errToStr(st.code));
  reportCheck("isOnline", device.isOnline(), "");

  // Restore original mask
  device.setChannelMask(origMask);
  (void)reportSkip; // suppress unused warning

  Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                goodIfNonZeroColor(stats.pass), static_cast<unsigned long>(stats.pass), LOG_COLOR_RESET,
                goodIfZeroColor(stats.fail), static_cast<unsigned long>(stats.fail), LOG_COLOR_RESET,
                skipCountColor(stats.skip), static_cast<unsigned long>(stats.skip), LOG_COLOR_RESET);
}

// ============================================================================
// Command Processing
// ============================================================================

void processCommand(const String& cmd) {
  if (cmd == "help" || cmd == "?") {
    printHelp();
    return;
  }
  if (cmd == "version" || cmd == "ver") {
    printVersionInfo();
    return;
  }
  if (cmd == "scan") {
    i2c::scan();
    return;
  }
  if (cmd == "begin") {
    doBegin();
    return;
  }
  if (cmd == "end") {
    device.end();
    LOGI("Driver shut down");
    return;
  }
  if (cmd == "probe") {
    TCA9548A::Status st = device.probe();
    Serial.print("probe: ");
    printStatus(st);
    return;
  }
  if (cmd == "recover") {
    TCA9548A::Status st = device.recover();
    Serial.print("recover: ");
    printStatus(st);
    return;
  }
  if (cmd == "drv") {
    printDriverHealth();
    return;
  }
  if (cmd == "cfg" || cmd == "settings") {
    printConfig();
    return;
  }
  if (cmd == "verbose") {
    LOGI("Verbose mode: %s%s%s", onOffColor(verboseMode), verboseMode ? "ON" : "OFF", LOG_COLOR_RESET);
    return;
  }
  if (cmd.startsWith("verbose ")) {
    int val = cmd.substring(8).toInt();
    verboseMode = (val != 0);
    LOGI("Verbose mode: %s%s%s", onOffColor(verboseMode), verboseMode ? "ON" : "OFF", LOG_COLOR_RESET);
    return;
  }
  if (cmd.startsWith("select ")) {
    int ch = atoi(cmd.c_str() + 7);
    TCA9548A::Status st = device.selectChannel(static_cast<uint8_t>(ch));
    Serial.printf("select(%d): ", ch);
    printStatus(st);
    if (st.ok()) { printMask(static_cast<uint8_t>(1U << ch)); }
    return;
  }
  if (cmd.startsWith("mask ")) {
    uint8_t m = static_cast<uint8_t>(strtoul(cmd.c_str() + 5, nullptr, 16));
    TCA9548A::Status st = device.setChannelMask(m);
    Serial.printf("mask(0x%02X): ", m);
    printStatus(st);
    if (st.ok()) { printMask(m); }
    return;
  }
  if (cmd.startsWith("enable ")) {
    int ch = atoi(cmd.c_str() + 7);
    if (ch < 0 || ch > 7) { LOGE("Channel must be 0-7"); return; }
    TCA9548A::Status st = device.enableChannels(static_cast<uint8_t>(1U << ch));
    Serial.printf("enable(%d): ", ch);
    printStatus(st);
    return;
  }
  if (cmd.startsWith("disable ")) {
    int ch = atoi(cmd.c_str() + 8);
    if (ch < 0 || ch > 7) { LOGE("Channel must be 0-7"); return; }
    TCA9548A::Status st = device.disableChannels(static_cast<uint8_t>(1U << ch));
    Serial.printf("disable(%d): ", ch);
    printStatus(st);
    return;
  }
  if (cmd == "off") {
    TCA9548A::Status st = device.disableAll();
    Serial.print("off: ");
    printStatus(st);
    return;
  }
  if (cmd == "read") {
    uint8_t mask = 0;
    TCA9548A::Status st = device.readChannelMask(mask);
    Serial.print("read: ");
    printStatus(st);
    if (st.ok()) { printMask(mask); }
    return;
  }
  if (cmd.startsWith("check ")) {
    int ch = atoi(cmd.c_str() + 6);
    bool enabled = false;
    TCA9548A::Status st = device.isChannelEnabled(static_cast<uint8_t>(ch), enabled);
    Serial.printf("check(%d): ", ch);
    printStatus(st);
    if (st.ok()) {
      Serial.printf("  Channel %d: %s%s%s\n", ch,
                    onOffColor(enabled), enabled ? "ENABLED" : "DISABLED", LOG_COLOR_RESET);
    }
    return;
  }
  if (cmd == "health") {
    printDriverHealth();
    return;
  }
  if (cmd == "state") {
    const auto st = device.state();
    const bool online = device.isOnline();
    Serial.printf("  State: %s%s%s  Online: %s%s%s\n",
                  LOG_COLOR_STATE(online, device.consecutiveFailures()),
                  stateToStr(st), LOG_COLOR_RESET,
                  onOffColor(online), online ? "yes" : "no", LOG_COLOR_RESET);
    return;
  }
  if (cmd.startsWith("scanch ")) {
    int ch = atoi(cmd.c_str() + 7);
    if (ch < 0 || ch > 7) { LOGE("Channel must be 0-7"); return; }
    LOGI("Selecting channel %d...", ch);
    TCA9548A::Status st = device.selectChannel(static_cast<uint8_t>(ch));
    if (!st.ok()) {
      Serial.print("select failed: ");
      printStatus(st);
      return;
    }
    LOGI("Scanning downstream bus on channel %d:", ch);
    i2c::scan();
    return;
  }
  if (cmd.startsWith("addr ")) {
    uint8_t a = static_cast<uint8_t>(strtoul(cmd.c_str() + 5, nullptr, 16));
    Serial.printf("Setting address to 0x%02X and reinitializing...\n", a);
    device.end();
    gConfig.i2cAddress = a;
    doBegin();
    return;
  }
  if (cmd.startsWith("stress_mix")) {
    int count = 10;
    if (cmd.length() > 10) {
      count = cmd.substring(11).toInt();
    }
    runStressMix(count);
    return;
  }
  if (cmd.startsWith("stress")) {
    int count = 10;
    if (cmd.length() > 6) {
      count = cmd.substring(7).toInt();
    }
    runStress(count);
    return;
  }
  if (cmd == "selftest") {
    runSelfTest();
    return;
  }
  LOGW("Unknown command: %s (type 'help')", cmd.c_str());
}

// ============================================================================
// Arduino Setup / Loop
// ============================================================================

void setup() {
  log_begin(115200);
  delay(1000);

  Serial.println(F("\n============================="));
  Serial.println(F("  TCA9548A Bringup CLI"));
  Serial.println(F("============================="));

  printVersionInfo();

  LOGI("Initializing I2C...");
  board::initI2c();
  bus_diag::scan();

  initConfig();
  doBegin();
  printDriverHealth();

  printHelp();
  Serial.print(F("\n> "));
}

void loop() {
  device.tick(millis());

  String line;
  if (cli_shell::readLine(line)) {
    processCommand(line);
    Serial.print(F("\n> "));
  }
}
