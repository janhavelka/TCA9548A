#include <cstdint>
#include <limits>
#include <string>

#include <unity.h>

#include "support/CliFixture.h"

namespace {

using test_support::CliFixture;
using Kind = CliFixture::Kind;

void bind(CliFixture& fixture) {
  fixture.command("color off");
  fixture.command("begin");
  TEST_ASSERT_TRUE(fixture.device.isBound());
  TEST_ASSERT_TRUE(fixture.device.isInitialized());
  TEST_ASSERT_EQUAL_UINT8(0, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.shapeValid);
  fixture.clearHistory();
}

void tick(CliFixture& fixture, uint32_t elapsedMs = 1) {
  const auto before = fixture.transfers.size();
  fixture.clockMs += elapsedMs;
  fixture.cli.tick();
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(1,
      static_cast<uint32_t>(fixture.transfers.size() - before));
  TEST_ASSERT_TRUE(fixture.shapeValid);
}

void finish(CliFixture& fixture, uint32_t elapsedMs = 1) {
  unsigned remaining = 10000;
  while (fixture.cli.active() && remaining > 0) {
    tick(fixture, elapsedMs);
    --remaining;
  }
  TEST_ASSERT_FALSE_MESSAGE(fixture.cli.active(), "session exceeded its bound");
}

void test_setup_and_diagnostic_aliases_are_bus_silent() {
  CliFixture fixture;
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  fixture.command("color off");
  const char* commands[] = {"help", "?", "version", "ver", "cfg", "settings",
      "snapshot", "health", "drv", "state", "online", "diag", "bus", "addr",
      "timeout", "resettimeout", "offline", "stats", "result", "xfer_stats",
      "counters", "color", "verbose", "stop", "cancel"};
  for (const char* command : commands) fixture.command(command);
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  TEST_ASSERT_FALSE(fixture.device.isBound());
  TEST_ASSERT_TRUE(fixture.contains("TCA9548A CLI Help"));
  TEST_ASSERT_TRUE(fixture.contains("Host fixture: actual CLI and driver"));
  TEST_ASSERT_TRUE(fixture.contains("I2C address:"));
  TEST_ASSERT_TRUE(fixture.contains("State:"));
  TEST_ASSERT_FALSE(fixture.contains("Unknown"));
}

void test_begin_precheck_retains_working_owner_for_nondefault_strap() {
  CliFixture fixture;
  fixture.strapAddress = 0x73;
  fixture.command("color off");
  fixture.command("begin");
  TEST_ASSERT_FALSE(fixture.device.isBound());
  TEST_ASSERT_FALSE(fixture.receiveFaulted);
  TEST_ASSERT_EQUAL_UINT32(1, fixture.count(Kind::PROBE));
  TEST_ASSERT_EQUAL_UINT32(0, fixture.count(Kind::READ));
  TEST_ASSERT_TRUE(fixture.contains("I2C_NACK_ADDR"));

  fixture.command("addr 0x73");
  TEST_ASSERT_EQUAL_HEX8(0x73, fixture.config.i2cAddress);
  fixture.clearHistory();
  fixture.command("begin");
  TEST_ASSERT_TRUE(fixture.device.isInitialized());
  TEST_ASSERT_EQUAL_HEX8(0x73, fixture.ownerAddress);
  TEST_ASSERT_EQUAL_UINT32(1, fixture.count(Kind::PROBE));
  TEST_ASSERT_EQUAL_UINT32(2, fixture.count(Kind::READ));
  TEST_ASSERT_EQUAL_UINT32(1, fixture.count(Kind::WRITE));
  TEST_ASSERT_TRUE(fixture.device.channelMaskObservation().verified());
  for (const auto& transfer : fixture.transfers) {
    TEST_ASSERT_EQUAL_HEX8(0x73, transfer.address);
  }
}

void test_runtime_settings_are_bounded_and_apply_only_after_explicit_begin() {
  CliFixture fixture;
  fixture.command("color off");
  fixture.command("addr 0x77");
  fixture.command("timeout 37");
  fixture.command("resettimeout 19");
  fixture.command("offline 2");
  TEST_ASSERT_EQUAL_HEX8(0x77, fixture.config.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(37, fixture.config.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(19, fixture.config.resetTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(2, fixture.config.offlineThreshold);
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  TEST_ASSERT_EQUAL_UINT32(0, fixture.prepareCalls);
  fixture.strapAddress = 0x77;
  fixture.command("begin");
  TEST_ASSERT_TRUE(fixture.device.isInitialized());
  for (const auto& transfer : fixture.transfers) {
    TEST_ASSERT_EQUAL_UINT32(37, transfer.timeoutMs);
  }
  fixture.clearHistory();
  for (const char* command : {"addr 0x70", "timeout 1", "resettimeout 1", "offline 1"}) {
    fixture.command(command);
  }
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  TEST_ASSERT_EQUAL_HEX8(0x77, fixture.config.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(37, fixture.config.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(19, fixture.config.resetTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(2, fixture.config.offlineThreshold);
  TEST_ASSERT_TRUE(fixture.contains("BUSY"));
  fixture.command("reset");
  TEST_ASSERT_EQUAL_UINT32(19, fixture.resetTimeoutSeen);
}

void test_invalid_settings_never_change_config_or_retarget_owner() {
  CliFixture fixture;
  const char* commands[] = {"addr 0x6f", "addr 0x78", "addr -1", "addr 112x",
      "timeout 0", "timeout 60001", "timeout -1", "timeout 4294967296",
      "resettimeout 0", "resettimeout 60001", "offline 0", "offline 256",
      "offline 1 2", "timeout 20 trailing"};
  for (const char* command : commands) fixture.command(command);
  TEST_ASSERT_EQUAL_HEX8(0x70, fixture.config.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(20, fixture.config.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(10, fixture.config.resetTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5, fixture.config.offlineThreshold);
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  TEST_ASSERT_EQUAL_UINT32(0, fixture.prepareCalls);
}

void test_lifecycle_rebind_invalidation_and_probe_preserve_contracts() {
  CliFixture fixture;
  bind(fixture);
  fixture.command("mask 0xa5");
  fixture.command("begin");
  TEST_ASSERT_TRUE(fixture.contains("BUSY"));
  TEST_ASSERT_EQUAL_UINT32(1, fixture.transfers.size());
  fixture.clearHistory();
  fixture.command("invalidate");
  TEST_ASSERT_FALSE(fixture.device.channelMaskObservation().known());
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  const auto successes = fixture.device.totalSuccess();
  fixture.command("probe");
  TEST_ASSERT_TRUE(fixture.device.channelMaskObservation().verified());
  TEST_ASSERT_EQUAL_UINT32(successes, fixture.device.totalSuccess());
  fixture.clearHistory();
  fixture.command("end");
  TEST_ASSERT_FALSE(fixture.device.isBound());
  TEST_ASSERT_EQUAL_HEX8(0xa5, fixture.hardwareMask);
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  fixture.command("read");
  fixture.command("mask 1");
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  fixture.command("begin");
  TEST_ASSERT_TRUE(fixture.device.isInitialized());
  TEST_ASSERT_EQUAL_HEX8(0, fixture.hardwareMask);
  TEST_ASSERT_GREATER_THAN_UINT32(successes, fixture.device.totalSuccess());
}

void test_all_channel_commands_update_actual_register_and_read_modify_write() {
  CliFixture fixture;
  bind(fixture);
  for (unsigned channel = 0; channel < 8; ++channel) {
    fixture.command(("select " + std::to_string(channel)).c_str());
    TEST_ASSERT_EQUAL_HEX8(1U << channel, fixture.hardwareMask);
  }
  fixture.command("mask 0xa5");
  fixture.command("enable 1");
  TEST_ASSERT_EQUAL_HEX8(0xa7, fixture.hardwareMask);
  fixture.command("disable 5");
  TEST_ASSERT_EQUAL_HEX8(0x87, fixture.hardwareMask);
  fixture.command("read");
  fixture.command("dump");
  fixture.command("verify 0x87");
  fixture.command("verify");
  TEST_ASSERT_EQUAL_HEX8(0x87, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.contains("mask=0x87"));
  fixture.command("off");
  TEST_ASSERT_EQUAL_HEX8(0, fixture.hardwareMask);
  fixture.command("mask 255");
  fixture.command("recover");
  TEST_ASSERT_EQUAL_HEX8(0, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.shapeValid);
}

void test_read_modify_write_never_writes_after_failed_read() {
  CliFixture fixture;
  bind(fixture);
  fixture.hardwareMask = 0xa5;
  fixture.failAt = 1;
  fixture.command("enable 1");
  TEST_ASSERT_EQUAL_UINT32(1, fixture.count(Kind::READ));
  TEST_ASSERT_EQUAL_UINT32(0, fixture.count(Kind::WRITE));
  TEST_ASSERT_EQUAL_HEX8(0xa5, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.contains("I2C_TIMEOUT"));
}

void test_reset_aliases_verify_hardware_and_report_absent_or_unwired_reset() {
  CliFixture fixture;
  bind(fixture);
  fixture.hardwareMask = 0xff;
  fixture.command("reset");
  fixture.hardwareMask = 0xa5;
  fixture.command("hardreset");
  TEST_ASSERT_EQUAL_UINT32(2, fixture.resetCalls);
  TEST_ASSERT_EQUAL_UINT32(2, fixture.count(Kind::READ));
  TEST_ASSERT_EQUAL_UINT32(0, fixture.count(Kind::WRITE));
  TEST_ASSERT_EQUAL_HEX8(0, fixture.hardwareMask);
  fixture.resetChangesMask = false;
  fixture.hardwareMask = 1;
  fixture.command("reset");
  TEST_ASSERT_TRUE(fixture.contains("RESET_STATE_MISMATCH"));
  TEST_ASSERT_EQUAL_HEX8(1, fixture.hardwareMask);

  CliFixture missing(false);
  bind(missing);
  missing.command("reset");
  TEST_ASSERT_EQUAL_UINT32(0, missing.transfers.size());
  TEST_ASSERT_TRUE(missing.contains("UNSUPPORTED"));
}

void test_hil_parser_and_dry_modes_execute_without_bus_access() {
  CliFixture fixture;
  fixture.command("color off");
  for (const char* command : {"hil", "hil dry", "hil parser"}) {
    fixture.output.clear();
    fixture.command(command);
    TEST_ASSERT_TRUE(fixture.contains("HIL result:"));
    TEST_ASSERT_TRUE(fixture.contains("fail=0"));
  }
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  TEST_ASSERT_EQUAL_UINT32(0, fixture.resetCalls);
  TEST_ASSERT_FALSE(fixture.device.isBound());
}

void test_live_hil_checks_every_channel_and_restores_verified_entry_mask() {
  CliFixture fixture;
  bind(fixture);
  fixture.hardwareMask = 0x5a;
  fixture.command("hil run reset");
  TEST_ASSERT_TRUE(fixture.contains("fail=0"));
  TEST_ASSERT_TRUE(fixture.contains("skip=0"));
  TEST_ASSERT_EQUAL_HEX8(0x5a, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.device.channelMaskObservation().verified());
  TEST_ASSERT_EQUAL_UINT32(1, fixture.resetCalls);
  bool seen[8] = {};
  for (const auto& transfer : fixture.transfers) {
    if (transfer.kind == Kind::WRITE) {
      for (unsigned channel = 0; channel < 8; ++channel) {
        if (transfer.value == (1U << channel)) seen[channel] = true;
      }
    }
  }
  for (const bool channel : seen) TEST_ASSERT_TRUE(channel);
  for (const char* command : {"selftest", "hil run"}) {
    fixture.clearHistory();
    fixture.command(command);
    TEST_ASSERT_EQUAL_HEX8(0x5a, fixture.hardwareMask);
    TEST_ASSERT_TRUE(fixture.contains("fail=0"));
    TEST_ASSERT_TRUE(fixture.contains("skip=1"));
  }
}

void test_hil_mismatch_and_restore_failure_cannot_report_success() {
  CliFixture reference;
  bind(reference);
  reference.hardwareMask = 0x5a;
  reference.command("hil run");
  TEST_ASSERT_GREATER_THAN_UINT32(2, reference.transfers.size());
  const size_t restoreWrite = reference.transfers.size() - 1;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Kind::WRITE),
      static_cast<int>(reference.transfers[restoreWrite - 1].kind));

  CliFixture failure;
  bind(failure);
  failure.hardwareMask = 0x5a;
  failure.failAt = restoreWrite;
  failure.command("hil run");
  TEST_ASSERT_FALSE(failure.contains("fail=0"));
  TEST_ASSERT_TRUE(failure.contains("[FAIL] final verified mask restore"));

  CliFixture mismatch;
  bind(mismatch);
  mismatch.hardwareMask = 0x5a;
  mismatch.corruptReadAt = 4;
  mismatch.corruptReadValue = 0xff;
  mismatch.command("hil run");
  TEST_ASSERT_FALSE(mismatch.contains("fail=0"));
  TEST_ASSERT_EQUAL_HEX8(0x5a, mismatch.hardwareMask);
  TEST_ASSERT_TRUE(mismatch.device.channelMaskObservation().verified());
}

void test_malformed_and_discarded_lines_never_dispatch_mutating_prefix() {
  CliFixture fixture;
  bind(fixture);
  fixture.hardwareMask = 0xa5;
  for (const char* command : {"select 8", "select -1", "mask 256", "mask +1",
      "mask 0x", "mask 1 2", "enable 8", "disable -1", "verify 256",
      "stress 0", "stress 1001", "stress_mix -1", "watch 0 10",
      "watch 1 0", "soak 0", "soak 86401", "sweep extra", "off extra"}) {
    fixture.line(command);
  }
  std::string embedded("off");
  embedded.push_back('\0');
  embedded += " suffix";
  fixture.line(embedded);
  fixture.line("off\x01");
  fixture.line("mask 0 " + std::string(150, '0'));
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  TEST_ASSERT_FALSE(fixture.cli.active());
  TEST_ASSERT_EQUAL_HEX8(0xa5, fixture.hardwareMask);
  fixture.line("  read\t\r");
  TEST_ASSERT_EQUAL_UINT32(1, fixture.count(Kind::READ));
  TEST_ASSERT_TRUE(fixture.contains("mask=0xA5"));
}

void test_sweep_is_cooperative_exhaustive_and_restores_original_mask() {
  CliFixture fixture;
  bind(fixture);
  fixture.hardwareMask = 0x5a;
  fixture.command("sweep");
  TEST_ASSERT_TRUE(fixture.cli.active());
  finish(fixture);
  TEST_ASSERT_EQUAL_HEX8(0x5a, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.device.channelMaskObservation().verified());
  size_t index = 0;
  for (const auto& transfer : fixture.transfers) {
    if (transfer.kind == Kind::WRITE) {
      if (index < 256) TEST_ASSERT_EQUAL_HEX8(index, transfer.value);
      else TEST_ASSERT_EQUAL_HEX8(0x5a, transfer.value);
      ++index;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(257, index);
  TEST_ASSERT_TRUE(fixture.contains("completed=256"));
}

void test_cancelled_sweep_restores_mask_and_rejects_competing_mutation() {
  CliFixture fixture;
  bind(fixture);
  fixture.hardwareMask = 0x5a;
  fixture.command("sweep");
  for (unsigned index = 0; index < 5; ++index) tick(fixture);
  const size_t before = fixture.transfers.size();
  fixture.command("mask 255");
  fixture.command("end");
  TEST_ASSERT_EQUAL_UINT32(before, fixture.transfers.size());
  TEST_ASSERT_TRUE(fixture.device.isBound());
  TEST_ASSERT_TRUE(fixture.contains("BUSY"));
  fixture.command("cancel");
  finish(fixture);
  TEST_ASSERT_EQUAL_HEX8(0x5a, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.device.channelMaskObservation().verified());
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(before + 2, fixture.transfers.size());
}

void test_sweep_readback_failure_stops_and_restores_entry() {
  CliFixture fixture;
  bind(fixture);
  fixture.hardwareMask = 0x5a;
  fixture.command("sweep");
  fixture.corruptReadAt = 3;
  fixture.corruptReadValue = 0xff;
  finish(fixture);
  TEST_ASSERT_EQUAL_UINT32(2, fixture.count(Kind::WRITE));
  TEST_ASSERT_EQUAL_HEX8(0x5a, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.device.channelMaskObservation().verified());
  TEST_ASSERT_FALSE(fixture.contains("completed=256"));
}

void test_stress_samples_verify_channels_and_finish_all_off() {
  CliFixture fixture;
  bind(fixture);
  fixture.command("stress 8");
  finish(fixture);
  TEST_ASSERT_EQUAL_HEX8(0, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.contains("completed=8 requested=8 status=OK safe_off=OK"));
  unsigned selected = 0;
  for (const auto& transfer : fixture.transfers) {
    if (transfer.kind == Kind::WRITE && transfer.value != 0) {
      TEST_ASSERT_EQUAL_HEX8(1U << selected, transfer.value);
      ++selected;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(8, selected);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(9, fixture.count(Kind::READ));
}

void test_mixed_stress_covers_all_channels_and_detects_readback_mismatch() {
  CliFixture fixture;
  bind(fixture);
  fixture.command("stress_mix 32");
  finish(fixture);
  bool seen[8] = {};
  for (const auto& transfer : fixture.transfers) {
    if (transfer.kind == Kind::WRITE) {
      for (unsigned channel = 0; channel < 8; ++channel) {
        if (transfer.value == (1U << channel)) seen[channel] = true;
      }
    }
  }
  for (const bool channel : seen) TEST_ASSERT_TRUE(channel);
  TEST_ASSERT_TRUE(fixture.contains("completed=32 requested=32 status=OK safe_off=OK"));
  fixture.clearHistory();
  fixture.command("stress 8");
  fixture.corruptReadAt = 2;
  fixture.corruptReadValue = 0xff;
  finish(fixture);
  TEST_ASSERT_FALSE(fixture.contains("completed=8 requested=8 status=OK"));
  TEST_ASSERT_EQUAL_HEX8(0, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.device.channelMaskObservation().verified());
}

void test_scan_and_discover_have_bounded_probes_and_no_register_writes() {
  CliFixture fixture;
  bind(fixture);
  fixture.hardwareMask = 0x20;
  fixture.command("scan");
  finish(fixture);
  TEST_ASSERT_EQUAL_UINT32(112, fixture.count(Kind::PROBE));
  TEST_ASSERT_EQUAL_UINT32(1, fixture.count(Kind::READ));
  TEST_ASSERT_EQUAL_UINT32(0, fixture.count(Kind::WRITE));
  TEST_ASSERT_TRUE(fixture.contains("active_mask=0x20"));
  TEST_ASSERT_TRUE(fixture.contains("devices=1 errors=0"));
  fixture.command("end");
  fixture.strapAddress = 0x76;
  for (const char* command : {"discover", "detect"}) {
    fixture.clearHistory();
    fixture.command(command);
    finish(fixture);
    TEST_ASSERT_EQUAL_UINT32(8, fixture.count(Kind::PROBE));
    TEST_ASSERT_EQUAL_UINT32(0, fixture.count(Kind::READ));
    TEST_ASSERT_EQUAL_UINT32(0, fixture.count(Kind::WRITE));
    TEST_ASSERT_TRUE(fixture.contains("0x76"));
    TEST_ASSERT_FALSE(fixture.device.isBound());
    TEST_ASSERT_EQUAL_HEX8(0x70, fixture.config.i2cAddress);
  }
}

void test_watch_honors_count_interval_wrap_and_bus_silent_stop() {
  CliFixture fixture;
  bind(fixture);
  fixture.clockMs = std::numeric_limits<uint32_t>::max() - 10U;
  fixture.command("watch 3 20");
  tick(fixture, 0);
  const size_t first = fixture.transfers.size();
  tick(fixture, 19);
  TEST_ASSERT_EQUAL_UINT32(first, fixture.transfers.size());
  tick(fixture, 1);
  finish(fixture, 20);
  TEST_ASSERT_EQUAL_UINT32(3, fixture.count(Kind::READ));
  TEST_ASSERT_EQUAL_UINT32(0, fixture.count(Kind::WRITE));
  fixture.clearHistory();
  fixture.command("watch 3 20");
  tick(fixture);
  const size_t beforeStop = fixture.transfers.size();
  fixture.command("stop");
  finish(fixture);
  TEST_ASSERT_EQUAL_UINT32(beforeStop, fixture.transfers.size());
}

void test_soak_has_finite_deadline_and_preserves_selected_route() {
  CliFixture fixture;
  bind(fixture);
  fixture.hardwareMask = 0x40;
  fixture.clockMs = std::numeric_limits<uint32_t>::max() - 400U;
  fixture.command("soak 1");
  finish(fixture, 10);
  TEST_ASSERT_EQUAL_HEX8(0x40, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.device.channelMaskObservation().verified());
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(1100, static_cast<uint32_t>(fixture.clockMs -
      (std::numeric_limits<uint32_t>::max() - 400U)));
  TEST_ASSERT_GREATER_THAN_UINT32(0, fixture.count(Kind::READ));
  TEST_ASSERT_EQUAL_UINT32(0, fixture.count(Kind::WRITE));
  TEST_ASSERT_FALSE(fixture.contains("[FAIL]"));
}

void test_receive_fault_retirement_is_not_reset_by_rebind_or_reset() {
  CliFixture fixture;
  bind(fixture);
  fixture.failAt = 1;
  fixture.command("read");
  TEST_ASSERT_TRUE(fixture.receiveFaulted);
  const auto physicalAttempts = fixture.transfers.size();
  fixture.command("recover");
  fixture.command("reset");
  fixture.command("end");
  fixture.command("begin");
  TEST_ASSERT_TRUE(fixture.receiveFaulted);
  TEST_ASSERT_EQUAL_UINT32(physicalAttempts, fixture.transfers.size());
  TEST_ASSERT_FALSE(fixture.device.isBound());
}

void test_prepare_failure_and_unavailable_owner_do_not_start_transactions() {
  CliFixture fixture;
  fixture.command("color off");
  fixture.prepareResult = TCA9548A::Status::Error(TCA9548A::Err::I2C_ERROR,
                                                "registration failed", -31);
  fixture.command("init");
  TEST_ASSERT_FALSE(fixture.device.isBound());
  TEST_ASSERT_EQUAL_UINT32(1, fixture.prepareCalls);
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  TEST_ASSERT_TRUE(fixture.contains("registration failed"));
  fixture.prepareResult = TCA9548A::Status::Ok();
  fixture.ready = false;
  fixture.command("begin");
  fixture.command("discover");
  TEST_ASSERT_FALSE(fixture.device.isBound());
  TEST_ASSERT_FALSE(fixture.cli.active());
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  fixture.ready = true;
  fixture.command("init");
  TEST_ASSERT_TRUE(fixture.device.isBound());
  fixture.clearHistory();
  fixture.command("unbind");
  TEST_ASSERT_FALSE(fixture.device.isBound());
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
}

void test_settings_boundary_values_and_unbound_snapshot_use_staged_values() {
  CliFixture fixture;
  fixture.command("color off");
  fixture.command("timeout 60000");
  fixture.command("resettimeout 60000");
  fixture.command("offline 255");
  TEST_ASSERT_EQUAL_UINT32(60000, fixture.config.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(60000, fixture.config.resetTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(255, fixture.config.offlineThreshold);
  fixture.command("timeout 1");
  fixture.command("resettimeout 1");
  fixture.command("offline 1");
  TEST_ASSERT_EQUAL_UINT32(1, fixture.config.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(1, fixture.config.resetTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(1, fixture.config.offlineThreshold);
  fixture.command("addr 0x71");
  fixture.output.clear();
  fixture.command("snapshot");
  TEST_ASSERT_TRUE(fixture.contains("Settings source: staged (unbound)"));
  TEST_ASSERT_TRUE(fixture.contains("I2C address: 0x71"));
  TEST_ASSERT_TRUE(fixture.contains("I2C timeout: 1 ms"));
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
}

void test_verify_requires_evidence_and_mismatch_does_not_change_register() {
  CliFixture fixture;
  bind(fixture);
  fixture.command("invalidate");
  fixture.command("verify");
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  TEST_ASSERT_TRUE(fixture.contains("INVALID_PARAM"));
  fixture.hardwareMask = 0x80;
  fixture.output.clear();
  fixture.command("verify 0");
  TEST_ASSERT_EQUAL_UINT32(1, fixture.count(Kind::READ));
  TEST_ASSERT_EQUAL_UINT32(0, fixture.count(Kind::WRITE));
  TEST_ASSERT_EQUAL_HEX8(0x80, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.device.channelMaskObservation().verified());
  TEST_ASSERT_FALSE(fixture.contains("verify: OK"));
}

void test_physical_counters_include_probe_and_assertions_leave_health_unchanged() {
  CliFixture fixture;
  bind(fixture);
  const uint32_t successes = fixture.device.totalSuccess();
  TEST_ASSERT_EQUAL_UINT32(4, fixture.stats.attempts);
  TEST_ASSERT_EQUAL_UINT32(3, successes);
  fixture.command("xfer_reset");
  TEST_ASSERT_EQUAL_UINT32(0, fixture.stats.attempts);
  TEST_ASSERT_EQUAL_UINT32(successes, fixture.device.totalSuccess());
  fixture.command("read");
  fixture.command("probe");
  TEST_ASSERT_EQUAL_UINT32(2, fixture.stats.attempts);
  TEST_ASSERT_EQUAL_UINT32(successes + 1, fixture.device.totalSuccess());
  fixture.command("xfer_assert 2");
  fixture.command("xfer_assert 2 2 0");
  TEST_ASSERT_TRUE(fixture.contains("xfer_assert: PASS"));
  fixture.failAt = 3;
  fixture.command("off");
  TEST_ASSERT_EQUAL_UINT32(3, fixture.stats.attempts);
  TEST_ASSERT_EQUAL_UINT32(2, fixture.stats.successes);
  TEST_ASSERT_EQUAL_UINT32(1, fixture.stats.failures);
  TEST_ASSERT_EQUAL_UINT32(1, fixture.device.totalFailures());
  fixture.output.clear();
  fixture.command("xfer_assert 3 2 1");
  fixture.command("xfer_stats");
  fixture.command("counters");
  TEST_ASSERT_TRUE(fixture.contains("xfer_assert: PASS"));
  TEST_ASSERT_TRUE(fixture.contains("attempts=3"));
  fixture.output.clear();
  fixture.command("xfer_assert 4");
  TEST_ASSERT_TRUE(fixture.contains("xfer_assert: FAIL"));
  TEST_ASSERT_EQUAL_UINT32(3, fixture.transfers.size());
}

void test_physical_counter_saturation_is_explicit_and_not_assertable() {
  CliFixture fixture;
  fixture.command("color off");
  fixture.stats.attempts = UINT32_MAX - 1U;
  fixture.stats.successes = UINT32_MAX - 1U;
  fixture.stats.reads = UINT32_MAX - 1U;
  fixture.stats.record(Kind::READ, true);
  fixture.stats.record(Kind::READ, true);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, fixture.stats.attempts);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, fixture.stats.successes);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, fixture.stats.reads);
  fixture.command("xfer_stats");
  fixture.command("xfer_assert 0");
  TEST_ASSERT_TRUE(fixture.contains("saturated=yes"));
  TEST_ASSERT_TRUE(fixture.contains("xfer_assert: FAIL"));
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
}

void test_health_preserves_full_backend_diagnostics_after_recovery() {
  CliFixture fixture;
  bind(fixture);
  fixture.failAt = 1;
  fixture.command("off");
  fixture.command("recover");
  fixture.output.clear();
  fixture.command("health");
  TEST_ASSERT_TRUE(fixture.contains("State: READY"));
  TEST_ASSERT_TRUE(fixture.contains("Consecutive failures: 0"));
  TEST_ASSERT_TRUE(fixture.contains("Last error: I2C_TIMEOUT"));
  TEST_ASSERT_TRUE(fixture.contains("detail=-77"));
  TEST_ASSERT_TRUE(fixture.contains("I2C transaction timeout"));
  TEST_ASSERT_EQUAL_UINT32(2, fixture.transfers.size());
}

void test_color_help_categories_and_verbose_output_are_runtime_settings() {
  CliFixture fixture;
  fixture.command("help");
  TEST_ASSERT_TRUE(fixture.contains("\033["));
  for (const char* group : {"[Common]", "[Configuration]", "[Switch]", "[Diagnostics]"}) {
    TEST_ASSERT_TRUE(fixture.contains(group));
  }
  fixture.command("color 0");
  fixture.output.clear();
  fixture.command("help");
  TEST_ASSERT_FALSE(fixture.contains("\033["));
  fixture.command("color 1");
  fixture.output.clear();
  fixture.command("help");
  TEST_ASSERT_TRUE(fixture.contains("\033["));
  bind(fixture);
  fixture.command("verbose 1");
  fixture.command("stress 1");
  finish(fixture);
  TEST_ASSERT_TRUE(fixture.contains("round=1 verified="));
  fixture.command("verbose off");
  fixture.clearHistory();
  fixture.command("stress 1");
  finish(fixture);
  TEST_ASSERT_FALSE(fixture.contains("round=1 verified="));
}

void test_run_statistics_are_cached_and_reset_separately_from_health() {
  CliFixture fixture;
  bind(fixture);
  fixture.command("watch 2 1");
  tick(fixture);
  const auto attempts = fixture.transfers.size();
  fixture.command("stats");
  fixture.command("result");
  fixture.command("stats reset");
  fixture.command("xfer_reset");
  TEST_ASSERT_TRUE(fixture.cli.active());
  TEST_ASSERT_EQUAL_UINT32(attempts, fixture.transfers.size());
  finish(fixture);
  fixture.output.clear();
  fixture.command("stats");
  const std::string completedStats = fixture.output;
  fixture.clockMs += 1000;
  fixture.command("read");
  fixture.output.clear();
  fixture.command("result");
  TEST_ASSERT_EQUAL_STRING(completedStats.c_str(), fixture.output.c_str());
  const auto successes = fixture.device.totalSuccess();
  fixture.output.clear();
  fixture.command("result reset");
  fixture.command("stats");
  TEST_ASSERT_TRUE(fixture.contains("No diagnostic run statistics"));
  TEST_ASSERT_EQUAL_UINT32(successes, fixture.device.totalSuccess());
}

void test_scan_transport_error_is_not_counted_as_absence_or_retried() {
  CliFixture fixture;
  bind(fixture);
  fixture.command("scan");
  fixture.failAt = 2;
  finish(fixture);
  TEST_ASSERT_EQUAL_UINT32(1, fixture.count(Kind::READ));
  TEST_ASSERT_EQUAL_UINT32(1, fixture.count(Kind::PROBE));
  TEST_ASSERT_TRUE(fixture.contains("errors=1"));
  TEST_ASSERT_TRUE(fixture.contains("outcome=FAIL"));
  TEST_ASSERT_EQUAL_UINT32(0, fixture.count(Kind::WRITE));
}

void test_failed_cleanup_is_retained_even_when_final_read_happens_to_match() {
  CliFixture fixture;
  bind(fixture);
  fixture.command("stress 1");
  tick(fixture);
  tick(fixture);
  TEST_ASSERT_TRUE(fixture.cli.active());
  fixture.hardwareMask = 0;
  fixture.failAt = fixture.transfers.size() + 1;
  finish(fixture);
  TEST_ASSERT_TRUE(fixture.device.channelMaskObservation().verified());
  TEST_ASSERT_EQUAL_HEX8(0, fixture.hardwareMask);
  TEST_ASSERT_TRUE(fixture.contains("safe_off=FAILED"));
  TEST_ASSERT_TRUE(fixture.contains("Cleanup result: I2C_TIMEOUT"));
  TEST_ASSERT_TRUE(fixture.contains("outcome=FAIL"));
}

void test_cancel_before_sweep_capture_is_bus_silent_and_terminal_prompt_unique() {
  CliFixture fixture;
  bind(fixture);
  fixture.command("sweep");
  TEST_ASSERT_FALSE(fixture.contains("> "));
  fixture.command("stop");
  TEST_ASSERT_FALSE(fixture.cli.active());
  TEST_ASSERT_EQUAL_UINT32(0, fixture.transfers.size());
  TEST_ASSERT_TRUE(fixture.contains("outcome=CANCELLED"));
  const size_t prompt = fixture.output.find("> ");
  TEST_ASSERT_NOT_EQUAL(std::string::npos, prompt);
  TEST_ASSERT_EQUAL(std::string::npos, fixture.output.find("> ", prompt + 2));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_setup_and_diagnostic_aliases_are_bus_silent);
  RUN_TEST(test_begin_precheck_retains_working_owner_for_nondefault_strap);
  RUN_TEST(test_runtime_settings_are_bounded_and_apply_only_after_explicit_begin);
  RUN_TEST(test_invalid_settings_never_change_config_or_retarget_owner);
  RUN_TEST(test_lifecycle_rebind_invalidation_and_probe_preserve_contracts);
  RUN_TEST(test_all_channel_commands_update_actual_register_and_read_modify_write);
  RUN_TEST(test_read_modify_write_never_writes_after_failed_read);
  RUN_TEST(test_reset_aliases_verify_hardware_and_report_absent_or_unwired_reset);
  RUN_TEST(test_hil_parser_and_dry_modes_execute_without_bus_access);
  RUN_TEST(test_live_hil_checks_every_channel_and_restores_verified_entry_mask);
  RUN_TEST(test_hil_mismatch_and_restore_failure_cannot_report_success);
  RUN_TEST(test_malformed_and_discarded_lines_never_dispatch_mutating_prefix);
  RUN_TEST(test_sweep_is_cooperative_exhaustive_and_restores_original_mask);
  RUN_TEST(test_cancelled_sweep_restores_mask_and_rejects_competing_mutation);
  RUN_TEST(test_sweep_readback_failure_stops_and_restores_entry);
  RUN_TEST(test_stress_samples_verify_channels_and_finish_all_off);
  RUN_TEST(test_mixed_stress_covers_all_channels_and_detects_readback_mismatch);
  RUN_TEST(test_scan_and_discover_have_bounded_probes_and_no_register_writes);
  RUN_TEST(test_watch_honors_count_interval_wrap_and_bus_silent_stop);
  RUN_TEST(test_soak_has_finite_deadline_and_preserves_selected_route);
  RUN_TEST(test_receive_fault_retirement_is_not_reset_by_rebind_or_reset);
  RUN_TEST(test_prepare_failure_and_unavailable_owner_do_not_start_transactions);
  RUN_TEST(test_settings_boundary_values_and_unbound_snapshot_use_staged_values);
  RUN_TEST(test_verify_requires_evidence_and_mismatch_does_not_change_register);
  RUN_TEST(test_physical_counters_include_probe_and_assertions_leave_health_unchanged);
  RUN_TEST(test_physical_counter_saturation_is_explicit_and_not_assertable);
  RUN_TEST(test_health_preserves_full_backend_diagnostics_after_recovery);
  RUN_TEST(test_color_help_categories_and_verbose_output_are_runtime_settings);
  RUN_TEST(test_run_statistics_are_cached_and_reset_separately_from_health);
  RUN_TEST(test_scan_transport_error_is_not_counted_as_absence_or_retried);
  RUN_TEST(test_failed_cleanup_is_retained_even_when_final_read_happens_to_match);
  RUN_TEST(test_cancel_before_sweep_capture_is_bus_silent_and_terminal_prompt_unique);
  return UNITY_END();
}
