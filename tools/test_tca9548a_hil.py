#!/usr/bin/env python3
"""Host-only HIL framing, evidence, fixture and interruption regressions."""

from __future__ import annotations

import contextlib
import io
import types
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import tca9548a_hil as hil


HEALTH = ("=== Driver Health ===\n  State: READY (passive; never gates I2C)\n"
          "  State alias parity: yes\n  Bound: yes\n  Initialized: yes\n"
          "  Consecutive failures: 0\n  Last error: I2C_TIMEOUT\n> ")
BASE_CASES = ("typed ChannelMask is one byte", "address helper", "version")
LIVE_CASES = ("probe", "probe no-health-side-effects", "capture original mask readback",
              "disableAll write", "disableAll readback", "all eight one-hot channels",
              "write mask 0xA5", "read mask 0xA5", "recover safe-off write",
              "recover readback 0x00", "final verified mask restore")
RESET_CASES = ("hardReset seed write", "hardReset seed readback",
               "hardReset exact-zero verification", "hardReset leaves verified all-off")

CFG_FIELDS = ("Bound: yes\nInitialized: yes\nController restart required: no\n"
              "Config reference parity: yes\nSnapshot: OK\n")


def cfg_text(address: int = 0x70, timeout_ms: int = 20) -> str:
    return (f"=== Configuration ===\nI2C address: 0x{address:02X}\n" + CFG_FIELDS
            + f"I2C timeout: {timeout_ms} ms\n> ")


def hil_text(*, reset: bool = True, dry: bool = False) -> str:
    labels = BASE_CASES + (() if dry else LIVE_CASES + (RESET_CASES if reset else ()))
    skips = ("probe", "mask I/O", "hardReset") if dry else (() if reset else ("hardReset",))
    title = "DRY-RUN" if dry else "RUN"
    return (f"=== TCA9548A HIL {title} ===\n"
            + "".join(f"  [PASS] {label}\n" for label in labels)
            + "".join(f"  [SKIP] {label} - not requested\n" for label in skips)
            + f"HIL result: pass={len(labels)} fail=0 skip={len(skips)}\n> ")


def scan_text(mask: int, addresses: set[int], errors: int = 0) -> str:
    return (f"Scan topology: OK active_mask=0x{mask:02X} [channels]\n"
            "Scanning I2C bus (112 bounded probes)...\n"
            + "".join(f"  Found device at 0x{address:02X}\n" for address in sorted(addresses))
            + f"Scan complete: devices={len(addresses)} errors={errors}\n> ")


def console_text(*, dropped=0, messages=0, overflows=0, accepted=100, connected="yes") -> str:
    return (f"Console: backend=test connected={connected} queued_bytes=0 accepted_bytes={accepted} "
            f"dropped_bytes={dropped} dropped_messages={messages} rx_overflows={overflows}\n> ")


def stress_text(count: int, *, mixed=True) -> str:
    return (("=== stress_mix summary ===\n" if mixed else "")
            + f"Stress results: completed={count} requested={count} status=OK safe_off=OK\n"
            + f"Duration: 3 ms\nHealth delta: success={(6 if mixed else 2) * count + 2} failure=0\n"
            + f"Session transfers: success={(7 if mixed else 2) * count + 2} failure=0\n> ")


def discover_text(addresses=None, target=0x70) -> str:
    addresses = {0x70} if addresses is None else addresses
    return ("discover started: requested=8; stop cancels\n"
            + "".join(f"  Found device at 0x{address:02X}\n" for address in sorted(addresses))
            + f"Discover complete: devices={len(addresses)} errors=0\n"
            + f"Detection: tested=8 ack={len(addresses)} absent={8 - len(addresses)} errors=0 "
            + f"target=0x{target:02X} target_ack={'yes' if target in addresses else 'no'} identity=unverified\n"
            + "CLI result: command=discover outcome=PASS completed=8 requested=8\n> ")


class Clock:
    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


class FakeSerial:
    def __init__(self, clock: Clock, chunks: list[bytes | None], *, short_write=False) -> None:
        self.clock = clock
        self.chunks = chunks[:]
        self.writes: list[bytes] = []
        self.short_write = short_write
        self.timeout = 0.05

    @property
    def in_waiting(self) -> int:
        return len(self.chunks[0]) if self.chunks and self.chunks[0] else 0

    def read(self, size: int) -> bytes:
        self.clock.sleep(self.timeout)
        if not self.chunks:
            return b""
        chunk = self.chunks.pop(0)
        if chunk is None:
            return b""
        if len(chunk) > size:
            self.chunks.insert(0, chunk[size:])
        return chunk[:size]

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data) - int(self.short_write)

    def flush(self) -> None:
        raise AssertionError("flush is not timeout-bounded")


class FixtureRunner:
    """Host fixture model only; never used by live runner/production code."""

    def __init__(self, *, clock: Clock | None = None) -> None:
        self.mask = 0x12
        self.routes = {(0, 0x48), (7, 0x48), (4, 0x49)}
        self.upstream = {0x70, 0x50}
        self.synchronized = True
        self.commands: list[str] = []
        self.responses: dict[str, str] = {}
        self.clock = clock
        self.interrupt_at: str | None = None
        self.verbose = False

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        return None

    def read_until_idle(self) -> str:
        return "boot\n> "

    def command(self, command: str) -> hil.Response:
        self.commands.append(command)
        if self.clock:
            self.clock.sleep(0.1)
        if command == self.interrupt_at:
            self.interrupt_at = None
            raise KeyboardInterrupt
        if command in self.responses:
            text = self.responses[command]
        elif command == "read":
            text = f"read: OK mask=0x{self.mask:02X} [mask]\n> "
        elif command == "scan":
            visible = self.upstream | {address for channel, address in self.routes if self.mask & (1 << channel)}
            text = scan_text(self.mask, visible)
        elif command.startswith("mask "):
            self.mask = int(command.split()[1], 0)
            text = f"mask 0x{self.mask:02X}: OK\n> "
        elif command.startswith("select "):
            self.mask = 1 << int(command.split()[1])
            text = f"{command}: OK\n> "
        elif command in ("off", "recover"):
            self.mask = 0
            label = "recover (safe-off write)" if command == "recover" else command
            text = f"{label}: OK\n> "
        elif command == "health":
            text = HEALTH
        elif command == "probe":
            text = "probe: OK\n> "
        elif command == "console":
            text = console_text()
        elif command in ("discover", "detect"):
            text = discover_text(self.upstream & set(range(0x70, 0x78)))
        elif command.startswith("verbose"):
            if command != "verbose":
                self.verbose = command.split()[1] == "on"
            text = f"verbose: {'on' if self.verbose else 'off'}\n> "
        elif command.startswith("stress_mix "):
            self.mask = 0
            text = stress_text(int(command.split()[1]))
        else:
            text = "unexpected command\n> "
        return hil.Response(text, 0.1, "prompt")


class EvidenceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.args = hil.parse_args([])
        self.plan = {step.test_id: step for step in hil.build_plan(self.args)}

    def classify(self, command: str, text: str, expected=hil.PASS) -> None:
        step = next((step for step in self.plan.values() if step.command == command), hil.operation_step(command))
        self.assertEqual(hil.classify(text, step)[0], expected, text)

    def test_live_reset_requires_seed_and_complete_case_counts(self):
        self.classify("hil run reset", hil_text())
        for label in BASE_CASES + LIVE_CASES + RESET_CASES:
            with self.subTest(label=label):
                text = hil_text().replace(f"  [PASS] {label}\n", "")
                self.assertNotEqual(hil.classify(text, self.plan["TCA-HIL-008"])[0], hil.PASS)
        self.classify("hil run reset", hil_text().replace("fail=0", "fail=1"), hil.FAIL)
        self.classify("hil run reset", hil_text().replace("skip=0", "skip=1"), hil.FAIL)
        self.classify("hil run reset", hil_text().replace("HIL result:", "truncated:"), hil.UNKNOWN)
        self.classify("hil run reset", hil_text() + "\nHIL result: pass=18 fail=0 skip=0\n", hil.UNKNOWN)

    def test_hil_dry_and_explicit_skip_reset(self):
        self.classify("hil dry", hil_text(dry=True))
        step = next(step for step in hil.build_plan(hil.parse_args(["--skip-reset"])) if step.test_id == "TCA-HIL-008")
        self.assertEqual(hil.classify(hil_text(reset=False), step)[0], hil.PASS)
        self.classify("hil run reset", hil_text(reset=False), hil.FAIL)

    def test_parser_check_requires_actual_parser_results(self):
        self.classify("hil parser", hil_text(dry=True), hil.FAIL)
        text = hil_text(dry=True).replace("HIL result: pass=3", "  [PASS] numeric parser rejects invalid input\n"
                                       "  [PASS] line parser rejects controls and overflow\nHIL result: pass=5")
        self.classify("hil parser", text)

    def test_restart_banners_cannot_be_valid_command_evidence(self):
        for banner in ("ESP-ROM:esp32s3-20210327", "rst:0x1 (POWERON)", "boot:0x8", "Brownout detector was triggered",
                       "Guru Meditation Error", "Rebooting...", "TCA9548A Arduino bring-up CLI",
                       "TCA9548A native ESP-IDF bring-up CLI"):
            with self.subTest(banner=banner):
                self.classify("version", banner + "\n=== Version Info ===\nLibrary: test\n> ", hil.FAIL)

    def test_discovery_requires_full_unique_address_and_terminal_evidence(self):
        self.classify("discover", discover_text({0x70, 0x76}))
        self.classify("detect", discover_text(set()))
        for text in (discover_text().replace("tested=8", "tested=7"),
                     discover_text().replace("absent=7", "absent=6"),
                     discover_text().replace("target_ack=yes", "target_ack=no"),
                     discover_text().replace("completed=8", "completed=7"),
                     discover_text().replace("Discover complete: devices=1", "Discover complete: devices=2"),
                     discover_text({0x69}),
                     discover_text().replace("Found device at 0x70", "Found device at 0x70\n  Found device at 0x70"),
                     discover_text() + discover_text()):
            with self.subTest(text=text):
                self.assertNotEqual(hil.classify(text, hil.operation_step("discover"))[0], hil.PASS)

    def test_console_loss_evidence_is_bounded_unambiguous_and_monotonic(self):
        baseline = hil.parse_console(console_text(dropped=10, messages=2, accepted=100))
        hil.compare_console(console_text(dropped=10, messages=2, accepted=101), baseline)
        hil.parse_console(console_text(connected="unknown"))
        for text in (console_text(dropped=11, messages=2), console_text(dropped=9, messages=2),
                     console_text(dropped=10, messages=3), console_text(dropped=10, messages=2, overflows=1),
                     console_text(dropped=10, messages=2, accepted=99), console_text(dropped=0xFFFFFFFF),
                     console_text(accepted=0x100000000), console_text(connected="no"),
                     console_text() + console_text(), console_text().replace("rx_overflows=0", "rx_overflows=nan")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                hil.compare_console(text, baseline)

    def test_cfg_reports_valid_owner_timeout_without_hard_coding_default(self):
        for timeout_ms in (1, 20, 50, 60000):
            text = cfg_text(timeout_ms=timeout_ms)
            status, notes = hil.classify(text, self.plan["TCA-HIL-003"])
            self.assertEqual(status, hil.PASS)
            self.assertIn(f"firmware I2C timeout={timeout_ms} ms (reported setting)", notes)

    def test_cfg_rejects_missing_and_duplicate_timeout_fields(self):
        base = "=== Configuration ===\nI2C address: 0x70\n" + CFG_FIELDS
        for fields in ("", "I2C timeout: 20 ms\nI2C timeout: 20 ms\n",
                       "I2C timeout: 20 ms\nI2C timeout: invalid\n"):
            with self.subTest(fields=fields):
                self.classify("cfg", base + fields + "> ", hil.FAIL)

    def test_cfg_rejects_malformed_zero_and_excessive_timeouts(self):
        base = "=== Configuration ===\nI2C address: 0x70\n" + CFG_FIELDS
        for value in ("0 ms", "60001 ms", "-1 ms", "nan ms", "inf ms", "20.0 ms",
                      "20 us", "20", "20 ms trailing", "9" * 5000 + " ms"):
            with self.subTest(value=value[:40]):
                self.classify("cfg", base + "  I2C timeout: " + value + "\n> ", hil.FAIL)

    def test_cfg_rejects_unbound_faulted_and_contradictory_snapshots(self):
        for field in CFG_FIELDS.splitlines():
            with self.subTest(field=field):
                self.classify("cfg", cfg_text().replace(field + "\n", ""), hil.FAIL)
                self.classify("cfg", cfg_text() + "\n" + field + "\n", hil.FAIL)
        for before, after in (("Bound: yes", "Bound: no"),
                              ("Initialized: yes", "Initialized: no"),
                              ("Controller restart required: no", "Controller restart required: yes"),
                              ("Config reference parity: yes", "Config reference parity: no"),
                              ("Snapshot: OK", "Snapshot: INVALID_CONFIG")):
            self.classify("cfg", cfg_text().replace(before, after), hil.FAIL)

    def test_health_ignores_sticky_error_but_requires_current_ready(self):
        self.classify("health", HEALTH)
        for before, after in (("State: READY", "State: OFFLINE"), ("State: READY", "State: UNINIT"),
                              ("Consecutive failures: 0", "Consecutive failures: 1"),
                              ("Bound: yes", "Bound: no"), ("Initialized: yes", "Initialized: no"),
                              ("State alias parity: yes", "State alias parity: no")):
            self.classify("health", HEALTH.replace(before, after), hil.FAIL)
        self.classify("health", HEALTH + "\nState: OFFLINE\nConsecutive failures: 1\n", hil.FAIL)

    def test_scan_requires_topology_counts_and_zero_errors(self):
        text = scan_text(0, {0x70, 0x48})
        self.classify("scan", text)
        self.classify("scan", text.replace("errors=0", "errors=1"), hil.FAIL)
        self.classify("scan", text.replace(" errors=0", ""), hil.UNKNOWN)
        self.classify("scan", text.replace("devices=2", "devices=1"), hil.FAIL)
        self.classify("scan", text.replace("0x48\n", "0x70\n"), hil.FAIL)
        self.classify("scan", text.replace("topology: OK", "topology: I2C_TIMEOUT"), hil.FAIL)
        self.classify("scan", text.replace("Scan complete:", "interrupted:"), hil.UNKNOWN)

    def test_stress_requires_full_count_and_verified_safe_off(self):
        step = hil.operation_step("stress 100")
        text = ("Stress results: completed=100 requested=100 status=OK safe_off=OK\n"
                "Duration: 23 ms\nHealth delta: success=102 failure=0\n> ")
        self.assertEqual(hil.classify(text, step)[0], hil.PASS)
        for before, after in (("completed=100", "completed=99"), ("requested=100", "requested=99"),
                              ("safe_off=OK", "safe_off=FAILED"), ("status=OK", "status=I2C_BUS"),
                              ("failure=0", "failure=1")):
            self.assertEqual(hil.classify(text.replace(before, after), step)[0], hil.FAIL)
        self.assertEqual(hil.classify("stress results started\n> ", step)[0], hil.UNKNOWN)

    def test_operation_requires_explicit_ok_and_mask_readback(self):
        for command in ("probe", "off", "recover", "select 0", "mask 255"):
            self.classify(command, command + ":\n> ", hil.FAIL)
        self.classify("probe", "probe: OK\n> ")
        step = hil.operation_step("read", 0xA5)
        self.assertEqual(hil.classify("read: OK mask=0xA5 [mask]\n> ", step)[0], hil.PASS)
        self.assertEqual(hil.classify("read: OK mask=0xA4 [mask]\n> ", step)[0], hil.FAIL)
        self.assertEqual(hil.classify("read: OK\n> ", step)[0], hil.UNKNOWN)

    def test_timeout_overrides_complete_tokens_and_faults_override_pass(self):
        step = self.plan["TCA-HIL-001"]
        text = "=== Version Info ===\n  Library: 1.0\n"
        self.assertEqual(hil.classify(text, step, "timeout")[0], hil.UNKNOWN)
        for fault in ("[E] invalid", "Command discarded: too long", "Guru Meditation", "Rebooting", "[FAIL] bad"):
            self.assertEqual(hil.classify(text + fault, step)[0], hil.FAIL)
        self.assertEqual(hil.classify("\x1b[32m" + text + "\x1b[0m", step)[0], hil.PASS)

    def test_oversized_summary_counters_never_raise_or_pass(self):
        oversized = "9" * 5000
        cases = (
            ("hil dry", hil_text(dry=True).replace("pass=3", "pass=" + oversized)),
            ("hil dry", hil_text(dry=True).replace("skip=3", "skip=" + oversized)),
            ("hil dry", hil_text(dry=True).replace("fail=0", "fail=" + "0" * 5000)),
            ("scan", scan_text(0, {0x70}).replace("devices=1", "devices=" + oversized)),
            ("scan", scan_text(0, {0x70}).replace("errors=0", "errors=" + "0" * 5000)),
        )
        for command, response in cases:
            with self.subTest(command=command, suffix=response[-40:]):
                step = next(step for step in self.plan.values() if step.command == command)
                self.assertNotEqual(hil.classify(response, step)[0], hil.PASS)
        stress = ("Stress results: completed=1 requested=1 status=OK safe_off=OK\n"
                  "Health delta: success=3 failure=0\n> ")
        for field in ("completed=1", "requested=1", "success=3"):
            response = stress.replace(field, field.split("=")[0] + "=" + oversized)
            self.assertNotEqual(hil.classify(response, hil.operation_step("stress 1"))[0], hil.PASS)

    def test_stress_health_counter_is_uint32_and_unique(self):
        prefix = "Stress results: completed=1 requested=1 status=OK safe_off=OK\n"
        step = hil.operation_step("stress 1")
        for suffix in ("Health delta: success=4294967296 failure=0\n",
                       "Health delta: success=3 failure=0\nHealth delta: success=3 failure=0\n"):
            self.assertEqual(hil.classify(prefix + suffix, step)[0], hil.FAIL)

    def test_stress_requires_transfer_evidence_even_when_lifetime_counter_saturates(self):
        prefix = ("Stress results: completed=100 requested=100 status=OK safe_off=OK\n"
                  "Health delta: success=0 failure=0\n")
        self.classify("stress 100", prefix, hil.FAIL)
        self.classify("stress 100", prefix + "Session transfers: success=202 failure=0\n")
        self.classify("stress 100", prefix + "Session transfers: success=201 failure=0\n", hil.FAIL)
        self.classify("stress 100", prefix + "Session transfers: success=202 failure=1\n", hil.FAIL)
        for command, count in (("stress 100", 202), ("stress_mix 100", 702)):
            self.classify(command, prefix + f"Session transfers: success={count} failure=0\n")
            for evidence in ("success=broken failure=0", "success=9999999999 failure=0",
                             f"success={count - 1} failure=0", f"success={count + 1} failure=0"):
                # Even a plausible legacy health delta must not hide bad new evidence.
                self.classify(command, prefix.replace("success=0", "success=1000") +
                              f"Session transfers: {evidence}\n", hil.FAIL)

    def test_scan_requires_one_topology_and_extracts_only_device_records(self):
        text = scan_text(0, {0x70})
        self.classify("scan", text + "\nScan topology: OK active_mask=0x01\n", hil.FAIL)
        text = "debug: Found device at 0x48\n" + text
        self.classify("scan", text)
        self.assertEqual(hil.scan_addresses(text, 0), {0x70})


class SerialTests(unittest.TestCase):
    def test_control_lines_are_configured_before_open_and_closed_on_failure(self):
        for flags, expected in (([], (True, False)), (["--dtr", "0", "--rts", "1"], (False, True))):
            for fail in (False, True):
                with self.subTest(flags=flags, fail=fail):
                    port = Mock()
                    def open_port():
                        self.assertEqual((port.dtr, port.rts), expected)
                        self.assertEqual(port.port, "COM8")
                        if fail:
                            raise OSError("open failed")
                    port.open.side_effect = open_port
                    factory = Mock(return_value=port)
                    with patch.dict("sys.modules", {"serial": types.SimpleNamespace(Serial=factory)}), \
                            patch.object(hil.time, "sleep"):
                        runner = hil.SerialRunner(hil.parse_args(["--port", "COM8", *flags]))
                        if fail:
                            with self.assertRaises(OSError):
                                runner.__enter__()
                        else:
                            with runner:
                                pass
                    self.assertIsNone(factory.call_args.kwargs["port"])
                    port.close.assert_called_once()

    def exchange(self, chunks, *, timeout=2.0, short_write=False):
        clock = Clock()
        runner = hil.SerialRunner(hil.parse_args(["--timeout-s", str(timeout)]))
        runner.serial = FakeSerial(clock, chunks, short_write=short_write)
        with patch.object(hil.time, "monotonic", clock.monotonic):
            response = runner.command("version")
        return runner, response, clock

    def test_fragmented_ansi_prompt_completes_after_long_silence(self):
        chunks = [b"=== Version Info ===\nLibrary: 1\n"] + [None] * 12 + [b"\x1b[36m>", b" \x1b[0m"]
        runner, response, clock = self.exchange(chunks)
        self.assertEqual(response.completion, "prompt")
        self.assertGreater(clock.now, 0.4)
        self.assertTrue(runner.synchronized)
        self.assertEqual(runner.serial.writes, [b"version\n"])

    def test_complete_text_without_prompt_times_out_and_stops_session(self):
        runner, response, clock = self.exchange([b"=== Version Info ===\nLibrary: 1\n"], timeout=0.2)
        self.assertIn("timeout", response.completion)
        self.assertLessEqual(clock.now, 0.201)
        self.assertFalse(runner.synchronized)
        with self.assertRaises(hil.SessionLost):
            runner.command("help")
        self.assertEqual(len(runner.serial.writes), 1)

    def test_stale_prompt_cannot_pass_a_new_command(self):
        _, response, _ = self.exchange([b"> ", b"=== Version Info ===\nLibrary: stale\n> "])
        step = hil.build_plan(hil.parse_args([]))[0]
        self.assertEqual(hil.classify(response.text, step, response.completion)[0], hil.UNKNOWN)

    def test_coalesced_stale_and_new_responses_lose_attribution(self):
        runner, response, _ = self.exchange([b"> === Version Info ===\nLibrary: stale\n> "])
        self.assertIn("multiple CLI responses", response.completion)
        self.assertFalse(runner.synchronized)

    def test_response_overflow_is_bounded(self):
        runner, response, _ = self.exchange([b"x" * (hil.MAX_RESPONSE_BYTES * 2)])
        self.assertEqual(len(response.text), hil.MAX_RESPONSE_BYTES)
        self.assertIn("bounded", response.completion)
        self.assertFalse(runner.synchronized)

    def test_short_write_is_session_loss(self):
        with self.assertRaises(hil.SessionLost):
            self.exchange([], short_write=True)

    def test_reset_retires_session_even_when_boot_contains_valid_response(self):
        runner, response, _ = self.exchange([b"ESP-", b"ROM:esp32s3\n=== Version Info ===\nLibrary: test\n> "])
        self.assertIn("restarted", response.completion)
        self.assertFalse(runner.synchronized)
        with self.assertRaises(hil.SessionLost):
            runner.command("off")
        self.assertEqual(runner.serial.writes, [b"version\n"])

    def test_framework_startup_banners_retire_session_without_rom_output(self):
        for banner in (b"TCA9548A Arduino bring-up CLI", b"TCA9548A native ESP-IDF bring-up CLI"):
            with self.subTest(banner=banner):
                runner, response, _ = self.exchange([banner + b"\n=== Version Info ===\nLibrary: test\n> "])
                self.assertIn("restarted", response.completion)
                self.assertFalse(runner.synchronized)
                with self.assertRaises(hil.SessionLost):
                    runner.command("off")
                self.assertEqual(runner.serial.writes, [b"version\n"])


class FixtureTests(unittest.TestCase):
    def route_args(self):
        return hil.parse_args(["--route", "0:0x48", "--route", "7:0x48", "--route", "4:0x49"])

    def test_restart_during_mask_write_prevents_restoring_captured_state(self):
        runner = FixtureRunner()
        runner.responses["mask 0"] = "ESP-ROM:esp32s3\nmask 0x00: OK\n> "
        text, _ = hil.run_mask_checks(runner, hil.parse_args([]), lambda _: None, routing=False)
        self.assertIn("restore NOT_RUN", text)
        self.assertFalse(runner.synchronized)
        self.assertEqual(runner.commands, ["read", "mask 0"])

    def test_routing_all_eight_channels_shared_address_and_restore(self):
        runner = FixtureRunner()
        text, _ = hil.run_mask_checks(runner, self.route_args(), lambda _: None, routing=True)
        self.assertIn("completed=8 failures=0", text)
        self.assertEqual(runner.mask, 0x12)
        self.assertEqual([command for command in runner.commands if command.startswith("select ")],
                         [f"select {channel}" for channel in range(8)])
        self.assertEqual(runner.commands.count("scan"), 10)
        self.assertEqual(runner.commands[-2:], ["mask 18", "read"])

    def test_route_absent_miswired_and_upstream_collision_fail_with_restore(self):
        for mutation in ("absent", "miswired", "upstream", "no_mux"):
            with self.subTest(mutation=mutation):
                runner = FixtureRunner()
                if mutation == "absent":
                    runner.routes.remove((0, 0x48))
                elif mutation == "miswired":
                    runner.routes.add((1, 0x48))
                elif mutation == "upstream":
                    runner.upstream.add(0x48)
                else:
                    runner.upstream.remove(0x70)
                text, _ = hil.run_mask_checks(runner, self.route_args(), lambda _: None, routing=True)
                self.assertIn("[FAIL]", text)
                self.assertEqual(runner.mask, 0x12)

    def test_route_scan_timeout_error_is_not_device_absence(self):
        runner = FixtureRunner()
        runner.responses["scan"] = scan_text(0, {0x70}, errors=1)
        text, _ = hil.run_mask_checks(runner, self.route_args(), lambda _: None, routing=True)
        self.assertIn("[FAIL]", text)
        self.assertEqual(runner.commands.count("scan"), 1)
        self.assertEqual(runner.mask, 0x12)

    def test_exhaustive_sweep_and_restore(self):
        runner = FixtureRunner()
        text, _ = hil.run_mask_checks(runner, hil.parse_args(["--sweep-masks"]), lambda _: None, routing=False)
        self.assertIn("completed=256 failures=0", text)
        self.assertEqual(runner.commands[1:-2:2], [f"mask {mask}" for mask in range(256)])
        self.assertEqual(runner.mask, 0x12)

    def test_entry_mask_comes_from_validated_read_line(self):
        runner = FixtureRunner()
        original_command = runner.command

        def command(value):
            response = original_command(value)
            if value == "read":
                return hil.Response("debug mask=0x55\n" + response.text, response.elapsed_s, response.completion)
            return response

        runner.command = command
        text, _ = hil.run_mask_checks(runner, hil.parse_args([]), lambda _: None, routing=False)
        self.assertIn("completed=256 failures=0", text)
        self.assertEqual(runner.mask, 0x12)
        self.assertEqual(runner.commands[-2:], ["mask 18", "read"])

    def test_malformed_route_summary_still_restores_entry_mask(self):
        runner = FixtureRunner()
        runner.responses["scan"] = scan_text(0, {0x70}).replace("devices=1", "devices=" + "9" * 5000)
        text, _ = hil.run_mask_checks(runner, self.route_args(), lambda _: None, routing=True)
        self.assertIn("[FAIL]", text)
        self.assertEqual(runner.mask, 0x12)

    def test_sweep_failure_restores_and_does_not_continue(self):
        runner = FixtureRunner()
        runner.responses["mask 5"] = "mask 0x05: I2C_TIMEOUT\n> "
        text, _ = hil.run_mask_checks(runner, hil.parse_args([]), lambda _: None, routing=False)
        self.assertIn("[FAIL]", text)
        self.assertNotIn("mask 6", runner.commands)
        self.assertEqual(runner.commands[-2:], ["mask 18", "read"])
        self.assertEqual(runner.mask, 0x12)

    def test_restore_failure_cannot_pass(self):
        runner = FixtureRunner()
        runner.responses["mask 18"] = "mask 0x12: I2C_NACK_ADDR\n> "
        text, _ = hil.run_mask_checks(runner, self.route_args(), lambda _: None, routing=True)
        self.assertIn("entry-mask restore unverified", text)
        self.assertIn("[FAIL]", text)

    def test_keyboard_interrupt_still_restores_sweep_entry(self):
        runner = FixtureRunner()
        runner.interrupt_at = "mask 5"
        with self.assertRaises(KeyboardInterrupt):
            hil.run_mask_checks(runner, hil.parse_args([]), lambda _: None, routing=False)
        self.assertEqual(runner.mask, 0x12)
        self.assertEqual(runner.commands[-2:], ["mask 18", "read"])

    def test_lost_prompt_prevents_further_commands_or_unsafe_restore(self):
        runner = FixtureRunner()
        original_command = runner.command

        def command(value):
            if value == "mask 5":
                runner.synchronized = False
                return hil.Response("mask 0x05: OK\n", 1, "timeout")
            return original_command(value)

        runner.command = command
        text, _ = hil.run_mask_checks(runner, hil.parse_args([]), lambda _: None, routing=False)
        self.assertIn("restore NOT_RUN", text)
        self.assertIn("[FAIL]", text)
        self.assertEqual(runner.commands[-1], "read")
        self.assertEqual(runner.mask, 4)


class RunTests(unittest.TestCase):
    def test_live_version_requires_exact_requested_identity(self):
        commit = "a" * 40
        base = "=== Version Info ===\nLibrary: test\n"
        identity = f"Firmware commit: {commit}\nFirmware tree: clean\n"
        variants = ((identity, hil.PASS), ("", hil.FAIL),
                    (identity.replace(commit, "b" * 40), hil.FAIL),
                    (identity.replace("clean", "dirty"), hil.FAIL),
                    (identity + identity, hil.FAIL))
        args = hil.parse_args(["--expect-commit", commit.upper(), "--require-clean", "--command-delay-s", "0"])
        for fields, expected in variants:
            runner = FixtureRunner()
            runner.responses["version"] = base + fields + "> "
            with self.subTest(fields=fields), patch.object(hil, "SerialRunner", return_value=runner):
                results, _ = hil.run_live(args)
            self.assertEqual(results[0].status, expected)
            if expected == hil.FAIL:
                self.assertEqual(runner.commands, ["version"])

    def test_live_scan_requires_configured_mux_even_with_valid_summary(self):
        runner = FixtureRunner()
        runner.responses.update({"version": "=== Version Info ===\nLibrary: test\n> ",
                                 "help": "=== TCA9548A CLI Help ===\nhil [dry|parser|run|run reset]\n> ",
                                 "cfg": cfg_text(), "scan": scan_text(0, {0x50})})
        with patch.object(hil, "SerialRunner", return_value=runner):
            results, _ = hil.run_live(hil.parse_args(["--command-delay-s", "0"]))
        scan = next(result for result in results if result.step.command == "scan")
        self.assertEqual(scan.status, hil.FAIL)
        self.assertIn("mux address missing", scan.notes)
        self.assertEqual(runner.commands[-1], "scan")

    def test_parser_self_test_checks_configuration_timeout(self):
        original_classify = hil.classify

        def classify(text, step, completion="prompt"):
            if step.command == "cfg":
                return hil.PASS, "firmware I2C timeout=20 ms"
            return original_classify(text, step, completion)

        with patch.object(hil, "classify", classify), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(hil.parser_self_test(hil.parse_args([])), 1)

    def test_complete_live_plan_including_optional_host_checks(self):
        clock = Clock()
        runner = FixtureRunner(clock=clock)
        runner.responses.update({"version": "=== Version Info ===\nLibrary: test\n> ",
                                 "help": "=== TCA9548A CLI Help ===\nhil [dry|parser|run|run reset]\n> ",
                                 "cfg": cfg_text(),
                                 "hil dry": hil_text(dry=True), "hil run reset": hil_text(),
                                 "stress 16": "Stress results: completed=16 requested=16 status=OK safe_off=OK\nDuration: 3 ms\nHealth delta: success=18 failure=0\n> ",
                                 "stress_mix 16": "=== stress_mix summary ===\nStress results: completed=16 requested=16 status=OK safe_off=OK\nDuration: 3 ms\nHealth delta: success=18 failure=0\n> "})
        args = hil.parse_args(["--route", "0:0x48", "--route", "7:0x48", "--route", "4:0x49",
                               "--sweep-masks", "--sample-count", "16", "--stress-count", "16",
                               "--soak-duration-s", "1", "--command-delay-s", "0"])
        with patch.object(hil, "SerialRunner", return_value=runner), \
                patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep):
            results, _ = hil.run_live(args)
        self.assertEqual(len(results), 17)
        self.assertTrue(all(result.status == hil.PASS for result in results), [(result.step.command, result.notes) for result in results])
        self.assertIn("firmware I2C timeout=20 ms (reported setting)", results[2].notes)
        self.assertEqual(runner.mask, 0)

    def test_argument_bounds_and_reserved_addresses(self):
        cases = [("--timeout-s", "nan"), ("--timeout-s", "inf"), ("--timeout-s", "0"),
                 ("--scan-timeout-s", "601"), ("--stress-timeout-s", "-1"),
                 ("--idle-timeout-s", "-1"), ("--boot-settle-s", "nan"),
                 ("--command-delay-s", "-1"), ("--soak-duration-s", "inf"),
                 ("--soak-duration-s", "86401"), ("--sample-count", "1001"),
                 ("--stress-count", "-1"), ("--baud", "0"), ("--address", "0x6f"),
                 ("--address", "junk"), ("--route", "8:0x48"), ("--route", "0:0x07"),
                 ("--route", "0:0x78"), ("--route", "0:0x70"), ("--route", "bad"),
                 ("--dtr", "2"), ("--rts", "-1"), ("--expect-commit", "abc123")]
        for case in cases:
            with self.subTest(case=case), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                hil.parse_args(list(case))
        for case in (["--route", "0:0x48", "--route", "0:0x48"],
                     ["--report", "same.txt", "--transcript", "same.txt"]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                hil.parse_args(case)

    def test_optional_fixture_plan_and_unique_ids(self):
        args = hil.parse_args(["--route", "0:0x48", "--sweep-masks", "--sample-count", "1000",
                               "--stress-count", "1000", "--soak-duration-s", "1"])
        plan = hil.build_plan(args)
        self.assertEqual(len({step.test_id for step in plan}), len(plan))
        self.assertEqual([step.command for step in plan[-2:]], ["health", "console"])
        self.assertTrue(all(result.status == hil.NOT_RUN for result in hil.dry_run(args)))

    def test_live_failure_aborts_and_persists_transcript(self):
        runner = FixtureRunner()
        runner.responses["version"] = "=== Version Info ===\nLibrary: test\n> "
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.txt"
            args = hil.parse_args(["--transcript", str(path), "--command-delay-s", "0"])
            with patch.object(hil, "SerialRunner", return_value=runner):
                results, transcript = hil.run_live(args)
            self.assertEqual(results[0].status, hil.PASS)
            self.assertEqual(results[1].status, hil.UNKNOWN)
            self.assertTrue(all(result.status == hil.NOT_RUN for result in results[2:]))
            self.assertEqual(runner.commands, ["version", "help"])
            self.assertEqual(transcript, path)
            self.assertIn("unexpected command", path.read_text(encoding="utf-8"))

    def test_live_oversized_summary_stops_and_remains_reportable(self):
        runner = FixtureRunner()
        runner.responses.update({"version": "=== Version Info ===\nLibrary: test\n> ",
                                 "help": "=== TCA9548A CLI Help ===\nhil [dry|parser|run|run reset]\n> ",
                                 "cfg": cfg_text(),
                                 "scan": scan_text(0, {0x70}).replace("devices=1", "devices=" + "9" * 5000)})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.md"
            args = hil.parse_args(["--report", str(path), "--command-delay-s", "0"])
            with patch.object(hil, "SerialRunner", return_value=runner):
                results, _ = hil.run_live(args)
            scan_index = next(index for index, result in enumerate(results) if result.step.command == "scan")
            self.assertEqual(results[scan_index].status, hil.UNKNOWN)
            self.assertTrue(all(result.status == hil.NOT_RUN for result in results[scan_index + 1:]))
            with patch.object(hil, "git_text", return_value="host"), patch.object(hil, "tool_text", return_value="tool"):
                hil.write_report(path, args, results, None)
            self.assertIn("UNKNOWN", path.read_text(encoding="utf-8"))

    def test_transcript_write_error_preserves_attempted_step_result(self):
        class BrokenTranscript(io.StringIO):
            writes = 0

            def write(self, text):
                self.writes += 1
                if self.writes > 1:
                    raise OSError("disk full")
                return super().write(text)

        runner = FixtureRunner()
        runner.responses["version"] = "=== Version Info ===\nLibrary: test\n> "
        stream = BrokenTranscript()
        args = hil.parse_args(["--transcript", "transcript.txt"])
        with patch.object(hil.Path, "open", return_value=stream), patch.object(hil, "SerialRunner", return_value=runner):
            results, _ = hil.run_live(args)
        self.assertEqual(results[0].status, hil.UNKNOWN)
        self.assertIn("transcript write failed", results[0].notes)
        self.assertTrue(all(result.status == hil.NOT_RUN for result in results[1:]))
        self.assertTrue(stream.closed)

    def test_transcript_close_error_does_not_escape_result_reporting(self):
        class BrokenClose(io.StringIO):
            def close(self):
                super().close()
                raise OSError("flush failed")

        runner = FixtureRunner()
        stream = BrokenClose()
        args = hil.parse_args(["--transcript", "transcript.txt"])
        with patch.object(hil.Path, "open", return_value=stream), patch.object(hil, "SerialRunner", return_value=runner):
            results, _ = hil.run_live(args)
        self.assertEqual(results[0].status, hil.UNKNOWN)
        self.assertIn("transcript close failed", results[0].notes)

    def test_transcript_write_error_still_verifies_entry_mask_restore(self):
        class BrokenTranscript(io.StringIO):
            writes = 0

            def write(self, text):
                self.writes += 1
                if self.writes > 2:
                    raise OSError("disk full during mask write")
                return super().write(text)

        runner = FixtureRunner()
        stream = BrokenTranscript()
        args = hil.parse_args(["--transcript", "transcript.txt", "--sweep-masks"])
        step = next(step for step in hil.build_plan(args) if step.command == "<host all-256 mask sweep>")
        with patch.object(hil.Path, "open", return_value=stream), \
                patch.object(hil, "build_plan", return_value=[step]), \
                patch.object(hil, "SerialRunner", return_value=runner):
            results, _ = hil.run_live(args)
        self.assertEqual(results[0].status, hil.FAIL)
        self.assertIn("transcript write failed", results[0].notes)
        self.assertEqual(runner.commands, ["read", "mask 0", "mask 18", "read"])
        self.assertEqual(runner.mask, 0x12)

    def test_transcript_open_failure_prevents_serial_session(self):
        args = hil.parse_args(["--transcript", "transcript.txt"])
        with patch.object(hil.Path, "open", side_effect=OSError("cannot create transcript")), \
                patch.object(hil, "SerialRunner") as serial_runner:
            results, transcript = hil.run_live(args)
        serial_runner.assert_not_called()
        self.assertIsNone(transcript)
        self.assertTrue(all(result.status == hil.NOT_RUN for result in results))
        self.assertIn("transcript unavailable", results[0].notes)

    def test_soak_rejects_nonempty_garbage_and_cleans_up(self):
        clock = Clock()
        runner = FixtureRunner(clock=clock)
        runner.responses["health"] = "unrelated output\n> "
        args = hil.parse_args(["--soak-duration-s", "1", "--command-delay-s", "0"])
        with patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep):
            text, _ = hil.run_soak(runner, args, lambda _: None)
        self.assertIn("failures=1", text)
        self.assertEqual(runner.commands, ["console", "verbose", "stress_mix 8", "read", "health", "off", "read", "console"])
        self.assertEqual(runner.mask, 0)

    def test_soak_streams_and_stops_at_count_cap(self):
        clock = Clock()
        runner = FixtureRunner(clock=clock)
        args = hil.parse_args(["--soak-duration-s", "100", "--command-delay-s", "0"])
        records = []
        with patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep), \
                patch.object(hil, "MAX_SOAK_BATCHES", 2):
            text, _ = hil.run_soak(runner, args, records.append)
        self.assertEqual(len(records), 13)
        self.assertIn("batch cap", text)
        self.assertIn("failures=1", text)
        self.assertNotIn("$ read", text)

    def test_soak_success_and_failed_cleanup_are_distinct(self):
        for fail_cleanup in (False, True):
            clock = Clock()
            runner = FixtureRunner(clock=clock)
            if fail_cleanup:
                runner.responses["off"] = "off: I2C_TIMEOUT\n> "
            args = hil.parse_args(["--soak-duration-s", "1", "--command-delay-s", "0"])
            with patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep):
                text, elapsed = hil.run_soak(runner, args, lambda _: None)
            self.assertIn(f"failures={int(fail_cleanup)}", text)
            self.assertGreaterEqual(elapsed, 1)
            self.assertLess(elapsed, 1.7)

    def test_soak_interrupt_attempts_verified_cleanup(self):
        clock = Clock()
        runner = FixtureRunner(clock=clock)
        runner.interrupt_at = "health"
        args = hil.parse_args(["--soak-duration-s", "1", "--command-delay-s", "0"])
        with patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep):
            text, _ = hil.run_soak(runner, args, lambda _: None)
        self.assertIn("operator interrupted", text)
        self.assertIn("failures=1", text)
        self.assertEqual(runner.commands[-3:], ["off", "read", "console"])
        self.assertEqual(runner.mask, 0)

    def test_soak_uses_complete_all_channel_batches_and_restores_verbose(self):
        clock = Clock()
        runner = FixtureRunner()
        runner.verbose = True
        batch_starts = []
        original = runner.command

        def command(text):
            if text.startswith("stress_mix"):
                batch_starts.append(clock.now)
            return original(text)

        runner.command = command
        args = hil.parse_args(["--soak-duration-s", "3", "--command-delay-s", "0"])
        with patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep):
            text, elapsed = hil.run_soak(runner, args, lambda _: None)
        self.assertIn("batches=3 cycles=24 failures=0", text)
        self.assertEqual(batch_starts, [0, 1, 2])
        self.assertEqual(elapsed, 3)
        self.assertTrue(runner.verbose)
        self.assertEqual(runner.commands[:3], ["console", "verbose", "verbose off"])
        self.assertEqual(runner.commands[-4:], ["off", "read", "verbose on", "console"])

    def test_soak_cannot_pass_without_a_complete_batch_or_current_transfer_evidence(self):
        runner = FixtureRunner()
        args = hil.parse_args(["--soak-duration-s", "0.001"])
        with patch.object(hil.time, "monotonic", side_effect=range(1000)):
            text, _ = hil.run_soak(runner, args, lambda _: None)
        self.assertIn("without a complete verified batch", text)
        self.assertIn("batches=0 cycles=0 failures=1", text)
        self.assertFalse(any(command.startswith(("off", "stress", "mask")) for command in runner.commands))

        clock = Clock()
        runner = FixtureRunner(clock=clock)
        runner.responses["stress_mix 8"] = stress_text(8).replace("Session transfers: success=58 failure=0\n", "")
        with patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep):
            text, _ = hil.run_soak(runner, args, lambda _: None)
        self.assertIn("per-session transfer evidence", text)
        self.assertIn("batches=0 cycles=0 failures=1", text)
        self.assertEqual(runner.commands[-3:], ["off", "read", "console"])

    def test_soak_console_loss_or_restart_cannot_hide_behind_successful_stress(self):
        for fault in ("drop", "reset", "baseline"):
            with self.subTest(fault=fault):
                clock = Clock()
                runner = FixtureRunner(clock=clock)
                original = runner.command
                calls = 0

                def command(text):
                    nonlocal calls
                    if text == "console":
                        calls += 1
                        if (fault == "drop" and calls > 1) or fault == "baseline":
                            return hil.Response(console_text(dropped=1 if fault == "drop" else 0xFFFFFFFF), 0, "prompt")
                    return original(text)

                runner.command = command
                if fault == "reset":
                    runner.responses["stress_mix 8"] = "rst:0x1 (POWERON_RESET)\n" + stress_text(8)
                args = hil.parse_args(["--soak-duration-s", "1", "--command-delay-s", "0"])
                with patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep):
                    text, _ = hil.run_soak(runner, args, lambda _: None)
                self.assertIn("[FAIL]", text)
                self.assertNotIn("failures=0", text)
                if fault == "reset":
                    self.assertEqual(runner.commands, ["console", "verbose", "stress_mix 8"])
                    self.assertFalse(runner.synchronized)
                    self.assertIn("cleanup NOT_RUN", text)
                elif fault == "baseline":
                    self.assertEqual(runner.commands, [])

    def test_live_console_end_snapshot_rejects_new_output_loss(self):
        runner = FixtureRunner()
        baseline = console_text()
        original = runner.command
        calls = 0

        def command(text):
            nonlocal calls
            if text == "console":
                calls += 1
                return hil.Response(baseline if calls == 1 else console_text(dropped=1), 0, "prompt")
            return original(text)

        runner.command = command
        args = hil.parse_args(["--command-delay-s", "0"])
        plan = [step for step in hil.build_plan(args) if step.command == "console"]
        with patch.object(hil, "build_plan", return_value=plan), patch.object(hil, "SerialRunner", return_value=runner):
            results, _ = hil.run_live(args)
        self.assertEqual([result.status for result in results], [hil.PASS, hil.FAIL])
        self.assertIn("dropped_bytes changed", results[-1].notes)

    def test_console_counter_regression_is_detected_between_successive_snapshots(self):
        for mode in ("soak", "live"):
            with self.subTest(mode=mode):
                clock = Clock()
                runner = FixtureRunner(clock=clock)
                original = runner.command
                accepted = iter((100, 500, 200))

                def command(text):
                    if text == "console":
                        return hil.Response(console_text(accepted=next(accepted, 200)), 0, "prompt")
                    return original(text)

                runner.command = command
                args = hil.parse_args(["--soak-duration-s", "2", "--command-delay-s", "0"])
                with patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep):
                    if mode == "soak":
                        text, _ = hil.run_soak(runner, args, lambda _: None)
                        self.assertIn("counter moved backwards", text)
                        self.assertIn("[FAIL]", text)
                        self.assertIn("batches=1 cycles=8", text)
                    else:
                        steps = [step for step in hil.build_plan(args) if step.command == "console"]
                        with patch.object(hil, "build_plan", return_value=[steps[0], steps[0], steps[1]]), \
                                patch.object(hil, "SerialRunner", return_value=runner):
                            results, _ = hil.run_live(args)
                        self.assertEqual([result.status for result in results], [hil.PASS, hil.PASS, hil.FAIL])
                        self.assertIn("counter moved backwards", results[-1].notes)

    def test_live_discovery_requires_configured_mux_address_before_later_tests(self):
        runner = FixtureRunner()
        runner.responses["discover"] = discover_text({0x72})
        args = hil.parse_args(["--command-delay-s", "0"])
        plan = [step for step in hil.build_plan(args) if step.command in ("discover", "scan")]
        with patch.object(hil, "build_plan", return_value=plan), patch.object(hil, "SerialRunner", return_value=runner):
            results, _ = hil.run_live(args)
        self.assertEqual([result.status for result in results], [hil.FAIL, hil.NOT_RUN])
        self.assertEqual(runner.commands, ["discover"])

    def test_live_keyboard_interrupt_records_unknown_and_rest_not_run(self):
        runner = FixtureRunner()
        runner.interrupt_at = "version"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "interrupted.txt"
            args = hil.parse_args(["--transcript", str(path)])
            with patch.object(hil, "SerialRunner", return_value=runner):
                results, _ = hil.run_live(args)
            self.assertEqual(results[0].status, hil.UNKNOWN)
            self.assertTrue(all(result.status == hil.NOT_RUN for result in results[1:]))
            self.assertIn("operator interrupted", path.read_text(encoding="utf-8"))

    def test_live_address_mismatch_fails_before_hardware_mutations(self):
        runner = FixtureRunner()
        runner.responses.update({"version": "=== Version Info ===\nLibrary: test\n> ",
                                 "help": "=== TCA9548A CLI Help ===\nhil [dry|parser|run|run reset]\n> ",
                                 "cfg": cfg_text(address=0x71)})
        args = hil.parse_args(["--command-delay-s", "0"])
        with patch.object(hil, "SerialRunner", return_value=runner):
            results, _ = hil.run_live(args)
        self.assertEqual(results[2].status, hil.FAIL)
        self.assertEqual(runner.commands, ["version", "help", "cfg"])

    def test_live_invalid_timeout_stops_before_hardware_mutations(self):
        for timeout_field in ("", "I2C timeout: 0 ms\n",
                              "I2C timeout: 20 ms\nI2C timeout: 20 ms\n"):
            runner = FixtureRunner()
            runner.responses.update({"version": "=== Version Info ===\nLibrary: test\n> ",
                                     "help": "=== TCA9548A CLI Help ===\nhil [dry|parser|run|run reset]\n> ",
                                     "cfg": "=== Configuration ===\nI2C address: 0x70\n" + CFG_FIELDS + timeout_field + "> "})
            args = hil.parse_args(["--command-delay-s", "0"])
            with patch.object(hil, "SerialRunner", return_value=runner):
                results, _ = hil.run_live(args)
            self.assertEqual(results[2].status, hil.FAIL)
            self.assertTrue(all(result.status == hil.NOT_RUN for result in results[3:]))
            self.assertEqual(runner.commands, ["version", "help", "cfg"])

    def test_dry_report_does_not_claim_live_hardware_or_firmware_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.md"
            args = hil.parse_args(["--dry-run", "--report", str(path)])
            with patch.object(hil, "git_text", return_value="host-sha"), patch.object(hil, "tool_text", return_value="host-tool"):
                hil.write_report(path, args, hil.dry_run(args), None)
            report = path.read_text(encoding="utf-8")
            self.assertIn("not hardware evidence", report)
            self.assertIn("serial port was not opened", report)
            self.assertIn("No live timing samples", report)
            self.assertIn("not established by this run", report)

    def test_report_does_not_claim_unexecuted_route_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.md"
            args = hil.parse_args(["--route", "0:0x48"])
            plan = hil.build_plan(args)
            results = [hil.Result(plan[0], hil.FAIL, "version failed")]
            hil.append_not_run_results(results, plan, "stopped before routing")
            with patch.object(hil, "git_text", return_value="host"), patch.object(hil, "tool_text", return_value="tool"):
                hil.write_report(path, args, results, None)
            report = path.read_text(encoding="utf-8")
            self.assertIn("Declared routing fixture result: NOT_RUN", report)
            self.assertNotIn("fixtures were checked", report)

    def test_live_unavailable_and_not_run_exit_semantics(self):
        args = hil.parse_args([])
        with patch.object(hil, "SerialRunner", side_effect=RuntimeError("no serial")):
            results, _ = hil.run_live(args)
        self.assertEqual(len(results), len(hil.build_plan(args)))
        self.assertTrue(all(result.status == hil.NOT_RUN for result in results))
        self.assertEqual(hil.result_exit_code(results, dry_run=False, allow_not_run=False), 1)
        self.assertEqual(hil.result_exit_code(results, dry_run=False, allow_not_run=True), 0)
        results[0].status = hil.UNKNOWN
        self.assertEqual(hil.result_exit_code(results, dry_run=False, allow_not_run=True), 1)


if __name__ == "__main__":
    unittest.main()
