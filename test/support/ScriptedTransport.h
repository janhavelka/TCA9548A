#pragma once

#include <cstddef>
#include <cstdint>

#include "TCA9548A/Config.h"
#include "TCA9548A/Status.h"

namespace test_support {

enum class CallKind : uint8_t { WRITE, READ };

struct TransportCall {
  CallKind kind = CallKind::READ;
  uint8_t address = 0;
  bool txWasNull = true;
  bool rxWasNull = true;
  size_t txLen = 0;
  size_t rxLen = 0;
  uint32_t timeoutMs = 0;
  void* user = nullptr;
  uint8_t txBytes[4]{};
};

struct ScriptedResponse {
  TCA9548A::TransportStatus status = TCA9548A::TransportStatus::Ok();
  bool applyWrite = false;
  bool provideReadByte = false;
  uint8_t readByte = 0;
};

/// Fixed-capacity transport model. A failed write may still be configured to
/// affect the device, which models an ambiguous timeout or controller error.
class ScriptedTransport {
public:
  static constexpr size_t MAX_CALLS = 320;

  void reset(uint8_t initialMask = 0) {
    hardwareMask = initialMask;
    _callCount = 0;
    _scriptCount = 0;
    _scriptIndex = 0;
    _defaultResponse = {};
    _overflow = false;
  }

  void clearHistory() {
    _callCount = 0;
    _overflow = false;
  }

  void clearScript() {
    _scriptCount = 0;
    _scriptIndex = 0;
  }

  void setDefaultResponse(const ScriptedResponse& response) {
    _defaultResponse = response;
  }

  bool push(const ScriptedResponse& response) {
    if (_scriptCount >= MAX_CALLS) {
      _overflow = true;
      return false;
    }
    _script[_scriptCount++] = response;
    return true;
  }

  bool pushError(TCA9548A::TransportErr error, int32_t detail,
                 bool applyWrite = false, bool provideReadByte = false,
                 uint8_t readByte = 0) {
    return push({TCA9548A::TransportStatus::Error(error, detail), applyWrite,
                 provideReadByte, readByte});
  }

  size_t callCount() const { return _callCount; }
  const TransportCall& call(size_t index) const { return _calls[index]; }
  bool overflowed() const { return _overflow; }

  uint8_t hardwareMask = 0;

  static TCA9548A::TransportStatus write(uint8_t address,
                                         const uint8_t* data, size_t len,
                                         uint32_t timeoutMs, void* user) {
    auto* self = static_cast<ScriptedTransport*>(user);
    if (self == nullptr) {
      return TCA9548A::TransportStatus::Error(
          TCA9548A::TransportErr::OTHER, -1000);
    }
    return self->onWrite(address, data, len, timeoutMs, user);
  }

  static TCA9548A::TransportStatus read(uint8_t address,
                                        const uint8_t* txData, size_t txLen,
                                        uint8_t* rxData, size_t rxLen,
                                        uint32_t timeoutMs, void* user) {
    auto* self = static_cast<ScriptedTransport*>(user);
    if (self == nullptr) {
      return TCA9548A::TransportStatus::Error(
          TCA9548A::TransportErr::OTHER, -1001);
    }
    return self->onRead(address, txData, txLen, rxData, rxLen, timeoutMs,
                        user);
  }

private:
  ScriptedResponse nextResponse() {
    if (_scriptIndex < _scriptCount) {
      return _script[_scriptIndex++];
    }
    return _defaultResponse;
  }

  TransportCall* record(CallKind kind, uint8_t address,
                        const uint8_t* txData, size_t txLen, uint8_t* rxData,
                        size_t rxLen, uint32_t timeoutMs, void* user) {
    if (_callCount >= MAX_CALLS) {
      _overflow = true;
      return nullptr;
    }

    TransportCall& call = _calls[_callCount++];
    call = {};
    call.kind = kind;
    call.address = address;
    call.txWasNull = txData == nullptr;
    call.rxWasNull = rxData == nullptr;
    call.txLen = txLen;
    call.rxLen = rxLen;
    call.timeoutMs = timeoutMs;
    call.user = user;
    const size_t copyLen = txLen < sizeof(call.txBytes) ? txLen
                                                       : sizeof(call.txBytes);
    for (size_t i = 0; i < copyLen && txData != nullptr; ++i) {
      call.txBytes[i] = txData[i];
    }
    return &call;
  }

  TCA9548A::TransportStatus onWrite(uint8_t address, const uint8_t* data,
                                    size_t len, uint32_t timeoutMs,
                                    void* user) {
    if (record(CallKind::WRITE, address, data, len, nullptr, 0, timeoutMs,
               user) == nullptr) {
      return TCA9548A::TransportStatus::Error(
          TCA9548A::TransportErr::OTHER, -1002);
    }

    const ScriptedResponse response = nextResponse();
    if ((response.status.ok() || response.applyWrite) && data != nullptr &&
        len > 0) {
      hardwareMask = data[len - 1];
    }
    return response.status;
  }

  TCA9548A::TransportStatus onRead(uint8_t address, const uint8_t* txData,
                                   size_t txLen, uint8_t* rxData, size_t rxLen,
                                   uint32_t timeoutMs, void* user) {
    if (record(CallKind::READ, address, txData, txLen, rxData, rxLen,
               timeoutMs, user) == nullptr) {
      return TCA9548A::TransportStatus::Error(
          TCA9548A::TransportErr::OTHER, -1003);
    }

    const ScriptedResponse response = nextResponse();
    if ((response.status.ok() || response.provideReadByte) &&
        rxData != nullptr && rxLen > 0) {
      rxData[0] = response.provideReadByte ? response.readByte : hardwareMask;
    }
    return response.status;
  }

  TransportCall _calls[MAX_CALLS]{};
  ScriptedResponse _script[MAX_CALLS]{};
  ScriptedResponse _defaultResponse{};
  size_t _callCount = 0;
  size_t _scriptCount = 0;
  size_t _scriptIndex = 0;
  bool _overflow = false;
};

struct ResetHarness {
  ScriptedTransport* transport = nullptr;
  TCA9548A::Status result = TCA9548A::Status::Ok();
  uint8_t appliedMask = 0;
  bool applyEffect = true;
  uint32_t timeoutSeen = 0;
  int calls = 0;

  static TCA9548A::Status reset(uint32_t timeoutMs, void* user) {
    auto* self = static_cast<ResetHarness*>(user);
    if (self == nullptr) {
      return TCA9548A::Status::Error(TCA9548A::Err::I2C_ERROR,
                                     "Null reset context", -2000);
    }
    ++self->calls;
    self->timeoutSeen = timeoutMs;
    if (self->applyEffect && self->transport != nullptr) {
      self->transport->hardwareMask = self->appliedMask;
    }
    return self->result;
  }
};

} // namespace test_support
