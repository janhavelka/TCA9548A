/**
 * @file main.cpp
 * @brief Native ESP-IDF bring-up CLI for TCA9548A.
 */

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_chip_info.h>
#include <esp_err.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "examples/common/CliLineBuffer.h"
#include "TCA9548A/TCA9548A.h"

namespace {

static constexpr gpio_num_t I2C_SDA = GPIO_NUM_8;
static constexpr gpio_num_t I2C_SCL = GPIO_NUM_9;
static constexpr int RESET_GPIO = -1;
static constexpr uint32_t I2C_FREQ_HZ = 400000U;
static constexpr uint32_t I2C_TIMEOUT_MS = 50U;
static constexpr uint32_t RESET_TIMEOUT_MS = 10U;
static constexpr uint32_t MAX_STRESS_COUNT = 1000U;
static constexpr size_t LINE_LEN = 128U;

static constexpr const char* COLOR_RESET = "\033[0m";
static constexpr const char* COLOR_RED = "\033[31m";
static constexpr const char* COLOR_GREEN = "\033[32m";
static constexpr const char* COLOR_YELLOW = "\033[33m";
static constexpr const char* COLOR_CYAN = "\033[36m";
static constexpr const char* COLOR_GRAY = "\033[90m";

struct NativeBus {
  i2c_master_bus_handle_t bus = nullptr;
  i2c_master_dev_handle_t device = nullptr;
  uint8_t deviceAddress = 0U;
};

NativeBus gBus;
TCA9548A::TCA9548A gDevice;
TCA9548A::Config gConfig;
bool gI2cReady = false;
cli_shell::FixedLineBuffer gLineBuffer;
using TCA9548A::errorName;
using TCA9548A::driverStateName;
using TCA9548A::maskProvenanceName;

void printSection(const char* title) {
  printf("%s=== %s ===%s\n", COLOR_CYAN, title, COLOR_RESET);
}

void printStatusValue(const TCA9548A::Status& status) {
  printf("%s%s%s", status.ok() ? COLOR_GREEN : COLOR_RED,
         errorName(status.code), COLOR_RESET);
  if (!status.ok()) {
    printf(" (detail=%ld, %s)", static_cast<long>(status.detail), status.msg);
  }
}

void printStatus(const char* operation, const TCA9548A::Status& status) {
  printf("%s: ", operation);
  printStatusValue(status);
  putchar('\n');
}

void printMask(TCA9548A::ChannelMask mask) {
  printf("0x%02X [", static_cast<unsigned>(mask.raw()));
  bool first = true;
  for (uint8_t index = 0; index < TCA9548A::cmd::NUM_CHANNELS; ++index) {
    const auto channel = static_cast<TCA9548A::Channel>(index);
    if (mask.contains(channel)) {
      printf("%s%u", first ? "" : ",", static_cast<unsigned>(index));
      first = false;
    }
  }
  if (first) {
    fputs("none", stdout);
  }
  putchar(']');
}

void printObservation() {
  const auto observation = gDevice.channelMaskObservation();
  printf("Mask cache: %s known=%s verified=%s value=",
         maskProvenanceName(observation.provenance),
         observation.known() ? "yes" : "no",
         observation.verified() ? "yes" : "no");
  printMask(observation.mask);
  putchar('\n');
}

uint32_t nowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

int timeoutArg(uint32_t timeoutMs) {
  return timeoutMs > static_cast<uint32_t>(INT_MAX)
             ? INT_MAX
             : static_cast<int>(timeoutMs);
}

TCA9548A::TransportStatus mapI2c(esp_err_t error) {
  if (error == ESP_OK) {
    return TCA9548A::TransportStatus::Ok();
  }
  if (error == ESP_ERR_TIMEOUT) {
    return TCA9548A::TransportStatus::Error(
        TCA9548A::TransportErr::TIMEOUT, error);
  }
  // The native API does not reliably distinguish address NACK, data NACK,
  // arbitration loss, and other controller failures in every supported IDF.
  return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::OTHER,
                                          error);
}

esp_err_t ensureDevice(NativeBus& bus, uint8_t address) {
  if (bus.device != nullptr && bus.deviceAddress == address) {
    return ESP_OK;
  }
  if (bus.device != nullptr) {
    const esp_err_t removeError = i2c_master_bus_rm_device(bus.device);
    bus.device = nullptr;
    bus.deviceAddress = 0U;
    if (removeError != ESP_OK) {
      return removeError;
    }
  }

  i2c_device_config_t config = {};
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = address;
  config.scl_speed_hz = I2C_FREQ_HZ;
  const esp_err_t error =
      i2c_master_bus_add_device(bus.bus, &config, &bus.device);
  if (error == ESP_OK) {
    bus.deviceAddress = address;
  }
  return error;
}

TCA9548A::TransportStatus i2cWrite(uint8_t address, const uint8_t* data,
                                   size_t length, uint32_t timeoutMs,
                                   void* user) {
  auto* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr || data == nullptr ||
      length == 0U) {
    return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::OTHER,
                                            ESP_ERR_INVALID_ARG);
  }
  esp_err_t error = ensureDevice(*bus, address);
  if (error == ESP_OK) {
    error = i2c_master_transmit(bus->device, data, length,
                                timeoutArg(timeoutMs));
  }
  return mapI2c(error);
}

TCA9548A::TransportStatus i2cWriteRead(
    uint8_t address, const uint8_t* txData, size_t txLength, uint8_t* rxData,
    size_t rxLength, uint32_t timeoutMs, void* user) {
  auto* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr || rxData == nullptr ||
      rxLength == 0U || (txLength > 0U && txData == nullptr)) {
    return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::OTHER,
                                            ESP_ERR_INVALID_ARG);
  }
  esp_err_t error = ensureDevice(*bus, address);
  if (error != ESP_OK) {
    return mapI2c(error);
  }
  if (txLength == 0U) {
    error = i2c_master_receive(bus->device, rxData, rxLength,
                               timeoutArg(timeoutMs));
  } else {
    error = i2c_master_transmit_receive(bus->device, txData, txLength, rxData,
                                        rxLength, timeoutArg(timeoutMs));
  }
  return mapI2c(error);
}

TCA9548A::Status pulseReset(uint32_t timeoutMs, void*) {
  if (RESET_GPIO < 0) {
    return TCA9548A::Status::Error(TCA9548A::Err::UNSUPPORTED,
                                  "RESET GPIO is not configured");
  }
  if (timeoutMs == 0U) {
    return TCA9548A::Status::Error(TCA9548A::Err::TIMEOUT,
                                  "RESET timeout is zero");
  }
  const auto gpio = static_cast<gpio_num_t>(RESET_GPIO);
  if (gpio_set_level(gpio, 0) != ESP_OK) {
    return TCA9548A::Status::Error(TCA9548A::Err::RESET_ERROR,
                                  "Failed to assert RESET");
  }
  esp_rom_delay_us(1U);
  if (gpio_set_level(gpio, 1) != ESP_OK) {
    return TCA9548A::Status::Error(TCA9548A::Err::RESET_ERROR,
                                  "Failed to release RESET");
  }
  return TCA9548A::Status::Ok();
}

bool initBus() {
  i2c_master_bus_config_t config = {};
  config.i2c_port = I2C_NUM_0;
  config.sda_io_num = I2C_SDA;
  config.scl_io_num = I2C_SCL;
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7U;
  config.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&config, &gBus.bus) == ESP_OK;
}

void configureDriver() {
  gConfig.i2cWrite = i2cWrite;
  gConfig.i2cWriteRead = i2cWriteRead;
  gConfig.i2cUser = &gBus;
  gConfig.nowMs = nowMs;
  gConfig.i2cAddress = TCA9548A::cmd::DEFAULT_ADDRESS;
  gConfig.i2cTimeoutMs = I2C_TIMEOUT_MS;
  gConfig.resetTimeoutMs = RESET_TIMEOUT_MS;
  gConfig.offlineThreshold = 5U;
  if constexpr (RESET_GPIO >= 0) {
    const auto gpio = static_cast<gpio_num_t>(RESET_GPIO);
    gpio_config_t resetConfig = {};
    resetConfig.pin_bit_mask = 1ULL << static_cast<unsigned>(gpio);
    resetConfig.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&resetConfig) == ESP_OK && gpio_set_level(gpio, 1) == ESP_OK) {
      gConfig.hardReset = pulseReset;
    }
  }
}

bool safeOffVerified() {
  TCA9548A::Status status = gDevice.disableAll();
  if (!status.ok()) {
    printStatus("safe-off write", status);
    return false;
  }
  TCA9548A::ChannelMask observed;
  status = gDevice.readChannelMask(observed);
  if (!status.ok() || !observed.isNone()) {
    printStatus("safe-off readback", status);
    if (status.ok()) {
      fputs("  observed=", stdout);
      printMask(observed);
      putchar('\n');
    }
    return false;
  }
  return true;
}

void beginDriver() {
  if (!gI2cReady) {
    puts("begin: NOT_INITIALIZED (I2C controller unavailable)");
    return;
  }
  const bool wasBound = gDevice.isBound();
  const TCA9548A::Status status = gDevice.begin(gConfig);
  fputs("begin: ", stdout);
  printStatusValue(status);
  printf(" (bound=%s)\n", gDevice.isBound() ? "yes" : "no");
  if (!wasBound && gDevice.isBound()) {
    const bool safeOff = safeOffVerified();
    printf("startup safe-off: %s%s%s\n",
           safeOff ? COLOR_GREEN : COLOR_RED,
           safeOff ? "OK (verified 0x00)" : "FAILED",
           COLOR_RESET);
  }
}

void printVersion() {
  esp_chip_info_t chip = {};
  esp_chip_info(&chip);
  printSection("Version Info");
  printf("  ESP-IDF: %s\n", esp_get_idf_version());
  printf("  MCU cores: %u, revision: %u\n", static_cast<unsigned>(chip.cores),
         static_cast<unsigned>(chip.revision));
  printf("  Library: %s\n", TCA9548A::VERSION);
  printf("  Full: %s\n", TCA9548A::VERSION_FULL);
  printf("  Code: %lu\n", static_cast<unsigned long>(TCA9548A::VERSION_CODE));
}

void printConfig() {
  TCA9548A::SettingsSnapshot snapshot;
  const TCA9548A::Status status = gDevice.getSettings(snapshot);
  printSection("Configuration");
  printf("  Bound: %s\n", snapshot.bound ? "yes" : "no");
  printf("  Initialized: %s\n", snapshot.initialized ? "yes" : "no");
  printf("  I2C address: 0x%02X\n", static_cast<unsigned>(snapshot.i2cAddress));
  printf("  I2C frequency: %lu Hz\n", static_cast<unsigned long>(I2C_FREQ_HZ));
  printf("  I2C timeout: %lu ms\n",
         static_cast<unsigned long>(snapshot.i2cTimeoutMs));
  printf("  RESET timeout: %lu ms\n",
         static_cast<unsigned long>(snapshot.resetTimeoutMs));
  printf("  nowMs hook: %s\n",
         snapshot.hasNowMsHook ? "configured" : "not configured");
  printf("  RESET callback: %s\n",
         snapshot.hasHardReset ? "configured" : "not configured");
  printf("  Offline threshold: %u (diagnostic only)\n",
         static_cast<unsigned>(snapshot.offlineThreshold));
  fputs("  Snapshot: ", stdout);
  printStatusValue(status);
  putchar('\n');
}

void printHealth() {
  const bool online = gDevice.isOnline();
  const char* stateColor = !gDevice.isInitialized()
                               ? COLOR_GRAY
                               : (online ? (gDevice.consecutiveFailures() == 0U
                                                ? COLOR_GREEN
                                                : COLOR_YELLOW)
                                         : COLOR_RED);
  printSection("Driver Health");
  printf("  State: %s%s%s (passive; never gates I2C)\n", stateColor,
         driverStateName(gDevice.state()), COLOR_RESET);
  printf("  Bound: %s\n", gDevice.isBound() ? "yes" : "no");
  printf("  Initialized: %s\n", gDevice.isInitialized() ? "yes" : "no");
  printf("  Consecutive failures: %u\n",
         static_cast<unsigned>(gDevice.consecutiveFailures()));
  printf("  Total success/failure: %lu/%lu\n",
         static_cast<unsigned long>(gDevice.totalSuccess()),
         static_cast<unsigned long>(gDevice.totalFailures()));
  printf("  Last OK/error ms: %lu/%lu\n",
         static_cast<unsigned long>(gDevice.lastOkMs()),
         static_cast<unsigned long>(gDevice.lastErrorMs()));
  printf("  Last error: %s\n", errorName(gDevice.lastError().code));
  printObservation();
}

void printHelp() {
  printSection("TCA9548A CLI Help");
  puts("  version / ver                  Version information");
  puts("  cfg                            Bound configuration");
  puts("  health / drv / state           Passive diagnostics");
  puts("  read / dump                    Read and verify mask");
  puts("  select <0-7>                   Select one channel");
  puts("  mask <0-255>                   Write an arbitrary mask");
  puts("  off                            Disable all channels");
  puts("  probe                          Raw diagnostic read");
  puts("  recover                        One safe-off write");
  puts("  reset / hardreset              RESET then verify 0x00");
  puts("  invalidate                     Mark cached mask unknown");
  puts("  begin / end                    Bind+probe / bus-silent unbind");
  puts("  scan                           Maintenance: 126 probes");
  puts("  stress <1-1000>                Maintenance select sample");
  puts("  stress_mix <1-1000>            Maintenance primitive mix");
  puts("  selftest                       Live primitive contract checks");
  puts("  hil [dry|parser|run|run reset] HIL contract entry point");
  puts("  help / ?                       This help");
}

cli_shell::LineResult pollConsoleLine(char* output, size_t capacity) {
  for (size_t processed = 0U;
       processed < cli_shell::FixedLineBuffer::CAPACITY; ++processed) {
    const int input = getchar();
    if (input == EOF) {
      clearerr(stdin);
      return cli_shell::LineResult::NONE;
    }
    const cli_shell::LineResult result =
        gLineBuffer.push(static_cast<char>(input), output, capacity);
    if (result != cli_shell::LineResult::NONE) {
      return result;
    }
  }
  return cli_shell::LineResult::NONE;
}

void printPrompt() {
  printf("%s> %s", COLOR_CYAN, COLOR_RESET);
}

bool parseUnsignedArgument(const char* command, const char* prefix,
                           uint32_t maximum, uint32_t& output) {
  const size_t prefixLength = strlen(prefix);
  if (strncmp(command, prefix, prefixLength) != 0 ||
      command[prefixLength] != ' ') {
    return false;
  }
  const char* text = command + prefixLength + 1U;
  if (*text == '\0') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long value = strtoul(text, &end, 0);
  if (errno == ERANGE || end == text || *end != '\0' ||
      value > static_cast<unsigned long>(maximum)) {
    return false;
  }
  output = static_cast<uint32_t>(value);
  return true;
}

void scanBus() {
  if (!gI2cReady || gBus.bus == nullptr) {
    puts("scan: NOT_INITIALIZED (I2C controller unavailable)");
    return;
  }
  puts("Scanning I2C bus (126 bounded probes)...");
  unsigned found = 0U;
  for (uint16_t address = 1U; address <= 126U; ++address) {
    const esp_err_t error = i2c_master_probe(
        gBus.bus, static_cast<uint16_t>(address), timeoutArg(I2C_TIMEOUT_MS));
    if (error == ESP_OK) {
      printf("  Found device at 0x%02X\n", static_cast<unsigned>(address));
      ++found;
    }
  }
  printf("Scan complete: devices=%u\n", found);
}

struct HilCounts {
  uint16_t passed = 0U;
  uint16_t failed = 0U;
  uint16_t skipped = 0U;
};

void reportCheck(HilCounts& counts, const char* name, bool passed,
                 const char* detail = "") {
  if (passed) {
    ++counts.passed;
  } else {
    ++counts.failed;
  }
  printf("  [%s%s%s] %s", passed ? COLOR_GREEN : COLOR_RED,
         passed ? "PASS" : "FAIL", COLOR_RESET, name);
  if (detail != nullptr && detail[0] != '\0') {
    printf(" - %s", detail);
  }
  putchar('\n');
}

void reportSkip(HilCounts& counts, const char* name, const char* detail) {
  ++counts.skipped;
  printf("  [%sSKIP%s] %s - %s\n", COLOR_YELLOW, COLOR_RESET, name, detail);
}

void printHilResult(const HilCounts& counts) {
  printf("HIL result: pass=%u fail=%u skip=%u\n",
         static_cast<unsigned>(counts.passed),
         static_cast<unsigned>(counts.failed),
         static_cast<unsigned>(counts.skipped));
}

void finishHilSafe(HilCounts& counts) {
  reportCheck(counts, "final verified safe-off", safeOffVerified());
  printHilResult(counts);
}

void runHil(bool dryRun, bool includeReset) {
  char title[40] = {};
  snprintf(title, sizeof(title), "TCA9548A HIL %s",
           dryRun ? "DRY-RUN" : "RUN");
  printSection(title);
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

  const uint32_t successBefore = gDevice.totalSuccess();
  const uint32_t failuresBefore = gDevice.totalFailures();
  TCA9548A::Status status = gDevice.probe();
  const bool probeOk = status.ok();
  reportCheck(counts, "probe", probeOk, errorName(status.code));
  const bool probeHealthUnchanged =
      gDevice.totalSuccess() == successBefore &&
      gDevice.totalFailures() == failuresBefore;
  reportCheck(counts, "probe no-health-side-effects", probeHealthUnchanged);
  if (!probeOk || !probeHealthUnchanged) {
    finishHilSafe(counts);
    return;
  }

  status = gDevice.disableAll();
  const bool disableOk = status.ok();
  reportCheck(counts, "disableAll write", disableOk, errorName(status.code));
  if (!disableOk) {
    finishHilSafe(counts);
    return;
  }
  TCA9548A::ChannelMask observed;
  status = gDevice.readChannelMask(observed);
  const bool disableVerified = status.ok() && observed.isNone();
  reportCheck(counts, "disableAll readback", disableVerified,
              errorName(status.code));
  if (!disableVerified) {
    finishHilSafe(counts);
    return;
  }

  status = gDevice.selectChannel(TCA9548A::Channel::CH3);
  const bool selectOk = status.ok();
  reportCheck(counts, "select CH3", selectOk, errorName(status.code));
  if (!selectOk) {
    finishHilSafe(counts);
    return;
  }
  status = gDevice.readChannelMask(observed);
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

  status = gDevice.writeChannelMask(TCA9548A::ChannelMask::fromRaw(0xA5U));
  const bool maskWriteOk = status.ok();
  reportCheck(counts, "write mask 0xA5", maskWriteOk,
              errorName(status.code));
  if (!maskWriteOk) {
    finishHilSafe(counts);
    return;
  }
  status = gDevice.readChannelMask(observed);
  const bool maskVerified = status.ok() && observed.raw() == 0xA5U;
  reportCheck(counts, "read mask 0xA5", maskVerified,
              errorName(status.code));
  if (!maskVerified) {
    finishHilSafe(counts);
    return;
  }

  status = gDevice.recover();
  const bool recoverOk = status.ok();
  reportCheck(counts, "recover safe-off write", recoverOk,
              errorName(status.code));
  if (!recoverOk) {
    finishHilSafe(counts);
    return;
  }
  status = gDevice.readChannelMask(observed);
  const bool recoverVerified = status.ok() && observed.isNone();
  reportCheck(counts, "recover readback 0x00", recoverVerified,
              errorName(status.code));
  if (!recoverVerified) {
    finishHilSafe(counts);
    return;
  }

  if (includeReset) {
    if (gConfig.hardReset == nullptr) {
      reportCheck(counts, "hardReset", false, "callback not configured");
    } else {
      status = gDevice.hardReset();
      const bool resetOk = status.ok();
      reportCheck(counts, "hardReset exact-zero verification", resetOk,
                  errorName(status.code));
      const auto resetObservation = gDevice.channelMaskObservation();
      const bool resetVerified =
          resetObservation.verified() && resetObservation.mask.isNone();
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

void runStress(uint32_t count, bool mixed) {
  TCA9548A::Status status = TCA9548A::Status::Ok();
  const uint32_t successesBefore = gDevice.totalSuccess();
  const uint32_t failuresBefore = gDevice.totalFailures();
  const uint32_t startedMs = nowMs(nullptr);
  uint32_t completed = 0U;
  for (; completed < count; ++completed) {
    if (!mixed) {
      status = gDevice.selectChannel(
          static_cast<TCA9548A::Channel>(completed % 8U));
    } else {
      switch (completed % 4U) {
        case 0U:
          status = gDevice.selectChannel(
              static_cast<TCA9548A::Channel>(completed % 8U));
          break;
        case 1U:
          status = gDevice.writeChannelMask(TCA9548A::ChannelMask::fromRaw(
              static_cast<uint8_t>(completed)));
          break;
        case 2U: {
          TCA9548A::ChannelMask observed;
          status = gDevice.readChannelMask(observed);
          break;
        }
        default: status = gDevice.disableAll(); break;
      }
    }
    if (!status.ok()) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  const bool safeOff = safeOffVerified();
  if (mixed) {
    puts("=== stress_mix summary ===");
  }
  printf("Stress results: completed=%lu requested=%lu status=%s safe_off=%s\n",
         static_cast<unsigned long>(completed),
         static_cast<unsigned long>(count), errorName(status.code),
         safeOff ? "OK" : "FAILED");
  printf("Duration: %lu ms\n",
         static_cast<unsigned long>(nowMs(nullptr) - startedMs));
  printf("Health delta: success=%lu failure=%lu\n",
         static_cast<unsigned long>(gDevice.totalSuccess() - successesBefore),
         static_cast<unsigned long>(gDevice.totalFailures() - failuresBefore));
}

void processCommand(const char* command) {
  if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
    printHelp();
  } else if (strcmp(command, "version") == 0 || strcmp(command, "ver") == 0) {
    printVersion();
  } else if (strcmp(command, "cfg") == 0) {
    printConfig();
  } else if (strcmp(command, "health") == 0 || strcmp(command, "drv") == 0 ||
             strcmp(command, "state") == 0) {
    printHealth();
  } else if (strcmp(command, "read") == 0 || strcmp(command, "dump") == 0) {
    TCA9548A::ChannelMask mask;
    const TCA9548A::Status status = gDevice.readChannelMask(mask);
    fputs("read: ", stdout);
    printStatusValue(status);
    if (status.ok()) {
      fputs(" mask=", stdout);
      printMask(mask);
    }
    putchar('\n');
  } else if (strcmp(command, "off") == 0) {
    printStatus("off", gDevice.disableAll());
  } else if (strcmp(command, "probe") == 0) {
    printStatus("probe", gDevice.probe());
    printObservation();
  } else if (strcmp(command, "recover") == 0) {
    printStatus("recover (safe-off write)", gDevice.recover());
  } else if (strcmp(command, "reset") == 0 ||
             strcmp(command, "hardreset") == 0) {
    printStatus("hardreset", gDevice.hardReset());
    printObservation();
  } else if (strcmp(command, "invalidate") == 0) {
    gDevice.invalidateChannelMask();
    puts("invalidate: OK (no bus I/O)");
    printObservation();
  } else if (strcmp(command, "begin") == 0) {
    beginDriver();
  } else if (strcmp(command, "end") == 0) {
    gDevice.end();
    puts("end: OK (no bus I/O)");
  } else if (strcmp(command, "scan") == 0) {
    scanBus();
  } else if (strcmp(command, "selftest") == 0 ||
             strcmp(command, "hil run") == 0) {
    runHil(false, false);
  } else if (strcmp(command, "hil run reset") == 0) {
    runHil(false, true);
  } else if (strcmp(command, "hil dry") == 0 ||
             strcmp(command, "hil parser") == 0 || strcmp(command, "hil") == 0) {
    runHil(true, false);
  } else {
    uint32_t value = 0U;
    if (parseUnsignedArgument(command, "select", 7U, value)) {
      printf("select %lu: ", static_cast<unsigned long>(value));
      printStatusValue(gDevice.selectChannel(
          static_cast<TCA9548A::Channel>(value)));
      putchar('\n');
    } else if (parseUnsignedArgument(command, "mask", 255U, value)) {
      printf("mask 0x%02lX: ", static_cast<unsigned long>(value));
      printStatusValue(gDevice.writeChannelMask(
          TCA9548A::ChannelMask::fromRaw(static_cast<uint8_t>(value))));
      putchar('\n');
    } else if (parseUnsignedArgument(command, "stress_mix", MAX_STRESS_COUNT,
                                     value) &&
               value > 0U) {
      runStress(value, true);
    } else if (parseUnsignedArgument(command, "stress", MAX_STRESS_COUNT,
                                     value) &&
               value > 0U) {
      runStress(value, false);
    } else {
      printf("%sUnknown or invalid command: %s%s\n", COLOR_RED, command,
             COLOR_RESET);
    }
  }
}

}  // namespace

extern "C" void app_main(void) {
  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);
  puts("\n=============================");
  puts("  TCA9548A Native ESP-IDF CLI");
  puts("=============================");
  printVersion();
  gI2cReady = initBus();
  configureDriver();
  beginDriver();
  printHelp();

  char line[LINE_LEN] = {};
  putchar('\n');
  printPrompt();
  while (true) {
    gDevice.tick(nowMs(nullptr));
    const cli_shell::LineResult lineResult =
        pollConsoleLine(line, sizeof(line));
    if (lineResult == cli_shell::LineResult::READY) {
      processCommand(line);
      putchar('\n');
      printPrompt();
    } else if (lineResult == cli_shell::LineResult::TOO_LONG) {
      printf("%s[W]%s Command discarded: line exceeds %u bytes\n\n",
             COLOR_YELLOW, COLOR_RESET,
             static_cast<unsigned>(cli_shell::FixedLineBuffer::CAPACITY - 1U));
      printPrompt();
    } else if (lineResult == cli_shell::LineResult::OUTPUT_TOO_SMALL) {
      printf("%s[W]%s Command discarded: destination buffer is too small\n\n",
             COLOR_YELLOW, COLOR_RESET);
      printPrompt();
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
