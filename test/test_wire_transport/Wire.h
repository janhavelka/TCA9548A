#pragma once

#include <cstddef>
#include <cstdint>

// Recording test double for the exact Wire surface used by I2cTransport.h.
// This include directory is visible only in the native_transport environment.
class TwoWire {
public:
  bool beginResult = true;
  uint8_t endResult = 0;
  size_t writeResult = 1;
  size_t requestResult = 1;
  int availableResult = 1;
  int readResult = 0xA5;

  unsigned beginCalls = 0;
  unsigned timeoutCalls = 0;
  unsigned beginTransmissionCalls = 0;
  unsigned writeCalls = 0;
  unsigned endTransmissionCalls = 0;
  unsigned requestCalls = 0;
  unsigned availableCalls = 0;
  unsigned readCalls = 0;
  int sdaSeen = -1;
  int sclSeen = -1;
  uint32_t frequencySeen = 0;
  uint16_t timeoutSeen = 0;
  uint8_t addressSeen = 0;
  size_t writeLengthSeen = 0;
  size_t requestLengthSeen = 0;
  uint8_t writtenByte = 0;
  bool writeStop = false;
  bool readStop = false;

  bool begin(int sda, int scl, uint32_t frequency) {
    ++beginCalls;
    sdaSeen = sda;
    sclSeen = scl;
    frequencySeen = frequency;
    return beginResult;
  }

  void setTimeOut(uint16_t timeout) {
    ++timeoutCalls;
    timeoutSeen = timeout;
  }

  void beginTransmission(uint8_t address) {
    ++beginTransmissionCalls;
    addressSeen = address;
  }

  size_t write(const uint8_t* data, size_t length) {
    ++writeCalls;
    writeLengthSeen = length;
    if (data != nullptr && length > 0) {
      writtenByte = data[0];
    }
    return writeResult;
  }

  uint8_t endTransmission(bool sendStop) {
    ++endTransmissionCalls;
    writeStop = sendStop;
    return endResult;
  }

  size_t requestFrom(uint8_t address, size_t length, bool sendStop) {
    ++requestCalls;
    addressSeen = address;
    requestLengthSeen = length;
    readStop = sendStop;
    return requestResult;
  }

  int available() {
    ++availableCalls;
    return availableResult;
  }

  int read() {
    ++readCalls;
    return readResult;
  }
};

extern TwoWire Wire;
