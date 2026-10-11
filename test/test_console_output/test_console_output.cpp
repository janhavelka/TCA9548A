#include <cstring>
#include <string>
#include <unity.h>

#include "../../examples/common/ConsoleOutput.h"

void setUp() {}
void tearDown() {}

namespace {
struct Sink {
  size_t available = 64U;
  size_t calls = 0U;
  size_t largest = 0U;
  std::string text;
  static size_t write(const char* data, size_t length, void* user) {
    auto& sink = *static_cast<Sink*>(user);
    ++sink.calls;
    if (length > sink.largest) sink.largest = length;
    const size_t accepted = length < sink.available ? length : sink.available;
    sink.text.append(data, accepted);
    return accepted;
  }
};

template <size_t Capacity>
void flush(tca9548a_cli::ConsoleOutput<Capacity>& output, Sink& sink) {
  for (unsigned tick = 0; tick < 200U; ++tick) output.drain(Sink::write, &sink);
  TEST_ASSERT_EQUAL_UINT32(0U, output.stats().queuedBytes);
}

void test_disconnected_output_is_retained_without_backend_retry() {
  tca9548a_cli::ConsoleOutput<128> output;
  Sink sink;
  sink.available = 0U;
  output.append("Pending prompt> ");
  output.drain(Sink::write, &sink);
  TEST_ASSERT_EQUAL_UINT32(1U, sink.calls);
  TEST_ASSERT_EQUAL_UINT32(16U, output.stats().queuedBytes);
  TEST_ASSERT_EQUAL_UINT32(0U, output.stats().acceptedBytes);
  TEST_ASSERT_EQUAL_UINT32(0U, output.stats().droppedBytes);
  sink.available = 64U;
  flush(output, sink);
  TEST_ASSERT_EQUAL_STRING("Pending prompt> ", sink.text.c_str());
}

void test_partial_writes_preserve_order_across_ring_wrap() {
  tca9548a_cli::ConsoleOutput<128> output;
  Sink sink;
  const std::string first(100U, 'A');
  const std::string second(80U, 'B');
  output.append(first.c_str());
  sink.available = 30U;
  output.drain(Sink::write, &sink);
  output.drain(Sink::write, &sink);
  output.append(second.c_str());
  sink.available = 7U;
  flush(output, sink);
  TEST_ASSERT_EQUAL_STRING((first + second).c_str(), sink.text.c_str());
  TEST_ASSERT_EQUAL_UINT32(180U, output.stats().acceptedBytes);
  TEST_ASSERT_EQUAL_UINT32(0U, output.stats().droppedBytes);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(64U, sink.largest);
}

void test_loss_suppresses_short_prompt_until_marker_can_be_queued() {
  tca9548a_cli::ConsoleOutput<128> output;
  Sink sink;
  const std::string prefix(120U, 'A');
  output.append(prefix.c_str());
  output.append("FAIL hardware transfer failed\n");
  // A short prompt fits, but must not certify the response after its error
  // was dropped. It remains suppressed until an error marker is queued.
  output.append("> ");
  TEST_ASSERT_EQUAL_UINT32(2U, output.stats().droppedMessages);
  TEST_ASSERT_EQUAL_UINT32(32U, output.stats().droppedBytes);
  output.drain(Sink::write, &sink);
  output.drain(Sink::write, &sink);
  output.append("next> ");
  flush(output, sink);
  TEST_ASSERT_EQUAL_STRING(
      (prefix + "\n[E] CONSOLE_OUTPUT_LOST; inspect console counters\nnext> ").c_str(),
      sink.text.c_str());
}

void test_oversized_record_is_dropped_whole_and_marks_loss() {
  tca9548a_cli::ConsoleOutput<64> output;
  Sink sink;
  output.append(std::string(65U, 'x').c_str());
  TEST_ASSERT_EQUAL_UINT32(65U, output.stats().droppedBytes);
  flush(output, sink);
  TEST_ASSERT_EQUAL_STRING("\n[E] CONSOLE_OUTPUT_LOST; inspect console counters\n", sink.text.c_str());
  TEST_ASSERT_EQUAL_UINT32(1U, output.stats().droppedMessages);
}

void test_invalid_backend_count_does_not_erase_pending_data() {
  tca9548a_cli::ConsoleOutput<64> output;
  Sink sink;
  output.append("retained");
  output.drain([](const char*, size_t length, void*) { return length + 1U; });
  TEST_ASSERT_EQUAL_UINT32(8U, output.stats().queuedBytes);
  TEST_ASSERT_EQUAL_UINT32(0U, output.stats().acceptedBytes);
  flush(output, sink);
  TEST_ASSERT_EQUAL_STRING("retained", sink.text.c_str());
}

void test_drain_and_counters_have_explicit_bounds() {
  tca9548a_cli::ConsoleOutput<128> output;
  Sink sink;
  output.append(std::string(100U, 'x').c_str());
  output.drain(Sink::write, &sink);
  TEST_ASSERT_EQUAL_UINT32(1U, sink.calls);
  TEST_ASSERT_EQUAL_UINT32(64U, sink.text.size());
  output.countRxOverflow();
  TEST_ASSERT_EQUAL_UINT32(1U, output.stats().rxOverflows);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, tca9548a_cli::consoleSaturatingAdd(UINT32_MAX - 1U, 2U));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, tca9548a_cli::consoleSaturatingAdd(UINT32_MAX, 1U));
  TEST_ASSERT_EQUAL_UINT32(10U, tca9548a_cli::consoleSaturatingAdd(7U, 3U));
}
}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_disconnected_output_is_retained_without_backend_retry);
  RUN_TEST(test_partial_writes_preserve_order_across_ring_wrap);
  RUN_TEST(test_loss_suppresses_short_prompt_until_marker_can_be_queued);
  RUN_TEST(test_oversized_record_is_dropped_whole_and_marks_loss);
  RUN_TEST(test_invalid_backend_count_does_not_erase_pending_data);
  RUN_TEST(test_drain_and_counters_have_explicit_bounds);
  return UNITY_END();
}
