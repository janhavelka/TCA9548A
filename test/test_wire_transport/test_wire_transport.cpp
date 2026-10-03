#include <cstdint>
#include <initializer_list>
#include <limits>

#include <unity.h>

#include "../../examples/common/I2cTransport.h"

TwoWire Wire;

void setUp() { Wire = TwoWire{}; }
void tearDown() {}

namespace {

using TCA9548A::TransportErr;

void assertUntouched(const TwoWire& wire) {
  TEST_ASSERT_EQUAL_UINT32(0, wire.beginCalls);
  TEST_ASSERT_EQUAL_UINT32(0, wire.timeoutCalls);
  TEST_ASSERT_EQUAL_UINT32(0, wire.beginTransmissionCalls);
  TEST_ASSERT_EQUAL_UINT32(0, wire.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(0, wire.endTransmissionCalls);
  TEST_ASSERT_EQUAL_UINT32(0, wire.requestCalls);
  TEST_ASSERT_EQUAL_UINT32(0, wire.availableCalls);
  TEST_ASSERT_EQUAL_UINT32(0, wire.readCalls);
}

void test_init_validates_timeout_and_bus_frequency_before_touching_wire() {
  const uint32_t invalidTimeouts[] = {0, 60001, 65536,
                                     std::numeric_limits<uint32_t>::max()};
  for (uint32_t timeout : invalidTimeouts) {
    TEST_ASSERT_FALSE(transport::initWire(8, 9, 400000, timeout));
    assertUntouched(Wire);
  }
  TEST_ASSERT_FALSE(transport::initWire(8, 9, 0, 50));
  TEST_ASSERT_FALSE(transport::initWire(8, 9, 400001, 50));
  assertUntouched(Wire);
  Wire.beginResult = false;
  TEST_ASSERT_FALSE(transport::initWire(8, 9, 400000, 50));
  TEST_ASSERT_EQUAL_UINT32(1, Wire.beginCalls);
  TEST_ASSERT_EQUAL_UINT32(0, Wire.timeoutCalls);
  Wire.beginResult = true;
  TEST_ASSERT_TRUE(transport::initWire(8, 9, 400000, 60000));
  TEST_ASSERT_EQUAL_INT(8, Wire.sdaSeen);
  TEST_ASSERT_EQUAL_INT(9, Wire.sclSeen);
  TEST_ASSERT_EQUAL_UINT32(400000, Wire.frequencySeen);
  TEST_ASSERT_EQUAL_UINT16(60000, Wire.timeoutSeen);
}

void test_write_is_one_byte_with_stop_and_preserves_backend_uncertainty() {
  const TransportErr errors[] = {TransportErr::OK, TransportErr::OTHER,
      TransportErr::OTHER, TransportErr::NACK_DATA, TransportErr::OTHER,
      TransportErr::TIMEOUT, TransportErr::OTHER};
  for (uint8_t result = 0; result < sizeof(errors) / sizeof(errors[0]); ++result) {
    TwoWire wire;
    wire.endResult = result;
    const uint8_t mask = 0x81;
    const auto status = transport::wireWrite(0x77, &mask, 1, 60000, &wire);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(errors[result]),
                          static_cast<int>(status.code));
    TEST_ASSERT_EQUAL_INT32(result, status.detail);
    TEST_ASSERT_EQUAL_UINT32(0, wire.beginCalls);
    TEST_ASSERT_EQUAL_UINT32(1, wire.timeoutCalls);
    TEST_ASSERT_EQUAL_UINT16(60000, wire.timeoutSeen);
    TEST_ASSERT_EQUAL_UINT32(1, wire.beginTransmissionCalls);
    TEST_ASSERT_EQUAL_HEX8(0x77, wire.addressSeen);
    TEST_ASSERT_EQUAL_UINT32(1, wire.writeCalls);
    TEST_ASSERT_EQUAL_UINT32(1, wire.writeLengthSeen);
    TEST_ASSERT_EQUAL_HEX8(0x81, wire.writtenByte);
    TEST_ASSERT_EQUAL_UINT32(1, wire.endTransmissionCalls);
    TEST_ASSERT_TRUE(wire.writeStop);
    TEST_ASSERT_EQUAL_UINT32(0, wire.requestCalls);
  }
  assertUntouched(Wire);
}

void test_short_write_completes_transaction_and_reports_failure() {
  TwoWire wire;
  wire.writeResult = 0;
  const uint8_t mask = 0xFF;
  const auto status = transport::wireWrite(0x70, &mask, 1, 1, &wire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TransportErr::OTHER),
                        static_cast<int>(status.code));
  TEST_ASSERT_EQUAL_INT32(0, status.detail);
  TEST_ASSERT_EQUAL_UINT16(1, wire.timeoutSeen);
  TEST_ASSERT_EQUAL_UINT32(1, wire.endTransmissionCalls);
  TEST_ASSERT_TRUE(wire.writeStop);
}

void test_invalid_write_arguments_never_touch_wire() {
  const uint8_t data[] = {0x01, 0x02};
  TEST_ASSERT_FALSE(transport::wireWrite(0x70, data, 1, 50, nullptr).ok());
  TEST_ASSERT_FALSE(transport::wireWrite(0x70, nullptr, 1, 50, &Wire).ok());
  TEST_ASSERT_FALSE(transport::wireWrite(0x70, data, 0, 50, &Wire).ok());
  TEST_ASSERT_FALSE(transport::wireWrite(0x70, data, 2, 50, &Wire).ok());
  for (uint32_t timeout : {0U, 60001U, 65536U}) {
    TEST_ASSERT_FALSE(transport::wireWrite(0x70, data, 1, timeout, &Wire).ok());
  }
  TEST_ASSERT_FALSE(transport::wireWrite(0x6F, data, 1, 50, &Wire).ok());
  TEST_ASSERT_FALSE(transport::wireWrite(0x78, data, 1, 50, &Wire).ok());
  assertUntouched(Wire);
}

void test_read_is_one_read_only_byte_with_explicit_stop() {
  TwoWire wire;
  uint8_t output = 0;
  TEST_ASSERT_TRUE(transport::wireWriteRead(0x70, nullptr, 0, &output, 1,
                                           1, &wire).ok());
  TEST_ASSERT_EQUAL_HEX8(0xA5, output);
  TEST_ASSERT_EQUAL_UINT32(1, wire.timeoutCalls);
  TEST_ASSERT_EQUAL_UINT16(1, wire.timeoutSeen);
  TEST_ASSERT_EQUAL_UINT32(0, wire.beginTransmissionCalls);
  TEST_ASSERT_EQUAL_UINT32(0, wire.endTransmissionCalls);
  TEST_ASSERT_EQUAL_UINT32(1, wire.requestCalls);
  TEST_ASSERT_EQUAL_HEX8(0x70, wire.addressSeen);
  TEST_ASSERT_EQUAL_UINT32(1, wire.requestLengthSeen);
  TEST_ASSERT_TRUE(wire.readStop);
  TEST_ASSERT_EQUAL_UINT32(1, wire.readCalls);
  assertUntouched(Wire);
}

void test_failed_reads_preserve_output_and_do_not_guess_error_causes() {
  struct Case { size_t received; int available; int value; int32_t detail; };
  const Case cases[] = {{0, 0, -1, 0}, {2, 2, 0xA5, 2},
      {1, 0, -1, -5}, {1, 2, 0xA5, -5}, {1, 1, -1, -6}, {1, 1, 256, -6}};
  for (const Case& item : cases) {
    TwoWire wire;
    wire.requestResult = item.received;
    wire.availableResult = item.available;
    wire.readResult = item.value;
    uint8_t output = 0x42;
    const auto status = transport::wireWriteRead(0x72, nullptr, 0, &output,
                                                1, 50, &wire);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TransportErr::OTHER),
                          static_cast<int>(status.code));
    TEST_ASSERT_EQUAL_INT32(item.detail, status.detail);
    TEST_ASSERT_EQUAL_HEX8(0x42, output);
    TEST_ASSERT_EQUAL_UINT32(1, wire.requestCalls);
    TEST_ASSERT_TRUE(wire.readStop);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(1, wire.readCalls);
  }
}

void test_read_rejects_combined_transfers_and_invalid_arguments_without_io() {
  const uint8_t tx = 0xFF;
  uint8_t output = 0x42;
  TEST_ASSERT_FALSE(transport::wireWriteRead(0x70, nullptr, 0, &output, 1,
                                            50, nullptr).ok());
  TEST_ASSERT_FALSE(transport::wireWriteRead(0x70, &tx, 1, &output, 1,
                                            50, &Wire).ok());
  TEST_ASSERT_FALSE(transport::wireWriteRead(0x70, &tx, 0, &output, 1,
                                            50, &Wire).ok());
  TEST_ASSERT_FALSE(transport::wireWriteRead(0x70, nullptr, 1, &output, 1,
                                            50, &Wire).ok());
  TEST_ASSERT_FALSE(transport::wireWriteRead(0x70, nullptr, 0, nullptr, 1,
                                            50, &Wire).ok());
  for (size_t length : {0U, 2U}) {
    TEST_ASSERT_FALSE(transport::wireWriteRead(0x70, nullptr, 0, &output,
                                              length, 50, &Wire).ok());
  }
  for (uint32_t timeout : {0U, 60001U, 65536U}) {
    TEST_ASSERT_FALSE(transport::wireWriteRead(0x70, nullptr, 0, &output,
                                              1, timeout, &Wire).ok());
  }
  for (uint8_t address : {0x6FU, 0x78U}) {
    TEST_ASSERT_FALSE(transport::wireWriteRead(address, nullptr, 0, &output,
                                              1, 50, &Wire).ok());
  }
  TEST_ASSERT_EQUAL_HEX8(0x42, output);
  assertUntouched(Wire);
}

} // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_init_validates_timeout_and_bus_frequency_before_touching_wire);
  RUN_TEST(test_write_is_one_byte_with_stop_and_preserves_backend_uncertainty);
  RUN_TEST(test_short_write_completes_transaction_and_reports_failure);
  RUN_TEST(test_invalid_write_arguments_never_touch_wire);
  RUN_TEST(test_read_is_one_read_only_byte_with_explicit_stop);
  RUN_TEST(test_failed_reads_preserve_output_and_do_not_guess_error_causes);
  RUN_TEST(test_read_rejects_combined_transfers_and_invalid_arguments_without_io);
  return UNITY_END();
}
