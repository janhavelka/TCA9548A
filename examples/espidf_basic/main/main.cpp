/// @file main.cpp
/// @brief Native ESP-IDF transport and console adapter for the common CLI.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_chip_info.h>
#include <esp_err.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if CONFIG_ESP_CONSOLE_USB_CDC
// The ROM CDC VFS discards partial-write results. Use the same SDK backend
// directly; this private interface is compile-checked on IDF 5.4 and 5.5.
#include <esp_private/usb_console.h>
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#endif

#include "examples/common/Tca9548aCli.h"
#include "TCA9548A/Version.h"

namespace {

constexpr gpio_num_t I2C_SDA = GPIO_NUM_8;
constexpr gpio_num_t I2C_SCL = GPIO_NUM_9;
constexpr uint8_t I2C_ADDRESS = TCA9548A::cmd::DEFAULT_ADDRESS;
constexpr int RESET_GPIO = -1;
constexpr uint32_t I2C_FREQ_HZ = 400000U;
constexpr uint32_t I2C_TIMEOUT_MS = TCA9548A::Config{}.i2cTimeoutMs;
constexpr uint32_t RESET_TIMEOUT_MS = 10U;
constexpr uint32_t CONSOLE_WRITE_TIMEOUT_MS = 50U;
constexpr unsigned CONSOLE_INPUT_BUDGET = 32U;

struct NativeBus {
  i2c_master_bus_handle_t bus = nullptr;
  i2c_master_dev_handle_t device = nullptr;
  // Retain ownership even if SDK removal fails during a retarget rollback.
  i2c_master_dev_handle_t retainedDevice = nullptr;
  bool registrationFaulted = false;
  bool receiveFaulted = false;
  uint8_t address = I2C_ADDRESS;
  tca9548a_cli::TransferStats transfers{};
};

NativeBus gBus;
TCA9548A::TCA9548A gDevice;
TCA9548A::Config gConfig;
tca9548a_cli::Cli gConsole(gDevice, gConfig);
uint32_t gDroppedOutputPieces = 0;

uint32_t nowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

void pauseTask() {
  // pdMS_TO_TICKS(1) is zero at the SDK's default 100 Hz tick rate.
  vTaskDelay(pdMS_TO_TICKS(1) > 0 ? pdMS_TO_TICKS(1) : 1);
}

void writeText(const char* text, void*) {
  const size_t length = strlen(text);
  const uint32_t startedMs = nowMs(nullptr);
  size_t sent = 0;
  while (sent < length && nowMs(nullptr) - startedMs < CONSOLE_WRITE_TIMEOUT_MS) {
#if CONFIG_ESP_CONSOLE_USB_CDC
    const ssize_t written = esp_usb_console_write_buf(text + sent, length - sent);
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    const int written = usb_serial_jtag_write_bytes(text + sent, length - sent, 0);
#else
    // UART fallback; the maintained target defaults both use USB.
    const ssize_t written = write(STDOUT_FILENO, text + sent, 1U);
#endif
    if (written > 0) {
      sent += static_cast<size_t>(written);
    } else if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      break;
    } else {
      pauseTask();
    }
  }
#if CONFIG_ESP_CONSOLE_USB_CDC
  // Submit partial lines (including the prompt). The loop retries pending
  // buffered data if the host is temporarily unable to accept it.
  (void)esp_usb_console_flush();
#endif
  if (sent != length && gDroppedOutputPieces != UINT32_MAX) ++gDroppedOutputPieces;
}

void sdkError(const char* operation, esp_err_t error) {
  char text[192];
  snprintf(text, sizeof(text), "%s: %s (detail=%ld)\n", operation,
           esp_err_to_name(error), static_cast<long>(error));
  writeText(text, nullptr);
}

void printVersion(void*) {
  esp_chip_info_t chip = {};
  esp_chip_info(&chip);
  char text[384];
  snprintf(text, sizeof(text),
           "  Framework: esp-idf\n  ESP-IDF: %s\n  MCU cores: %u, revision: %u\n"
           "  Firmware commit: %s\n  Firmware tree: %s\n"
           "  Console dropped pieces: %lu\n",
           esp_get_idf_version(), static_cast<unsigned>(chip.cores),
           static_cast<unsigned>(chip.revision), TCA9548A::GIT_COMMIT,
           TCA9548A::GIT_STATUS, static_cast<unsigned long>(gDroppedOutputPieces));
  writeText(text, nullptr);
}

TCA9548A::TransportStatus mapI2c(esp_err_t error) {
  if (error == ESP_OK) return TCA9548A::TransportStatus::Ok();
  if (error == ESP_ERR_TIMEOUT) {
    return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::TIMEOUT, error);
  }
  // The transfer API cannot reliably distinguish the NACK phase or bus cause.
  return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::OTHER, error);
}

bool controllerReady(const NativeBus& bus) {
  return bus.bus != nullptr && !bus.registrationFaulted;
}

TCA9548A::TransportStatus i2cWrite(uint8_t address, const uint8_t* data,
                                 size_t length, uint32_t timeoutMs, void* user) {
  auto* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->device == nullptr || address != bus->address ||
      data == nullptr || length != 1U || timeoutMs == 0U || timeoutMs > 60000U) {
    return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::OTHER,
                                           ESP_ERR_INVALID_ARG);
  }
  if (!controllerReady(*bus) || bus->receiveFaulted) {
    return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::BUS,
                                           ESP_ERR_INVALID_STATE);
  }
  const esp_err_t error = i2c_master_transmit(bus->device, data, length,
                                             static_cast<int>(timeoutMs));
  bus->transfers.record(tca9548a_cli::TransferStats::Kind::WRITE, error == ESP_OK);
  return mapI2c(error);
}

TCA9548A::TransportStatus i2cWriteRead(
    uint8_t address, const uint8_t* txData, size_t txLength, uint8_t* rxData,
    size_t rxLength, uint32_t timeoutMs, void* user) {
  auto* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->device == nullptr || address != bus->address ||
      rxData == nullptr || rxLength != 1U || txData != nullptr || txLength != 0U ||
      timeoutMs == 0U || timeoutMs > 60000U) {
    return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::OTHER,
                                           ESP_ERR_INVALID_ARG);
  }
  if (!controllerReady(*bus) || bus->receiveFaulted) {
    return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::BUS,
                                           ESP_ERR_INVALID_STATE);
  }
  // The control byte has no pointer phase; each write ends in STOP before read.
  const esp_err_t error = i2c_master_receive(bus->device, rxData, rxLength,
                                            static_cast<int>(timeoutMs));
  bus->transfers.record(tca9548a_cli::TransferStats::Kind::READ, error == ESP_OK);
  // IDF 5.5.5 can retain ISR-visible receive state after failure. A subsequent
  // probe can access stale state. Retire this controller until explicit reboot.
  if (error != ESP_OK) bus->receiveFaulted = true;
  return mapI2c(error);
}

TCA9548A::Status prepareAddress(uint8_t address, void* user) {
  auto& owner = *static_cast<NativeBus*>(user);
  if (!controllerReady(owner) || owner.receiveFaulted) {
    return TCA9548A::Status::Error(TCA9548A::Err::I2C_BUS,
                                  "I2C controller unavailable; reboot required",
                                  ESP_ERR_INVALID_STATE);
  }
  if (!TCA9548A::cmd::isValidAddress(address)) {
    return TCA9548A::Status::Error(TCA9548A::Err::INVALID_PARAM, "Invalid mux address");
  }
  if (owner.device != nullptr && owner.address == address) return TCA9548A::Status::Ok();

  // Register outside transfer callbacks and keep the old registration until
  // its replacement exists. A failed setup must preserve handle ownership.
  i2c_device_config_t deviceConfig = {};
  deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  deviceConfig.device_address = address;
  deviceConfig.scl_speed_hz = I2C_FREQ_HZ;
  i2c_master_dev_handle_t replacement = nullptr;
  const esp_err_t added = i2c_master_bus_add_device(owner.bus, &deviceConfig, &replacement);
  if (added != ESP_OK) {
    return TCA9548A::Status::Error(TCA9548A::Err::I2C_ERROR,
                                  "Device registration failed; previous handle retained", added);
  }
  if (owner.device != nullptr) {
    const esp_err_t removed = i2c_master_bus_rm_device(owner.device);
    if (removed != ESP_OK) {
      const esp_err_t cleanup = i2c_master_bus_rm_device(replacement);
      if (cleanup != ESP_OK) {
        owner.retainedDevice = replacement;
        owner.registrationFaulted = true;
        sdkError("Replacement-handle cleanup failed; reboot required", cleanup);
      }
      return TCA9548A::Status::Error(TCA9548A::Err::I2C_ERROR,
                                    "Previous registration removal failed", removed);
    }
  }
  owner.device = replacement;
  owner.address = address;
  return TCA9548A::Status::Ok();
}

TCA9548A::Status probeAddress(uint8_t address, uint32_t timeoutMs, void* user) {
  auto& owner = *static_cast<NativeBus*>(user);
  if (!controllerReady(owner) || owner.receiveFaulted) {
    return TCA9548A::Status::Error(TCA9548A::Err::I2C_BUS,
                                  "I2C controller unavailable; reboot required",
                                  ESP_ERR_INVALID_STATE);
  }
  if (address < 0x08U || address > 0x77U || timeoutMs == 0U || timeoutMs > 60000U) {
    return TCA9548A::Status::Error(TCA9548A::Err::INVALID_PARAM, "Invalid address probe");
  }
  const esp_err_t error = i2c_master_probe(owner.bus, address, static_cast<int>(timeoutMs));
  owner.transfers.record(tca9548a_cli::TransferStats::Kind::PROBE, error == ESP_OK);
  if (error == ESP_OK) return TCA9548A::Status::Ok();
  const auto code = error == ESP_ERR_NOT_FOUND ? TCA9548A::Err::I2C_NACK_ADDR :
                    error == ESP_ERR_TIMEOUT ? TCA9548A::Err::I2C_TIMEOUT : TCA9548A::Err::I2C_ERROR;
  return TCA9548A::Status::Error(code, "Address-only probe failed", error);
}

tca9548a_cli::BusInfo busInfo(void* user) {
  const auto& owner = *static_cast<NativeBus*>(user);
  return {controllerReady(owner), owner.receiveFaulted, I2C_SDA, I2C_SCL,
          RESET_GPIO, I2C_FREQ_HZ, owner.address};
}

tca9548a_cli::TransferStats transferStats(void* user) {
  return static_cast<NativeBus*>(user)->transfers;
}

void resetTransferStats(void* user) {
  static_cast<NativeBus*>(user)->transfers = {};
}

[[maybe_unused]] TCA9548A::Status pulseReset(uint32_t timeoutMs, void*) {
  if (RESET_GPIO < 0) {
    return TCA9548A::Status::Error(TCA9548A::Err::RESET_ERROR, "RESET GPIO is not configured");
  }
  if (timeoutMs == 0U) {
    return TCA9548A::Status::Error(TCA9548A::Err::TIMEOUT, "RESET timeout is zero");
  }
  const auto gpio = static_cast<gpio_num_t>(RESET_GPIO);
  const esp_err_t asserted = gpio_set_level(gpio, 0);
  if (asserted != ESP_OK) {
    return TCA9548A::Status::Error(TCA9548A::Err::RESET_ERROR, "Failed to assert RESET", asserted);
  }
  esp_rom_delay_us(1U);
  const esp_err_t released = gpio_set_level(gpio, 1);
  if (released != ESP_OK) {
    return TCA9548A::Status::Error(TCA9548A::Err::RESET_ERROR, "Failed to release RESET", released);
  }
  return TCA9548A::Status::Ok();
}

void initBus() {
  i2c_master_bus_config_t config = {};
  config.i2c_port = I2C_NUM_0;
  config.sda_io_num = I2C_SDA;
  config.scl_io_num = I2C_SCL;
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7U;
  // External pull-ups must be sized for every active segment.
  config.flags.enable_internal_pullup = false;
  const esp_err_t error = i2c_new_master_bus(&config, &gBus.bus);
  if (error != ESP_OK) sdkError("I2C bus initialization failed", error);
}

void configureDriver() {
  gConfig.i2cWrite = i2cWrite;
  gConfig.i2cWriteRead = i2cWriteRead;
  gConfig.i2cUser = &gBus;
  gConfig.nowMs = nowMs;
  gConfig.i2cAddress = I2C_ADDRESS;
  gConfig.i2cTimeoutMs = I2C_TIMEOUT_MS;
  gConfig.resetTimeoutMs = RESET_TIMEOUT_MS;
  gConfig.offlineThreshold = 5U;
  if constexpr (RESET_GPIO >= 0) {
    const auto gpio = static_cast<gpio_num_t>(RESET_GPIO);
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
      sdkError("RESET GPIO setup failed", ESP_ERR_INVALID_ARG);
      return;
    }
    // Preset the output latch before enabling the driver: RESET stays high.
    const esp_err_t level = gpio_set_level(gpio, 1);
    if (level != ESP_OK) {
      sdkError("RESET GPIO high-level setup failed", level);
      return;
    }
    gpio_config_t resetConfig = {};
    resetConfig.pin_bit_mask = 1ULL << static_cast<unsigned>(gpio);
    resetConfig.mode = GPIO_MODE_OUTPUT;
    const esp_err_t mode = gpio_config(&resetConfig);
    if (mode != ESP_OK) {
      sdkError("RESET GPIO output setup failed", mode);
      return;
    }
    gConfig.hardReset = pulseReset;
  }
}

}  // namespace

extern "C" void app_main(void) {
  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  usb_serial_jtag_driver_config_t usbConfig = {};
  usbConfig.tx_buffer_size = 1024;
  usbConfig.rx_buffer_size = 256;
  const esp_err_t usbError = usb_serial_jtag_driver_install(&usbConfig);
  if (usbError != ESP_OK) {
    printf("USB console initialization failed: %s\n", esp_err_to_name(usbError));
    return;
  }
  usb_serial_jtag_vfs_use_driver();
#endif
  const int inputFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (inputFlags < 0 || fcntl(STDIN_FILENO, F_SETFL, inputFlags | O_NONBLOCK) < 0) {
    writeText("Console nonblocking input setup failed\n", nullptr);
    return;
  }
  initBus();
  configureDriver();
  tca9548a_cli::Platform platform;
  platform.writeText = writeText;
  platform.nowMs = nowMs;
  platform.printVersion = printVersion;
  platform.busInfo = busInfo;
  platform.transferStats = transferStats;
  platform.resetTransferStats = resetTransferStats;
  platform.prepareAddress = prepareAddress;
  platform.probeAddress = probeAddress;
  platform.user = &gBus;
  gConsole.setup(platform);
  writeText("\nTCA9548A native ESP-IDF bring-up CLI\n", nullptr);
  gConsole.processCommand("version");
  gConsole.beginDriver();
  gConsole.printHelp();
  gConsole.printPrompt();

  while (true) {
    for (unsigned count = 0; count < CONSOLE_INPUT_BUDGET; ++count) {
      char value;
      if (read(STDIN_FILENO, &value, 1U) != 1) break;
      gConsole.feed(value);
    }
    gConsole.tick();
#if CONFIG_ESP_CONSOLE_USB_CDC
    (void)esp_usb_console_flush();
#endif
    pauseTask();
  }
}
