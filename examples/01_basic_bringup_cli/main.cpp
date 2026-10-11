/// @file main.cpp
/// @brief Arduino transport and console adapter for the common bring-up CLI.

#include <Arduino.h>
#include <Wire.h>
#include <cstdio>
#include <atomic>

#if CONFIG_IDF_TARGET_ESP32S3
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_select.h>
#include <esp_err.h>
#include <hal/usb_serial_jtag_ll.h>
#if ARDUINO_USB_CDC_ON_BOOT
#error "The S3 example owns the USB driver; disable ARDUINO_USB_CDC_ON_BOOT"
#endif
#elif CONFIG_IDF_TARGET_ESP32S2
#if !ARDUINO_USB_CDC_ON_BOOT || ARDUINO_USB_MODE
#error "The S2 example requires TinyUSB CDC on boot"
#endif
#include <tusb.h>
#else
#error "The Arduino example supports ESP32-S2 and ESP32-S3 only"
#endif

#include "examples/common/BoardConfig.h"
#include "examples/common/I2cTransport.h"
#include "examples/common/Tca9548aCli.h"
#include "TCA9548A/Version.h"

namespace {

constexpr unsigned CONSOLE_INPUT_BUDGET = 32U;

struct ArduinoBus {
  TwoWire* wire = &Wire;
  bool ready = false;
  bool receiveFaulted = false;
  uint8_t address = board::TCA_ADDRESS;
  tca9548a_cli::TransferStats transfers{};
};

ArduinoBus bus;
TCA9548A::TCA9548A device;
TCA9548A::Config config;
tca9548a_cli::Cli console(device, config);
tca9548a_cli::ConsoleOutput<> output;
bool consoleReady = false;
bool wasConnected = false;
unsigned queuedRxDiscard = 0U;
#if !CONFIG_IDF_TARGET_ESP32S3
std::atomic<bool> rxOverflow{false};
void consoleEvent(void*, esp_event_base_t, int32_t event, void*) {
  if (event == ARDUINO_USB_CDC_RX_OVERFLOW_EVENT) rxOverflow.store(true);
}
#endif

uint32_t nowMs(void*) { return millis(); }

bool consoleConnected() {
#if CONFIG_IDF_TARGET_ESP32S3
  return consoleReady && usb_serial_jtag_is_connected();
#else
  // USBCDC::operator bool additionally requires RTS. The actual TinyUSB
  // transmit path requires DTR only, matching a monitor with RTS deasserted.
  return tud_cdc_connected();
#endif
}

void writeText(const char* text, void*) { output.append(text); }

size_t writeConsole(const char* data, size_t length, void*) {
  if (!consoleReady) return 0U;
#if CONFIG_IDF_TARGET_ESP32S3
  const int sent = usb_serial_jtag_write_bytes(data, length, 0);
  return sent > 0 ? static_cast<size_t>(sent) : 0U;
#else
  return Serial.write(reinterpret_cast<const uint8_t*>(data), length);
#endif
}

int readConsole() {
#if CONFIG_IDF_TARGET_ESP32S3
  uint8_t value = 0;
  return consoleReady && usb_serial_jtag_read_bytes(&value, 1U, 0) == 1 ? value : -1;
#else
  return Serial.read();
#endif
}

unsigned pendingInput() {
#if CONFIG_IDF_TARGET_ESP32S3
  return consoleReady ? static_cast<unsigned>(usb_serial_jtag_get_read_bytes_available()) : 0U;
#else
  const int available = Serial.available();
  return available > 0 ? static_cast<unsigned>(available) : 0U;
#endif
}

tca9548a_cli::ConsoleStats consoleStats(void*) {
  auto result = output.stats();
#if CONFIG_IDF_TARGET_ESP32S3
  result.backend = "USB-Serial-JTAG";
#else
  result.backend = "TinyUSB-CDC";
  result.rxOverflowObservable = true;
#endif
  result.connected = consoleConnected() ? 1 : 0;
  return result;
}

void printVersion(void*) {
  char text[384];
  std::snprintf(text, sizeof(text),
                "  Framework: arduino\n  Arduino-ESP32: %s\n  ESP-IDF: %s\n"
                "  MCU: %s rev %u\n  Firmware commit: %s\n  Firmware tree: %s\n"
                "  Console dropped pieces: %lu\n",
                ESP.getCoreVersion(), ESP.getSdkVersion(), ESP.getChipModel(),
                static_cast<unsigned>(ESP.getChipRevision()),
                TCA9548A::GIT_COMMIT, TCA9548A::GIT_STATUS,
                static_cast<unsigned long>(output.stats().droppedMessages));
  writeText(text, nullptr);
}

TCA9548A::Status pulseReset(uint32_t timeoutMs, void*) {
  if (board::TCA_RESET < 0) {
    return TCA9548A::Status::Error(TCA9548A::Err::RESET_ERROR, "RESET pin is not configured");
  }
  if (timeoutMs == 0U) {
    return TCA9548A::Status::Error(TCA9548A::Err::TIMEOUT, "RESET timeout is zero");
  }
  digitalWrite(board::TCA_RESET, LOW);
  delayMicroseconds(1U);
  digitalWrite(board::TCA_RESET, HIGH);
  return TCA9548A::Status::Ok();
}

TCA9548A::TransportStatus ownerWrite(uint8_t address, const uint8_t* data,
                                    size_t length, uint32_t timeoutMs, void* user) {
  auto& owner = *static_cast<ArduinoBus*>(user);
  if (!owner.ready || owner.receiveFaulted) {
    return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::BUS, -1);
  }
  const auto status = transport::wireWrite(address, data, length, timeoutMs, owner.wire);
  owner.transfers.record(tca9548a_cli::TransferStats::Kind::WRITE, status.ok());
  return status;
}

TCA9548A::TransportStatus ownerRead(uint8_t address, const uint8_t* txData,
                                   size_t txLength, uint8_t* rxData,
                                   size_t rxLength, uint32_t timeoutMs, void* user) {
  auto& owner = *static_cast<ArduinoBus*>(user);
  if (!owner.ready || owner.receiveFaulted) {
    return TCA9548A::TransportStatus::Error(TCA9548A::TransportErr::BUS, -1);
  }
  const auto status = transport::wireWriteRead(address, txData, txLength,
                                               rxData, rxLength, timeoutMs, owner.wire);
  owner.transfers.record(tca9548a_cli::TransferStats::Kind::READ, status.ok());
  // A failed receive can leave ISR-visible state in the pinned SDK. Keep the
  // controller retired until reboot; CLI lifecycle changes never clear this.
  if (!status.ok()) owner.receiveFaulted = true;
  return status;
}

TCA9548A::Status prepareAddress(uint8_t address, void* user) {
  auto& owner = *static_cast<ArduinoBus*>(user);
  if (!owner.ready || owner.receiveFaulted) {
    return TCA9548A::Status::Error(TCA9548A::Err::I2C_BUS,
                                  "I2C controller unavailable; reboot after receive failure");
  }
  if (!TCA9548A::cmd::isValidAddress(address)) {
    return TCA9548A::Status::Error(TCA9548A::Err::INVALID_PARAM, "Invalid mux address");
  }
  owner.address = address;
  return TCA9548A::Status::Ok();
}

TCA9548A::Status probeAddress(uint8_t address, uint32_t timeoutMs, void* user) {
  auto& owner = *static_cast<ArduinoBus*>(user);
  if (!owner.ready || owner.receiveFaulted) {
    return TCA9548A::Status::Error(TCA9548A::Err::I2C_BUS,
                                  "I2C controller unavailable; reboot after receive failure");
  }
  if (address < 0x08U || address > 0x77U || timeoutMs == 0U ||
      timeoutMs > transport::MAX_WIRE_TIMEOUT_MS) {
    return TCA9548A::Status::Error(TCA9548A::Err::INVALID_PARAM, "Invalid address probe");
  }
  owner.wire->setTimeOut(static_cast<uint16_t>(timeoutMs));
  owner.wire->beginTransmission(address);
  const uint8_t error = owner.wire->endTransmission(true);
  owner.transfers.record(tca9548a_cli::TransferStats::Kind::PROBE, error == 0U);
  if (error == 0U) return TCA9548A::Status::Ok();
  // There is no payload, so a NACK here can only be an address NACK.
  const auto code = error == 2U ? TCA9548A::Err::I2C_NACK_ADDR :
                    error == 5U ? TCA9548A::Err::I2C_TIMEOUT : TCA9548A::Err::I2C_ERROR;
  return TCA9548A::Status::Error(code, "Address-only probe failed", error);
}

tca9548a_cli::BusInfo busInfo(void* user) {
  const auto& owner = *static_cast<ArduinoBus*>(user);
  return {owner.ready, owner.receiveFaulted, board::I2C_SDA, board::I2C_SCL,
          board::TCA_RESET, board::I2C_FREQ_HZ, owner.address};
}

tca9548a_cli::TransferStats transferStats(void* user) {
  return static_cast<ArduinoBus*>(user)->transfers;
}

void resetTransferStats(void* user) {
  static_cast<ArduinoBus*>(user)->transfers = {};
}

void configureDriver() {
  config.i2cWrite = ownerWrite;
  config.i2cWriteRead = ownerRead;
  config.i2cUser = &bus;
  config.nowMs = nowMs;
  config.i2cAddress = board::TCA_ADDRESS;
  config.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
  config.resetTimeoutMs = board::TCA_RESET_TIMEOUT_MS;
  config.offlineThreshold = 5U;
  if (board::TCA_RESET >= 0) {
    // Register the pin before writing its latch; enter OUTPUT already high.
    pinMode(board::TCA_RESET, INPUT_PULLUP);
    digitalWrite(board::TCA_RESET, HIGH);
    pinMode(board::TCA_RESET, OUTPUT);
    config.hardReset = pulseReset;
  }
}

}  // namespace

void setup() {
#if CONFIG_IDF_TARGET_ESP32S3
  usb_serial_jtag_driver_config_t usbConfig = {};
  usbConfig.tx_buffer_size = 1024U;
  usbConfig.rx_buffer_size = 1024U;
  // CPU-only resets may leave interrupts enabled by the preceding USB owner.
  // Keep FIFO/status bits pending so the new driver can service them.
  usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
  const esp_err_t usbError = usb_serial_jtag_driver_install(&usbConfig);
  consoleReady = usbError == ESP_OK;
  if (!consoleReady) {
    // The native USB ROM path remains available for this startup failure.
    printf("USB console initialization failed: %s\n", esp_err_to_name(usbError));
    return;
  }
#else
  Serial.enableReboot(false);
  Serial.onEvent(ARDUINO_USB_CDC_RX_OVERFLOW_EVENT, consoleEvent);
  consoleReady = Serial.setRxBufferSize(1024U) == 1024U;
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0U);
  if (!consoleReady) {
    Serial.print("USB console RX allocation failed\n");
    return;
  }
#endif
  // USB may enumerate later. No wait for a monitor or reconnect is required.
  bus.ready = board::initI2c();
  configureDriver();
  tca9548a_cli::Platform platform;
  platform.writeText = writeText;
  platform.nowMs = nowMs;
  platform.printVersion = printVersion;
  platform.busInfo = busInfo;
  platform.transferStats = transferStats;
  platform.resetTransferStats = resetTransferStats;
  platform.consoleStats = consoleStats;
  platform.prepareAddress = prepareAddress;
  platform.probeAddress = probeAddress;
  platform.user = &bus;
  console.setup(platform);
  writeText("\nTCA9548A Arduino bring-up CLI\n", nullptr);
  console.processCommand("version");
  console.beginDriver();
  console.printHelp();
  console.printPrompt();
}

void loop() {
  const bool connected = consoleConnected();
  if (wasConnected && !connected) {
    console.discardInput();
    queuedRxDiscard = pendingInput();
  }
  wasConnected = connected;
  for (unsigned count = 0; consoleReady && count < CONSOLE_INPUT_BUDGET; ++count) {
#if !CONFIG_IDF_TARGET_ESP32S3
    if (rxOverflow.exchange(false)) {
      output.countRxOverflow();
      console.discardInput();
      // Overflow happened at the queue tail: discard every older queued byte
      // before accepting a future delimiter as the resynchronization point.
      queuedRxDiscard = pendingInput();
    }
#endif
    const int value = readConsole();
    if (value < 0) break;
    if (queuedRxDiscard != 0U) { --queuedRxDiscard; continue; }
    console.feed(static_cast<char>(value));
    if (value == '\r' || value == '\n') break;
  }
  console.tick();
  output.drain(writeConsole);
  delay(1);
}
