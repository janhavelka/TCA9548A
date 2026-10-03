#!/usr/bin/env python3
"""Host-only HIL framing, evidence, fixture and interruption regressions."""

from __future__ import annotations

import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

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

    def test_cfg_reports_valid_owner_timeout_without_hard_coding_default(self):
        for timeout_ms in (1, 20, 50, 60000):
            text = ("=== Configuration ===\nI2C address: 0x70\n"
                    f"  I2C timeout: {timeout_ms} ms\n> ")
            status, notes = hil.classify(text, self.plan["TCA-HIL-003"])
            self.assertEqual(status, hil.PASS)
            self.assertIn(f"firmware I2C timeout={timeout_ms} ms (reported setting)", notes)

    def test_cfg_rejects_missing_and_duplicate_timeout_fields(self):
        base = "=== Configuration ===\nI2C address: 0x70\n"
        for fields in ("", "I2C timeout: 20 ms\nI2C timeout: 20 ms\n",
                       "I2C timeout: 20 ms\nI2C timeout: invalid\n"):
            with self.subTest(fields=fields):
                self.classify("cfg", base + fields + "> ", hil.FAIL)

    def test_cfg_rejects_malformed_zero_and_excessive_timeouts(self):
        base = "=== Configuration ===\nI2C address: 0x70\n"
        for value in ("0 ms", "60001 ms", "-1 ms", "nan ms", "inf ms", "20.0 ms",
                      "20 us", "20", "20 ms trailing", "9" * 5000 + " ms"):
            with self.subTest(value=value[:40]):
                self.classify("cfg", base + "  I2C timeout: " + value + "\n> ", hil.FAIL)

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


class SerialTests(unittest.TestCase):
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


class FixtureTests(unittest.TestCase):
    def route_args(self):
        return hil.parse_args(["--route", "0:0x48", "--route", "7:0x48", "--route", "4:0x49"])

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
    def test_complete_live_plan_including_optional_host_checks(self):
        clock = Clock()
        runner = FixtureRunner(clock=clock)
        runner.responses.update({"version": "=== Version Info ===\nLibrary: test\n> ",
                                 "help": "=== TCA9548A CLI Help ===\nhil [dry|parser|run|run reset]\n> ",
                                 "cfg": "=== Configuration ===\nI2C address: 0x70\nI2C timeout: 20 ms\n> ",
                                 "hil dry": hil_text(dry=True), "hil run reset": hil_text(),
                                 "stress 16": "Stress results: completed=16 requested=16 status=OK safe_off=OK\nDuration: 3 ms\nHealth delta: success=18 failure=0\n> ",
                                 "stress_mix 16": "=== stress_mix summary ===\nStress results: completed=16 requested=16 status=OK safe_off=OK\nDuration: 3 ms\nHealth delta: success=18 failure=0\n> "})
        args = hil.parse_args(["--route", "0:0x48", "--route", "7:0x48", "--route", "4:0x49",
                               "--sweep-masks", "--sample-count", "16", "--stress-count", "16",
                               "--soak-duration-s", "1", "--command-delay-s", "0"])
        with patch.object(hil, "SerialRunner", return_value=runner), \
                patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep):
            results, _ = hil.run_live(args)
        self.assertEqual(len(results), 14)
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
                 ("--route", "0:0x78"), ("--route", "0:0x70"), ("--route", "bad")]
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
        self.assertEqual(plan[-1].command, "health")
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

    def test_soak_rejects_nonempty_garbage_and_cleans_up(self):
        clock = Clock()
        runner = FixtureRunner(clock=clock)
        runner.responses["health"] = "unrelated output\n> "
        args = hil.parse_args(["--soak-duration-s", "1", "--command-delay-s", "0"])
        with patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep):
            text, _ = hil.run_soak(runner, args, lambda _: None)
        self.assertIn("failures=1", text)
        self.assertEqual(runner.commands, ["read", "health", "off", "read"])
        self.assertEqual(runner.mask, 0)

    def test_soak_streams_and_stops_at_count_cap(self):
        clock = Clock()
        runner = FixtureRunner(clock=clock)
        args = hil.parse_args(["--soak-duration-s", "100", "--command-delay-s", "0"])
        records = []
        with patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep), \
                patch.object(hil, "MAX_SOAK_COMMANDS", 8):
            text, _ = hil.run_soak(runner, args, records.append)
        self.assertEqual(len(records), 10)
        self.assertIn("command cap", text)
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
            self.assertLess(elapsed, 1.4)

    def test_soak_interrupt_attempts_verified_cleanup(self):
        clock = Clock()
        runner = FixtureRunner(clock=clock)
        runner.interrupt_at = "probe"
        args = hil.parse_args(["--soak-duration-s", "1", "--command-delay-s", "0"])
        with patch.object(hil.time, "monotonic", clock.monotonic), patch.object(hil.time, "sleep", clock.sleep):
            text, _ = hil.run_soak(runner, args, lambda _: None)
        self.assertIn("operator interrupted", text)
        self.assertIn("failures=1", text)
        self.assertEqual(runner.commands[-2:], ["off", "read"])
        self.assertEqual(runner.mask, 0)

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
                                 "cfg": "=== Configuration ===\nI2C address: 0x71\nI2C timeout: 20 ms\n> "})
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
                                     "cfg": "=== Configuration ===\nI2C address: 0x70\n" + timeout_field + "> "})
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
