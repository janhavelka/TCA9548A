#pragma once

// Example-only single-owner output queue. The backend must report bytes
// accepted without waiting for the host; accepted bytes are not delivery proof.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tca9548a_cli {

struct ConsoleStats {
  const char* backend = "unspecified";
  int8_t connected = -1;  // -1: the backend cannot observe connection state.
  bool rxOverflowObservable = false;
  uint32_t queuedBytes = 0;
  uint32_t acceptedBytes = 0;
  uint32_t droppedBytes = 0;
  uint32_t droppedMessages = 0;
  uint32_t rxOverflows = 0;
};

inline uint32_t consoleSaturatingAdd(uint32_t value, size_t count) {
  return count > UINT32_MAX - value ? UINT32_MAX : value + static_cast<uint32_t>(count);
}

template <size_t Capacity = 8192U>
class ConsoleOutput {
 public:
  using WriteSome = size_t (*)(const char*, size_t, void*);
  static_assert(Capacity >= 64U && Capacity <= UINT32_MAX, "Invalid console capacity");

  void append(const char* text) {
    const size_t length = std::strlen(text);
    if (length == 0U) return;
    if (_lossPending || length > Capacity - _used) {
      _stats.droppedBytes = consoleSaturatingAdd(_stats.droppedBytes, length);
      _stats.droppedMessages = consoleSaturatingAdd(_stats.droppedMessages, 1U);
      _lossPending = true;
      return;
    }
    enqueue(text, length);
  }

  // At most one backend attempt per loop. A partial or zero write preserves
  // unsent data. Records following a loss stay suppressed until its marker is
  // queued, so a later prompt cannot make an incomplete response look valid.
  void drain(WriteSome write, void* user = nullptr) {
    static constexpr char LOSS[] = "\n[E] CONSOLE_OUTPUT_LOST; inspect console counters\n";
    if (_lossPending && Capacity - _used >= sizeof(LOSS) - 1U) {
      enqueue(LOSS, sizeof(LOSS) - 1U);
      _lossPending = false;
    }
    if (write == nullptr || _used == 0U) return;
    size_t amount = _used < Capacity - _head ? _used : Capacity - _head;
    if (amount > 64U) amount = 64U;
    const size_t accepted = write(_data + _head, amount, user);
    // A broken backend must not corrupt queue bounds or erase pending bytes.
    if (accepted > amount) return;
    _head = (_head + accepted) % Capacity;
    _used -= accepted;
    _stats.acceptedBytes = consoleSaturatingAdd(_stats.acceptedBytes, accepted);
  }

  void countRxOverflow() {
    _stats.rxOverflows = consoleSaturatingAdd(_stats.rxOverflows, 1U);
  }

  ConsoleStats stats() const {
    ConsoleStats result = _stats;
    result.queuedBytes = static_cast<uint32_t>(_used);
    return result;
  }

 private:
  void enqueue(const char* text, size_t length) {
    const size_t tail = (_head + _used) % Capacity;
    const size_t first = length < Capacity - tail ? length : Capacity - tail;
    std::memcpy(_data + tail, text, first);
    std::memcpy(_data, text + first, length - first);
    _used += length;
  }

  char _data[Capacity]{};
  size_t _head = 0U;
  size_t _used = 0U;
  ConsoleStats _stats{};
  bool _lossPending = false;
};

}  // namespace tca9548a_cli
