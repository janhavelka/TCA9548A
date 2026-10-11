#include "Tca9548aCli.h"
#include "CliArguments.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace tca9548a_cli {
namespace {
using TCA9548A::Channel;
using TCA9548A::ChannelMask;
using TCA9548A::Err;
using TCA9548A::Status;
using TCA9548A::errorName;
constexpr uint32_t MAX_COUNT = 1000;
constexpr uint32_t MAX_INTERVAL_MS = 60000;
constexpr uint32_t MAX_SOAK_SECONDS = 86400;
constexpr const char* RESET = "\033[0m";
constexpr const char* CYAN = "\033[36m";
constexpr const char* GREEN = "\033[32m";
constexpr const char* RED = "\033[31m";
constexpr const char* YELLOW = "\033[33m";

bool equals(const char* left, const char* right) {
  return std::strcmp(left, right) == 0;
}

using cli_shell::parseUnsigned;

bool boolean(const char* text, bool& output) {
  if (equals(text, "on") || equals(text, "1")) { output = true; return true; }
  if (equals(text, "off") || equals(text, "0")) { output = false; return true; }
  return false;
}

Status mismatch(ChannelMask expected, ChannelMask observed) {
  return Status::Error(Err::I2C_ERROR, "control-byte readback mismatch",
                       (static_cast<int32_t>(expected.raw()) << 8) | observed.raw());
}

void increment(uint32_t& value) {
  if (value != UINT32_MAX) ++value;
}
}  // namespace

void TransferStats::record(Kind kind, bool ok) {
  increment(attempts);
  increment(ok ? successes : failures);
  increment(kind == Kind::READ ? reads : kind == Kind::WRITE ? writes : probes);
}

void Cli::setup(const Platform& platform) {
  if (active()) { result("setup", Status::Error(Err::BUSY, "CLI work is active")); return; }
  _platform = platform;
  _lineBuffer.reset();
}

void Cli::print(const char* format, ...) {
  if (_platform.writeText == nullptr) return;
  char text[384];
  va_list args;
  va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  _platform.writeText(text, _platform.user);
}

const char* Cli::color(const char* escape) const { return _color ? escape : ""; }
uint32_t Cli::now() const { return _platform.nowMs ? _platform.nowMs(_platform.user) : 0; }
BusInfo Cli::busInfo() const { return _platform.busInfo ? _platform.busInfo(_platform.user) : BusInfo{}; }
bool Cli::active() const { return _session.running; }

void Cli::section(const char* title) { print("%s=== %s ===%s\n", color(CYAN), title, color(RESET)); }
void Cli::helpItem(const char* command, const char* description) {
  print("  %s%-32s%s - %s\n", color(CYAN), command, color(RESET), description);
}
void Cli::printPrompt() { if (!active()) print("> "); }

void Cli::status(Status value) {
  print("%s%s%s", color(value.ok() ? GREEN : RED), errorName(value.code), color(RESET));
  if (!value.ok()) print(" (detail=%ld, %s)", static_cast<long>(value.detail), value.msg ? value.msg : "");
}
void Cli::result(const char* operation, Status value) { print("%s: ", operation); status(value); print("\n"); }
void Cli::invalid(const char* usage) {
  print("%s[E]%s Unknown or invalid command; usage: %s\n", color(RED), color(RESET), usage);
}

void Cli::printMask(ChannelMask mask) {
  print("0x%02X [", static_cast<unsigned>(mask.raw()));
  bool first = true;
  for (uint8_t channel = 0; channel < TCA9548A::cmd::NUM_CHANNELS; ++channel) {
    if (mask.contains(static_cast<Channel>(channel))) {
      print("%s%u", first ? "" : ",", static_cast<unsigned>(channel)); first = false;
    }
  }
  if (first) print("none");
  print("]");
}

void Cli::printObservation() {
  const auto observation = _device.channelMaskObservation();
  print("Mask cache: %s known=%s verified=%s value=",
        TCA9548A::maskProvenanceName(observation.provenance), observation.known() ? "yes" : "no",
        observation.verified() ? "yes" : "no");
  printMask(observation.mask); print("\n");
}

void Cli::printVersion() {
  section("Version Info");
  print("  Library: %s\n  Full: %s\n  Code: %lu\n", TCA9548A::VERSION,
        TCA9548A::VERSION_FULL, static_cast<unsigned long>(TCA9548A::VERSION_CODE));
  if (_platform.printVersion) _platform.printVersion(_platform.user);
}

void Cli::printHealth() {
  section("Driver Health");
  print("  State: %s (passive; never gates I2C)\n", TCA9548A::driverStateName(_device.state()));
  print("  State alias parity: %s\n", _device.driverState() == _device.state() ? "yes" : "no");
  print("  Bound: %s\n  Initialized: %s\n", _device.isBound() ? "yes" : "no", _device.isInitialized() ? "yes" : "no");
  print("  Online: %s\n  Consecutive failures: %u\n", _device.isOnline() ? "yes" : "no", static_cast<unsigned>(_device.consecutiveFailures()));
  print("  Total success/failure: %lu/%lu\n", static_cast<unsigned long>(_device.totalSuccess()), static_cast<unsigned long>(_device.totalFailures()));
  print("  Last OK/error ms: %lu/%lu\n", static_cast<unsigned long>(_device.lastOkMs()), static_cast<unsigned long>(_device.lastErrorMs()));
  print("  Last error: "); status(_device.lastError()); print("\n");
  printObservation();
}

void Cli::printConfig() {
  TCA9548A::SettingsSnapshot snapshot;
  const auto snapshotStatus = _device.getSettings(snapshot);
  const auto& bound = _device.getConfig();
  const auto& settings = snapshot.bound ? bound : _config;
  const auto bus = busInfo();
  const bool parity = !snapshot.bound ||
      (bound.i2cAddress == snapshot.i2cAddress && bound.i2cTimeoutMs == snapshot.i2cTimeoutMs &&
       bound.resetTimeoutMs == snapshot.resetTimeoutMs && bound.offlineThreshold == snapshot.offlineThreshold);
  section("Configuration");
  print("  Bound: %s\n  Initialized: %s\n  Settings source: %s\n",
        snapshot.bound ? "yes" : "no", snapshot.initialized ? "yes" : "no", snapshot.bound ? "bound" : "staged (unbound)");
  print("  I2C address: 0x%02X\n  I2C frequency: %lu Hz\n", settings.i2cAddress, static_cast<unsigned long>(bus.frequencyHz));
  print("  Controller restart required: %s\n", bus.receiveFaulted ? "yes" : "no");
  print("  I2C timeout: %lu ms\n  RESET timeout: %lu ms\n", static_cast<unsigned long>(settings.i2cTimeoutMs), static_cast<unsigned long>(settings.resetTimeoutMs));
  print("  nowMs hook: %s\n  RESET callback: %s\n", settings.nowMs ? "configured" : "not configured", settings.hardReset ? "configured" : "not configured");
  print("  Offline threshold: %u (diagnostic only)\n", static_cast<unsigned>(settings.offlineThreshold));
  print("  Config reference parity: %s\n  Snapshot: ", parity ? "yes" : "no"); status(snapshotStatus); print("\n");
}

void Cli::printBus() {
  const auto bus = busInfo();
  section("Application Bus");
  print("  Ready: %s\n  Controller restart required: %s\n", bus.ready ? "yes" : "no", bus.receiveFaulted ? "yes" : "no");
  print("  SDA: %d\n  SCL: %d\n  RESET: %d\n", bus.sdaPin, bus.sclPin, bus.resetPin);
  print("  I2C frequency: %lu Hz\n  Owner address: 0x%02X\n", static_cast<unsigned long>(bus.frequencyHz), bus.address);
  print("  Cached application settings; no bus I/O.\n");
}

void Cli::printTransferStats() {
  if (!_platform.transferStats) { result("xfer_stats", Status::Error(Err::UNSUPPORTED, "physical counters unavailable")); return; }
  const auto stats = _platform.transferStats(_platform.user);
  print("Transfers: attempts=%lu success=%lu failure=%lu read=%lu write=%lu probe=%lu saturated=%s\n",
        static_cast<unsigned long>(stats.attempts), static_cast<unsigned long>(stats.successes), static_cast<unsigned long>(stats.failures),
        static_cast<unsigned long>(stats.reads), static_cast<unsigned long>(stats.writes), static_cast<unsigned long>(stats.probes),
        stats.attempts == UINT32_MAX ? "yes" : "no");
}

void Cli::printHelp() {
  section("TCA9548A CLI Help");
  print("\n%s[Common]%s\n", color(GREEN), color(RESET));
  helpItem("help / ?", "Show this help");
  helpItem("version / ver", "Library, MCU and firmware identity");
  helpItem("color [on|off]", "Show/set ANSI colors (also 0|1)");
  helpItem("verbose [0|1]", "Show/set each watch/stress readback");
  helpItem("bus / diag", "Cached bus / version, config and health");
  helpItem("begin / init", "Probe address, bind, then verify safe-off");
  helpItem("end / unbind", "Bus-silent release; stop active work first");
  helpItem("scan", "112 address probes in the observed topology");
  helpItem("discover / detect", "Probe 0x70..0x77; ACK is not chip identity");
  print("\n%s[Configuration]%s\n", color(GREEN), color(RESET));
  helpItem("cfg / settings / snapshot", "Bound settings or staged settings while ended");
  helpItem("addr [0x70..0x77]", "Show/set target while ended; no I2C");
  helpItem("timeout [1..60000]", "Show/set transfer timeout while ended");
  helpItem("resettimeout [1..60000]", "Show/set RESET timeout while ended");
  helpItem("offline [1..255]", "Show/set passive threshold while ended");
  print("\n%s[Switch]%s\n", color(GREEN), color(RESET));
  helpItem("read / dump", "Read and verify the control byte");
  helpItem("select <0-7>", "Select one channel, disable the rest");
  helpItem("mask <0-255>", "Write any channel combination");
  helpItem("enable / disable <0-7>", "Fresh read then enable/disable one channel");
  helpItem("verify [0-255]", "Compare live mask to expected value or cache");
  helpItem("off / recover", "One all-off write; recover never resets bus");
  helpItem("reset / hardreset", "Optional RESET callback then verify zero");
  helpItem("invalidate", "Mark cached mask unknown; no I2C");
  print("\n%s[Diagnostics]%s\n", color(GREEN), color(RESET));
  helpItem("health / drv / state / online", "Cached passive health and last full error");
  helpItem("probe", "Raw read; no tracked health effects");
  helpItem("selftest", "Live mask tests; restore entry mask");
  helpItem("hil [dry|parser|run|run reset]", "No-I2C checks / live tests with optional RESET");
  helpItem("sweep", "Verify all 256 masks; restore entry mask");
  helpItem("stress / stress_mix [1-1000]", "Readback-verified rounds; finish all-off");
  helpItem("watch [N [interval_ms]]", "1..1000 reads; defaults 20 at 1000 ms");
  helpItem("soak <1..86400 seconds>", "Read once per second for a finite duration");
  helpItem("stop / cancel", "Stop scheduling; verify mutating-test cleanup");
  helpItem("stats / result [reset]", "Cached current/last run and cleanup evidence");
  helpItem("xfer_stats / counters", "Physical adapter attempts, including probes");
  helpItem("xfer_reset", "Reset physical counters while idle");
  helpItem("xfer_assert N [ok fail]", "Assert exact unsaturated physical counts");
  print("\nNumbers accept decimal, 0x hex and leading-zero octal.\n");
  print("Mask tests change routing. Do not combine branches with conflicting addresses.\n");
  print("Prompt appears after completion; stats and stop remain usable during a run.\n");
}

Status Cli::verifyMask(ChannelMask expected) {
  ChannelMask observed;
  const auto value = recordTransfer(_device.readChannelMask(observed));
  if (!value.ok()) return value;
  if (observed.raw() != expected.raw()) return mismatch(expected, observed);
  return Status::Ok();
}

Status Cli::restoreMask(ChannelMask mask) {
  const auto write = _device.writeChannelMask(mask);
  if (!write.ok()) result("restore write", write);
  const auto read = verifyMask(mask);
  if (!read.ok()) result("restore readback", read);
  return !write.ok() ? write : read;
}

void Cli::beginDriver() {
  if (active() || _device.isBound()) { result("begin", Status::Error(Err::BUSY, "stop work and end before rebinding")); return; }
  // The extra address-only preflight also needs valid arguments. Keep invalid
  // example configuration bus-silent before the core performs its own checks.
  if (!_config.i2cWrite || !_config.i2cWriteRead ||
      !TCA9548A::cmd::isValidAddress(_config.i2cAddress) ||
      _config.i2cTimeoutMs == 0 || _config.i2cTimeoutMs > 60000 ||
      _config.resetTimeoutMs == 0 || _config.resetTimeoutMs > 60000 ||
      _config.offlineThreshold == 0) {
    result("begin", Status::Error(Err::INVALID_CONFIG, "invalid staged driver settings"));
    return;
  }
  const auto bus = busInfo();
  if (!bus.ready || bus.receiveFaulted) { result("begin", Status::Error(Err::NOT_INITIALIZED, "I2C controller unavailable")); return; }
  auto value = _platform.prepareAddress ? _platform.prepareAddress(_config.i2cAddress, _platform.user) : Status::Ok();
  if (!value.ok()) { result("begin prepare", value); return; }
  if (!_platform.probeAddress) { result("begin", Status::Error(Err::UNSUPPORTED, "address-only presence adapter required")); return; }
  value = _platform.probeAddress(_config.i2cAddress, _config.i2cTimeoutMs, _platform.user);
  if (!value.ok()) { result("begin presence", value); return; }
  value = _device.begin(_config);
  print("begin: "); status(value); print(" (bound=%s)\n", _device.isBound() ? "yes" : "no");
  if (!_device.isBound()) return;
  const auto off = _device.disableAll();
  const auto verified = off.ok() ? verifyMask(ChannelMask::none()) : off;
  if (!verified.ok()) result("safe-off", verified);
  print("startup safe-off: %s\n", verified.ok() ? "OK (verified 0x00)" : "FAILED");
}

void Cli::feed(char value) {
  char command[cli_shell::FixedLineBuffer::CAPACITY];
  const auto state = _lineBuffer.push(value, command, sizeof(command));
  if (state == cli_shell::LineResult::READY) processCommand(command);
  else if (state != cli_shell::LineResult::NONE) {
    print("[E] Command discarded: %s\n", state == cli_shell::LineResult::TOO_LONG ? "line too long" : "invalid input");
    printPrompt();
  }
}

void Cli::processCommand(const char* command) {
  char buffer[cli_shell::FixedLineBuffer::CAPACITY]{};
  const char* args[4]{};
  size_t size = 0;
  while (command && size < sizeof(buffer) && command[size]) {
    const auto ch = static_cast<unsigned char>(command[size]);
    if ((ch < 0x20U && ch != '\t') || ch == 0x7FU) { invalid("printable command"); printPrompt(); return; }
    ++size;
  }
  if (!command || size >= sizeof(buffer)) { invalid("command up to 127 bytes"); printPrompt(); return; }
  std::memcpy(buffer, command, size + 1U);
  unsigned count = 0;
  char* cursor = buffer;
  while (*cursor) {
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (!*cursor) break;
    if (count == 4) { invalid("at most four command tokens"); printPrompt(); return; }
    args[count++] = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
    if (*cursor) *cursor++ = '\0';
  }
  if (!count) return;
  const auto name = args[0];
  uint32_t value = 0;
  _inCommand = true;
  do {
    if (count == 1 && (equals(name, "help") || equals(name, "?"))) { printHelp(); break; }
    if (count == 1 && (equals(name, "version") || equals(name, "ver"))) { printVersion(); break; }
    if (count == 1 && (equals(name, "cfg") || equals(name, "settings") || equals(name, "snapshot"))) { printConfig(); break; }
    if (count == 1 && (equals(name, "health") || equals(name, "drv") || equals(name, "state") || equals(name, "online"))) { printHealth(); break; }
    if (count == 1 && equals(name, "bus")) { printBus(); break; }
    if (count == 1 && equals(name, "diag")) { printVersion(); printConfig(); printHealth(); printBus(); break; }
    if (equals(name, "color") || equals(name, "verbose")) {
      bool setting = equals(name, "color") ? _color : _verbose;
      if (count > 2 || (count == 2 && !boolean(args[1], setting))) { invalid("color|verbose [0|1|off|on]"); break; }
      if (equals(name, "color")) _color = setting; else _verbose = setting;
      print("%s: %s\n", name, setting ? "on" : "off"); break;
    }
    if (equals(name, "stats") || equals(name, "result")) {
      if (count == 1) printRunStats();
      else if (count == 2 && equals(args[1], "reset") && !active()) { _session = Session{}; print("stats: OK (run evidence cleared; health unchanged)\n"); }
      else invalid("stats [reset while idle]");
      break;
    }
    if (count == 1 && (equals(name, "xfer_stats") || equals(name, "counters"))) { printTransferStats(); break; }
    if (equals(name, "xfer_assert")) {
      uint32_t ok = 0, failed = 0;
      if ((count != 2 && count != 4) || !parseUnsigned(args[1], 0, UINT32_MAX - 1, value) ||
          (count == 4 && (!parseUnsigned(args[2], 0, UINT32_MAX - 1, ok) || !parseUnsigned(args[3], 0, UINT32_MAX - 1, failed)))) {
        invalid("xfer_assert <attempts> [success failures]"); break;
      }
      if (!_platform.transferStats) { result(name, Status::Error(Err::UNSUPPORTED, "physical counters unavailable")); break; }
      const auto counters = _platform.transferStats(_platform.user);
      const bool matched = counters.attempts != UINT32_MAX && counters.attempts == value &&
          (count == 2 || (counters.successes == ok && counters.failures == failed));
      print("xfer_assert: %s attempts=%lu\n", matched ? "PASS" : "FAIL", static_cast<unsigned long>(counters.attempts)); break;
    }
    if (count == 1 && (equals(name, "stop") || equals(name, "cancel"))) { stop(); break; }
    if (active()) { result(name, Status::Error(Err::BUSY, "diagnostic active; use stop first")); break; }
    if (count == 1 && equals(name, "xfer_reset")) {
      if (_platform.resetTransferStats) { _platform.resetTransferStats(_platform.user); print("xfer_reset: OK (driver health unchanged)\n"); }
      else result(name, Status::Error(Err::UNSUPPORTED, "physical counter reset unavailable"));
      break;
    }
    if (equals(name, "addr") || equals(name, "timeout") || equals(name, "resettimeout") || equals(name, "offline")) {
      if (count == 1) { printConfig(); break; }
      const uint32_t minimum = equals(name, "addr") ? 0x70 : 1;
      const uint32_t maximum = equals(name, "addr") ? 0x77 : equals(name, "offline") ? 255 : 60000;
      if (count != 2 || !parseUnsigned(args[1], minimum, maximum, value)) { invalid("addr 0x70..0x77 | timeout/resettimeout 1..60000 | offline 1..255"); break; }
      if (_device.isBound()) { result(name, Status::Error(Err::BUSY, "end driver before changing settings")); break; }
      if (equals(name, "addr")) _config.i2cAddress = static_cast<uint8_t>(value);
      else if (equals(name, "offline")) _config.offlineThreshold = static_cast<uint8_t>(value);
      else if (equals(name, "timeout")) _config.i2cTimeoutMs = value;
      else _config.resetTimeoutMs = value;
      printConfig(); break;
    }
    if (count == 1 && (equals(name, "begin") || equals(name, "init"))) { beginDriver(); break; }
    if (count == 1 && (equals(name, "end") || equals(name, "unbind"))) { _device.end(); print("end: OK (no bus I/O)\n"); break; }
    if (count == 1 && equals(name, "invalidate")) { _device.invalidateChannelMask(); print("invalidate: OK (no bus I/O)\n"); printObservation(); break; }
    if (count == 1 && (equals(name, "read") || equals(name, "dump"))) {
      ChannelMask mask; const auto read = _device.readChannelMask(mask);
      print("read: "); status(read); if (read.ok()) { print(" mask="); printMask(mask); } print("\n"); break;
    }
    if (count == 1 && equals(name, "off")) { result("off", _device.disableAll()); break; }
    if (count == 1 && equals(name, "recover")) { result("recover (safe-off write)", _device.recover()); break; }
    if (count == 1 && equals(name, "probe")) { result("probe", _device.probe()); printObservation(); break; }
    if (count == 1 && (equals(name, "reset") || equals(name, "hardreset"))) { result("hardreset", _device.hardReset()); printObservation(); break; }
    if (equals(name, "select") || equals(name, "mask") || equals(name, "enable") || equals(name, "disable")) {
      if (count != 2 || !parseUnsigned(args[1], 0, equals(name, "mask") ? 255 : 7, value)) { invalid("select/enable/disable <0-7> | mask <0-255>"); break; }
      Status outcome;
      if (equals(name, "mask")) { outcome = _device.writeChannelMask(ChannelMask::fromRaw(static_cast<uint8_t>(value))); print("mask 0x%02lX: ", static_cast<unsigned long>(value)); }
      else if (equals(name, "select")) { outcome = _device.selectChannel(static_cast<Channel>(value)); print("select %lu: ", static_cast<unsigned long>(value)); }
      else {
        ChannelMask observed; outcome = _device.readChannelMask(observed);
        if (outcome.ok()) {
          const auto bit = ChannelMask::one(static_cast<Channel>(value));
          outcome = _device.writeChannelMask(equals(name, "enable") ? observed.withEnabled(bit) : observed.withDisabled(bit));
        }
        print("%s %lu: ", name, static_cast<unsigned long>(value));
      }
      status(outcome); print("\n"); break;
    }
    if (equals(name, "verify")) {
      if (count > 2 || (count == 2 && !parseUnsigned(args[1], 0, 255, value))) { invalid("verify [0-255]"); break; }
      const auto cached = _device.channelMaskObservation();
      if (count == 1 && !cached.known()) { result(name, Status::Error(Err::INVALID_PARAM, "cached mask unknown; supply expected byte")); break; }
      const auto expected = count == 2 ? ChannelMask::fromRaw(static_cast<uint8_t>(value)) : cached.mask;
      result("verify", verifyMask(expected)); break;
    }
    if (equals(name, "hil") || equals(name, "selftest")) {
      const bool selftest = equals(name, "selftest");
      const bool dry = !selftest && (count == 1 || (count == 2 && (equals(args[1], "dry") || equals(args[1], "parser"))));
      const bool live = (selftest && count == 1) || (!selftest && count >= 2 && equals(args[1], "run") &&
          (count == 2 || (count == 3 && equals(args[2], "reset"))));
      if (!dry && !live) { invalid("selftest | hil [dry|parser|run|run reset]"); break; }
      runHil(dry, count == 3, count == 2 && equals(args[1], "parser")); break;
    }
    if (count == 1 && (equals(name, "scan") || equals(name, "discover") || equals(name, "detect"))) {
      start(equals(name, "scan") ? Run::SCAN : Run::DISCOVER, equals(name, "scan") ? 112 : 8); break;
    }
    if (count == 1 && equals(name, "sweep")) { start(Run::SWEEP, 256); break; }
    if (equals(name, "stress") || equals(name, "stress_mix")) {
      value = 100;
      if (count > 2 || (count == 2 && !parseUnsigned(args[1], 1, MAX_COUNT, value))) { invalid("stress/stress_mix [1-1000]"); break; }
      start(equals(name, "stress") ? Run::STRESS : Run::MIX, value); break;
    }
    if (equals(name, "watch")) {
      value = 20; uint32_t interval = 1000;
      if (count > 3 || (count >= 2 && !parseUnsigned(args[1], 1, MAX_COUNT, value)) ||
          (count == 3 && !parseUnsigned(args[2], 1, MAX_INTERVAL_MS, interval))) { invalid("watch [1-1000 [1-60000 ms]]"); break; }
      start(Run::WATCH, value, interval); break;
    }
    if (equals(name, "soak")) {
      if (count != 2 || !parseUnsigned(args[1], 1, MAX_SOAK_SECONDS, value)) { invalid("soak <1-86400 seconds>"); break; }
      start(Run::SOAK, value, 1000); break;
    }
    invalid(command);
  } while (false);
  _inCommand = false;
  printPrompt();
}

void Cli::check(HilCounts& counts, const char* label, bool passed, const char* detail) {
  if (passed) ++counts.passed; else ++counts.failed;
  print("  [%s%s%s] %s", color(passed ? GREEN : RED), passed ? "PASS" : "FAIL", color(RESET), label);
  if (detail && *detail) print(" - %s", detail);
  print("\n");
}
void Cli::skip(HilCounts& counts, const char* label, const char* detail) {
  ++counts.skipped;
  print("  [%sSKIP%s] %s - %s\n", color(YELLOW), color(RESET), label, detail);
}

void Cli::runHilMaskChecks(HilCounts& counts, bool reset) {
  auto value = _device.disableAll();
  check(counts, "disableAll write", value.ok(), errorName(value.code));
  if (!value.ok()) return;
  value = verifyMask(ChannelMask::none());
  check(counts, "disableAll readback", value.ok(), errorName(value.code));
  if (!value.ok()) return;
  for (uint8_t channel = 0; channel < 8; ++channel) {
    value = _device.selectChannel(static_cast<Channel>(channel));
    if (value.ok()) value = verifyMask(ChannelMask::one(static_cast<Channel>(channel)));
    if (!value.ok()) break;
  }
  check(counts, "all eight one-hot channels", value.ok(), errorName(value.code));
  if (!value.ok()) return;
  value = _device.writeChannelMask(ChannelMask::fromRaw(0xA5));
  check(counts, "write mask 0xA5", value.ok(), errorName(value.code));
  if (!value.ok()) return;
  value = verifyMask(ChannelMask::fromRaw(0xA5));
  check(counts, "read mask 0xA5", value.ok(), errorName(value.code));
  if (!value.ok()) return;
  value = _device.recover();
  check(counts, "recover safe-off write", value.ok(), errorName(value.code));
  if (!value.ok()) return;
  value = verifyMask(ChannelMask::none());
  check(counts, "recover readback 0x00", value.ok(), errorName(value.code));
  if (!value.ok()) return;
  if (!reset) { skip(counts, "hardReset", "use 'hil run reset' to include RESET"); return; }
  if (!_config.hardReset) { check(counts, "hardReset", false, "callback not configured"); return; }
  value = _device.selectChannel(Channel::CH0);
  check(counts, "hardReset seed write", value.ok(), errorName(value.code));
  if (!value.ok()) return;
  value = verifyMask(ChannelMask::one(Channel::CH0));
  check(counts, "hardReset seed readback", value.ok(), errorName(value.code));
  if (!value.ok()) return;
  value = _device.hardReset();
  check(counts, "hardReset exact-zero verification", value.ok(), errorName(value.code));
  const auto observed = _device.channelMaskObservation();
  check(counts, "hardReset leaves verified all-off", observed.verified() && observed.mask.isNone());
}

void Cli::runHil(bool dry, bool reset, bool parser) {
  section(dry ? "TCA9548A HIL DRY-RUN" : "TCA9548A HIL RUN");
  HilCounts counts;
  check(counts, "typed ChannelMask is one byte", sizeof(ChannelMask) == sizeof(uint8_t));
  check(counts, "address helper", TCA9548A::cmd::addressFromPins(false, false, false) == 0x70 && TCA9548A::cmd::addressFromPins(true, true, true) == 0x77);
  check(counts, "version", TCA9548A::VERSION[0] != '\0', TCA9548A::VERSION);
  if (parser) {
    uint32_t parsed = 19;
    bool valid = parseUnsigned("0xA5", 0, 255, parsed) && parsed == 0xA5;
    for (const char* bad : {"-1", "+1", "256", "1 extra", "999999999999999999999999999999"}) {
      parsed = 19;
      valid = !parseUnsigned(bad, 0, 255, parsed) && parsed == 19 && valid;
    }
    check(counts, "numeric parser rejects invalid input", valid);
    cli_shell::FixedLineBuffer input;
    char out[cli_shell::FixedLineBuffer::CAPACITY];
    (void)input.push('m', out, sizeof(out)); (void)input.push('\0', out, sizeof(out));
    const bool rejected = input.push('\n', out, sizeof(out)) == cli_shell::LineResult::INVALID_INPUT;
    for (size_t i = 0; i < cli_shell::FixedLineBuffer::CAPACITY; ++i) (void)input.push('x', out, sizeof(out));
    check(counts, "line parser rejects controls and overflow", rejected && input.push('\n', out, sizeof(out)) == cli_shell::LineResult::TOO_LONG);
  }
  if (dry) {
    skip(counts, "probe", "dry-run"); skip(counts, "mask I/O", "dry-run"); skip(counts, "hardReset", "not requested");
  } else {
    const uint32_t successes = _device.totalSuccess(), failures = _device.totalFailures();
    const auto state = _device.state(); const auto lastError = _device.lastError();
    const auto value = _device.probe();
    check(counts, "probe", value.ok(), errorName(value.code));
    const bool unchanged = successes == _device.totalSuccess() && failures == _device.totalFailures() && state == _device.state() &&
        lastError.code == _device.lastError().code && lastError.detail == _device.lastError().detail;
    check(counts, "probe no-health-side-effects", unchanged);
    if (value.ok() && unchanged) {
      ChannelMask original;
      const auto read = _device.readChannelMask(original);
      check(counts, "capture original mask readback", read.ok(), errorName(read.code));
      if (read.ok()) {
        runHilMaskChecks(counts, reset);
        const auto restored = restoreMask(original);
        check(counts, "final verified mask restore", restored.ok(), errorName(restored.code));
      }
    }
  }
  print("HIL result: pass=%u fail=%u skip=%u\n", counts.passed, counts.failed, counts.skipped);
}

const char* Cli::runName() const {
  switch (_session.kind) {
    case Run::SCAN: return "scan";
    case Run::DISCOVER: return "discover";
    case Run::WATCH: return "watch";
    case Run::STRESS: return "stress";
    case Run::MIX: return "stress_mix";
    case Run::SWEEP: return "sweep";
    case Run::SOAK: return "soak";
    default: return "none";
  }
}

void Cli::start(Run kind, uint32_t count, uint32_t intervalMs) {
  if (!_platform.nowMs) { result("run", Status::Error(Err::INVALID_CONFIG, "diagnostics require a monotonic clock")); return; }
  if (kind != Run::DISCOVER && !_device.isBound()) { result("run", Status::Error(Err::NOT_INITIALIZED, "driver is not bound")); return; }
  const auto bus = busInfo();
  if (!bus.ready || bus.receiveFaulted) { result("run", Status::Error(Err::NOT_INITIALIZED, "I2C controller unavailable")); return; }
  if ((kind == Run::SCAN || kind == Run::DISCOVER) && !_platform.probeAddress) { result("run", Status::Error(Err::UNSUPPORTED, "address probe unavailable")); return; }
  _session = Session{};
  _session.kind = kind; _session.available = true; _session.running = true;
  _session.target = count; _session.intervalMs = intervalMs;
  _session.startedMs = now(); _session.nextMs = _session.startedMs;
  _session.successBefore = _device.totalSuccess(); _session.failureBefore = _device.totalFailures();
  _session.address = kind == Run::SCAN ? 0x08 : 0x70;
  if (kind == Run::SOAK) _session.durationMs = count * 1000U;
  print("%s started: requested=%lu; stop cancels\n", runName(), static_cast<unsigned long>(count));
  if (kind == Run::DISCOVER) print("Address ACK candidates only; TCA9548A has no identity register.\n");
}

void Cli::fail(Status value) {
  if (_session.primary.ok()) _session.primary = value;
  if (_session.kind == Run::STRESS || _session.kind == Run::MIX ||
      (_session.kind == Run::SWEEP && _session.captured)) {
    _session.restoring = true; _session.phase = 0;
  } else finish();
}

Status Cli::recordTransfer(Status value) {
  if (active()) ++(value.ok() ? _session.transferSuccesses : _session.transferFailures);
  return value;
}

void Cli::stop() {
  if (!active()) { print("stop: OK (idle)\n"); return; }
  if (_session.restoring) { print("stop: cleanup already active; bounded write/read will finish\n"); return; }
  _session.cancelled = true;
  fail(Status::Error(Err::BUSY, "CLI session cancelled"));
}

void Cli::finish() {
  _session.running = false;
  _session.endedMs = now();
  _session.successAfter = _device.totalSuccess(); _session.failureAfter = _device.totalFailures();
  const bool success = _session.primary.ok() && _session.cleanup.ok() && !_session.cancelled;
  if (_session.kind == Run::STRESS || _session.kind == Run::MIX) {
    if (_session.kind == Run::MIX) print("=== stress_mix summary ===\n");
    const auto outcome = !_session.primary.ok() ? _session.primary : _session.cleanup;
    print("Stress results: completed=%lu requested=%lu status=%s safe_off=%s\n",
          static_cast<unsigned long>(_session.completed), static_cast<unsigned long>(_session.target), errorName(outcome.code), _session.cleanup.ok() ? "OK" : "FAILED");
    print("Duration: %lu ms\n", static_cast<unsigned long>(_session.endedMs - _session.startedMs));
    print("Health delta: success=%lu failure=%lu\n", static_cast<unsigned long>(_session.successAfter - _session.successBefore), static_cast<unsigned long>(_session.failureAfter - _session.failureBefore));
    print("Health delta saturated: %s\n", _session.successAfter == UINT32_MAX || _session.failureAfter == UINT32_MAX ? "yes" : "no");
    print("Session transfers: success=%lu failure=%lu\n", static_cast<unsigned long>(_session.transferSuccesses), static_cast<unsigned long>(_session.transferFailures));
    if (!_session.cleanup.ok()) print("  [FAIL] final safe-off was not verified\n");
  } else if (_session.kind == Run::SCAN || _session.kind == Run::DISCOVER) {
    print("%s %s: devices=%u errors=%u\n", _session.kind == Run::SCAN ? "Scan" : "Discover",
          _session.completed == _session.target ? "complete" : "aborted", _session.found, _session.scanErrors);
  } else if (_session.kind == Run::SWEEP) {
    print("Sweep result: completed=%lu requested=256 restore=%s outcome=%s\n", static_cast<unsigned long>(_session.completed),
          _session.captured ? (_session.cleanup.ok() ? "OK" : "FAILED") : "NOT_NEEDED", success ? "PASS" : "FAIL");
  }
  printRunStats();
  print("CLI result: command=%s outcome=%s completed=%lu requested=%lu\n", runName(), _session.cancelled ? "CANCELLED" : success ? "PASS" : "FAIL", static_cast<unsigned long>(_session.completed), static_cast<unsigned long>(_session.target));
  if (!_inCommand) printPrompt();
}

void Cli::printRunStats() {
  if (!_session.available) { print("No diagnostic run statistics.\n"); return; }
  print("Run: command=%s active=%s completed=%lu requested=%lu cancelled=%s duration_ms=%lu\n", runName(), active() ? "yes" : "no", static_cast<unsigned long>(_session.completed), static_cast<unsigned long>(_session.target), _session.cancelled ? "yes" : "no", static_cast<unsigned long>((active() ? now() : _session.endedMs) - _session.startedMs));
  result("Primary result", _session.primary);
  const bool needsCleanup = _session.kind == Run::STRESS || _session.kind == Run::MIX ||
      (_session.kind == Run::SWEEP && _session.captured);
  print("Cleanup: %s\n", !needsCleanup ? "not needed" : active() ? "pending" : _session.cleanup.ok() ? "verified" : "failed");
  result("Cleanup result", _session.cleanup);
}

void Cli::tickCleanup() {
  const auto expected = _session.kind == Run::SWEEP ? _session.original : ChannelMask::none();
  if (_session.phase == 0) {
    _session.cleanup = recordTransfer(_device.writeChannelMask(expected));
    _session.phase = 1;
    return;
  }
  const auto read = verifyMask(expected);
  if (_session.cleanup.ok()) _session.cleanup = read;
  if (!read.ok()) result("cleanup readback", read);
  finish();
}

void Cli::tickScan() {
  if (_session.kind == Run::SCAN && _session.phase == 0) {
    ChannelMask topology;
    const auto read = recordTransfer(_device.readChannelMask(topology));
    print("Scan topology: "); status(read);
    if (read.ok()) { print(" active_mask="); printMask(topology); } else print(" active_mask=unknown");
    print(" (select a one-hot mask before scan to isolate a branch)\n");
    if (!read.ok()) { fail(read); return; }
    _session.phase = 1; print("Scanning I2C bus (112 bounded probes)...\n"); return;
  }
  const auto value = _platform.probeAddress(static_cast<uint8_t>(_session.address), _config.i2cTimeoutMs, _platform.user);
  if (value.ok()) { ++_session.found; print("  Found device at 0x%02X\n", _session.address); }
  else if (!value.is(Err::I2C_NACK_ADDR)) {
    ++_session.scanErrors; print("  Scan error at 0x%02X: ", _session.address); status(value); print("\n");
    if (_session.primary.ok()) _session.primary = value;
    // Transport errors do not establish absence; stop this scan without retry.
    ++_session.completed; finish(); return;
  }
  ++_session.completed;
  if (++_session.address > 0x77) finish();
}

void Cli::tickMasks() {
  if (_session.kind == Run::SWEEP && !_session.captured) {
    const auto read = recordTransfer(_device.readChannelMask(_session.original));
    if (!read.ok()) { fail(read); return; }
    _session.captured = true; return;
  }
  Status value;
  bool roundComplete = false;
  if (_session.kind == Run::MIX) {
    switch (_session.phase) {
      case 0:
        _session.expected = ChannelMask::one(static_cast<Channel>(_session.completed % 8));
        value = recordTransfer(_device.selectChannel(static_cast<Channel>(_session.completed % 8))); break;
      case 1: value = verifyMask(_session.expected); break;
      case 2:
        _session.expected = ChannelMask::fromRaw(static_cast<uint8_t>(_session.completed * 37U + 0xA5U));
        value = recordTransfer(_device.writeChannelMask(_session.expected)); break;
      case 3: value = verifyMask(_session.expected); break;
      case 4: value = recordTransfer(_device.probe()); break;
      case 5: _session.expected = ChannelMask::none(); value = recordTransfer(_device.recover()); break;
      default: value = verifyMask(_session.expected); roundComplete = true; break;
    }
  } else {
    if (_session.phase == 0) {
      _session.expected = _session.kind == Run::SWEEP ? ChannelMask::fromRaw(static_cast<uint8_t>(_session.completed)) : ChannelMask::one(static_cast<Channel>(_session.completed % 8));
      value = recordTransfer(_session.kind == Run::SWEEP ? _device.writeChannelMask(_session.expected) : _device.selectChannel(static_cast<Channel>(_session.completed % 8)));
    } else { value = verifyMask(_session.expected); roundComplete = true; }
  }
  if (!value.ok()) { fail(value); return; }
  if (!roundComplete) { ++_session.phase; return; }
  ++_session.completed; _session.phase = 0;
  if (_verbose) print("%s round=%lu verified=0x%02X\n", runName(), static_cast<unsigned long>(_session.completed), _session.expected.raw());
  if (_session.completed == _session.target) { _session.restoring = true; _session.phase = 0; }
}

void Cli::tick() {
  _device.tick(now());
  if (!active()) return;
  if (_session.restoring) { tickCleanup(); return; }
  if (_session.kind == Run::SCAN || _session.kind == Run::DISCOVER) { tickScan(); return; }
  if (_session.kind == Run::STRESS || _session.kind == Run::MIX || _session.kind == Run::SWEEP) { tickMasks(); return; }
  const auto current = now();
  if (_session.kind == Run::SOAK && current - _session.startedMs >= _session.durationMs) {
    if (_session.completed == 0) _session.primary = Status::Error(Err::TIMEOUT, "soak expired before first sample");
    finish(); return;
  }
  if (static_cast<int32_t>(current - _session.nextMs) < 0) return;
  ChannelMask observed;
  const auto read = recordTransfer(_device.readChannelMask(observed));
  if (!read.ok()) { fail(read); return; }
  ++_session.completed;
  if (_verbose || _session.kind == Run::WATCH) print("%s sample=%lu mask=0x%02X timestamp_ms=%lu\n", runName(), static_cast<unsigned long>(_session.completed), observed.raw(), static_cast<unsigned long>(current));
  _session.nextMs = current + _session.intervalMs;
  if (_session.kind == Run::WATCH && _session.completed == _session.target) finish();
}

}  // namespace tca9548a_cli
