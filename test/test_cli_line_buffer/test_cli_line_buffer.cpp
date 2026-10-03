#include <cstdint>
#include <cstring>
#include <initializer_list>

#include <unity.h>

#include "../../examples/common/CliLineBuffer.h"
#include "../../examples/common/CliArguments.h"

void setUp() {}

void tearDown() {}

namespace {

void test_numeric_arguments_reject_sign_wrap_overflow_and_trailing_input() {
  const char* rejected[] = {
      "mask -0", "mask -4294967295", "mask -18446744073709551615",
      "mask +1", "mask 256", "mask 0x100", "mask 08", "mask 1x",
      "mask 1 2", "mask ", "mask", "masking 1",
      "mask 184467440737095516160", "mask 0x", "mask \t-1"};
  for (const char* command : rejected) {
    unsigned long result = 99UL;
    TEST_ASSERT_FALSE(cli_shell::parseUnsignedArgument(command, "mask", 255UL,
                                                       result));
    TEST_ASSERT_EQUAL_UINT32(99U, result);
  }
  const char* accepted[] = {"mask 255", "mask 0xFF", "mask 0377", "mask \t255"};
  for (const char* command : accepted) {
    unsigned long result = 0UL;
    TEST_ASSERT_TRUE(cli_shell::parseUnsignedArgument(command, "mask", 255UL,
                                                      result));
    TEST_ASSERT_EQUAL_UINT32(255U, result);
  }
  unsigned long result = 99UL;
  TEST_ASSERT_TRUE(cli_shell::parseUnsignedArgument("mask 0", "mask", 255UL,
                                                    result));
  TEST_ASSERT_EQUAL_UINT32(0U, result);
}

void test_control_bytes_discard_whole_line_without_dispatching_prefix() {
  for (char invalid : {'\0', '\x01', '\x1b'}) {
    cli_shell::FixedLineBuffer input;
    char command[128] = "unchanged";
    for (char value : {'o', 'f', 'f', invalid, 'x'}) {
      TEST_ASSERT_EQUAL_INT(static_cast<int>(cli_shell::LineResult::NONE),
          static_cast<int>(input.push(value, command, sizeof(command))));
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(cli_shell::LineResult::INVALID_INPUT),
        static_cast<int>(input.push('\n', command, sizeof(command))));
    TEST_ASSERT_EQUAL_STRING("unchanged", command);
    for (char value : {'r', 'e', 'a', 'd'}) {
      input.push(value, command, sizeof(command));
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(cli_shell::LineResult::READY),
        static_cast<int>(input.push('\n', command, sizeof(command))));
    TEST_ASSERT_EQUAL_STRING("read", command);
  }
}

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
  RUN_TEST(test_numeric_arguments_reject_sign_wrap_overflow_and_trailing_input);
  RUN_TEST(test_control_bytes_discard_whole_line_without_dispatching_prefix);
  return UNITY_END();
}
