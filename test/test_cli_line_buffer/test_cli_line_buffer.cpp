#include <cstdint>
#include <cstring>

#include <unity.h>

#include "../../examples/common/CliLineBuffer.h"

void setUp() {}

void tearDown() {}

namespace {

void test_fixed_cli_line_buffer_is_trimmed_bounded_and_recoverable() {
  cli_shell::FixedLineBuffer input;
  char command[cli_shell::FixedLineBuffer::CAPACITY]{};

  const char trimmed[] = "  mask 0xA5\t\r";
  cli_shell::LineResult result = cli_shell::LineResult::NONE;
  for (size_t index = 0U; index < sizeof(trimmed) - 1U; ++index) {
    result = input.push(trimmed[index], command, sizeof(command));
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(cli_shell::LineResult::READY),
                        static_cast<int>(result));
  TEST_ASSERT_EQUAL_STRING("mask 0xA5", command);

  // A CRLF pair produces one command; its second terminator is ignored.
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(cli_shell::LineResult::NONE),
      static_cast<int>(input.push('\n', command, sizeof(command))));

  // Exactly 127 bytes fit; a 128-byte line is discarded in full.
  for (size_t index = 0U;
       index < cli_shell::FixedLineBuffer::CAPACITY - 1U; ++index) {
    result = input.push('x', command, sizeof(command));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(cli_shell::LineResult::NONE),
                          static_cast<int>(result));
  }
  result = input.push('\n', command, sizeof(command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(cli_shell::LineResult::READY),
                        static_cast<int>(result));
  TEST_ASSERT_EQUAL_UINT32(
      cli_shell::FixedLineBuffer::CAPACITY - 1U,
      static_cast<uint32_t>(std::strlen(command)));

  for (size_t index = 0U; index < cli_shell::FixedLineBuffer::CAPACITY;
       ++index) {
    result = input.push('y', command, sizeof(command));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(cli_shell::LineResult::NONE),
                          static_cast<int>(result));
  }
  result = input.push('\r', command, sizeof(command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(cli_shell::LineResult::TOO_LONG),
                        static_cast<int>(result));

  const char next[] = "health\n";
  for (size_t index = 0U; index < sizeof(next) - 1U; ++index) {
    result = input.push(next[index], command, sizeof(command));
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(cli_shell::LineResult::READY),
                        static_cast<int>(result));
  TEST_ASSERT_EQUAL_STRING("health", command);

  char tooSmall[4]{};
  const char help[] = "help\n";
  for (size_t index = 0U; index < sizeof(help) - 1U; ++index) {
    result = input.push(help[index], tooSmall, sizeof(tooSmall));
  }
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(cli_shell::LineResult::OUTPUT_TOO_SMALL),
      static_cast<int>(result));

  const char invalidDestination[] = "x\n";
  for (size_t index = 0U; index < sizeof(invalidDestination) - 1U; ++index) {
    result = input.push(invalidDestination[index], nullptr, 0U);
  }
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(cli_shell::LineResult::OUTPUT_TOO_SMALL),
      static_cast<int>(result));

  const char recovered[] = "read\n";
  for (size_t index = 0U; index < sizeof(recovered) - 1U; ++index) {
    result = input.push(recovered[index], command, sizeof(command));
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(cli_shell::LineResult::READY),
                        static_cast<int>(result));
  TEST_ASSERT_EQUAL_STRING("read", command);
}

} // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fixed_cli_line_buffer_is_trimmed_bounded_and_recoverable);
  return UNITY_END();
}
