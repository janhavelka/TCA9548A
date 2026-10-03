#!/usr/bin/env python3
"""Bounded host-side HIL runner for the Arduino and native ESP-IDF CLIs.

The live mode talks to either bring-up CLI over a serial port.
Parser self-test and dry-run modes never open the serial port.
"""

from __future__ import annotations

import argparse
import contextlib
import dataclasses
import datetime as dt
import math
import platform
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable, Iterable


PASS = "PASS"
FAIL = "FAIL"
UNKNOWN = "UNKNOWN"
NOT_RUN = "NOT_RUN"
ROOT = Path(__file__).resolve().parents[1]
MAX_RESPONSE_BYTES = 65536
MAX_SOAK_COMMANDS = 100000
PROMPT_RE = re.compile(r"(?:^|[\r\n])> $")
ANY_PROMPT_RE = re.compile(r"(?:^|[\r\n])> ")

ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
COMMON_FAILURE_PATTERNS = (
    r"\[FAIL\]",
    r"\bfail=[1-9][0-9]*\b",
    r"\bfailures?=[1-9][0-9]*\b",
    # Printed by both CLIs when the post-stress all-off could not be verified.
    r"\bsafe_off=FAILED\b",
    r"\berrors=[1-9][0-9]*\b",
    r"\[E\]",
    r"\bCommand discarded:",
    r"\b(?:Guru Meditation|PANIC|Rebooting|Traceback)\b",
)
STATUS_FAILURE_PATTERNS = (
    r"\bI2C_(?:ERROR|TIMEOUT|BUS|NACK_ADDR|NACK_DATA)\b",
    r"\bDEVICE_NOT_FOUND\b",
    r"\bNOT_INITIALIZED\b",
    r"\bINVALID_(?:CONFIG|PARAM)\b",
    r"\bRESET_(?:ERROR|STATE_MISMATCH)\b",
)


@dataclasses.dataclass(frozen=True)
class Step:
    test_id: str
    area: str
    command: str
    expected: str
    expected_tokens: tuple[str, ...]
    failure_patterns: tuple[str, ...] = COMMON_FAILURE_PATTERNS
    live_required: bool = True
    expected_mask: int | None = None


@dataclasses.dataclass
class Result:
    step: Step
    status: str
    observed: str
    elapsed_s: float = 0.0
    notes: str = ""


@dataclasses.dataclass(frozen=True)
class Response:
    text: str
    elapsed_s: float
    completion: str


class SessionLost(RuntimeError):
    """A missing prompt makes subsequent command attribution unsafe."""


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def short_observed(text: str, limit: int = 120) -> str:
    clean = " ".join(strip_ansi(text).split())
    if not clean:
        return "(no output)"
    if len(clean) <= limit:
        return clean
    return clean[: limit - 3] + "..."


def contains_all(text: str, tokens: Iterable[str]) -> bool:
    clean = strip_ansi(text)
    return all(token in clean for token in tokens)


def first_failure(text: str, patterns: Iterable[str]) -> str | None:
    clean = strip_ansi(text)
    for pattern in patterns:
        if re.search(pattern, clean):
            return pattern
    return None


def classify(text: str, step: Step, completion: str = "prompt") -> tuple[str, str]:
    if completion != "prompt":
        return UNKNOWN, f"incomplete command: {completion}"
    failure = first_failure(text, step.failure_patterns)
    if failure is not None:
        return FAIL, f"failure pattern matched: {failure}"
    clean = strip_ansi(text)
    if not contains_all(clean, step.expected_tokens):
        if clean.strip():
            return UNKNOWN, "expected tokens missing"
        return UNKNOWN, "no response before timeout"

    command = step.command
    if command == "cfg":
        timeout_fields = re.findall(
            r"^[ \t]*I2C timeout:[ \t]*([^\r\n]*)", clean, re.M
        )
        if len(timeout_fields) != 1:
            return FAIL, "firmware I2C timeout missing or duplicated"
        timeout_match = re.fullmatch(r"([0-9]{1,5})[ \t]+ms[ \t]*", timeout_fields[0])
        if timeout_match is None or not 1 <= int(timeout_match[1]) <= 60000:
            return FAIL, "firmware I2C timeout must be an integer within 1..60000 ms"
        timeout_ms = int(timeout_match[1])
        return PASS, f"complete configuration; firmware I2C timeout={timeout_ms} ms (reported setting)"
    if command.startswith("hil "):
        summaries = re.findall(r"^HIL result: pass=(\d+) fail=(\d+) skip=(\d+)\s*$", clean, re.M)
        checks = re.findall(r"^\s*\[(PASS|FAIL|SKIP)\] ([^\r\n]+)", clean, re.M)
        if len(summaries) != 1 or not checks:
            return UNKNOWN, "missing or ambiguous HIL summary/check records"
        passed, failed, skipped = map(int, summaries[0])
        if (passed, failed, skipped) != tuple(sum(kind == name for kind, _ in checks) for name in (PASS, FAIL, "SKIP")):
            return FAIL, "HIL summary disagrees with check records"
        labels = {label.split(" - ", 1)[0] for kind, label in checks if kind == PASS}
        required = {"typed ChannelMask is one byte", "address helper", "version"}
        if command.startswith("hil run"):
            required.update(("probe", "probe no-health-side-effects", "capture original mask readback",
                             "disableAll write", "disableAll readback", "all eight one-hot channels",
                             "write mask 0xA5", "read mask 0xA5", "recover safe-off write",
                             "recover readback 0x00", "final verified mask restore"))
            if command == "hil run reset":
                required.update(("hardReset seed write", "hardReset seed readback",
                                 "hardReset exact-zero verification", "hardReset leaves verified all-off"))
                if skipped:
                    return FAIL, "required live RESET or other check skipped"
            elif skipped != 1 or not any(kind == "SKIP" and label.startswith("hardReset - ") for kind, label in checks):
                return FAIL, "unexpected skipped live check"
        elif skipped != 3:
            return FAIL, "unexpected dry-run skip count"
        if failed or not required.issubset(labels):
            return FAIL, "required HIL checks failed or missing"
    elif command == "health":
        states = re.findall(r"^\s*State: (\w+)\b", clean, re.M)
        failures = re.findall(r"^\s*Consecutive failures: (\d+)\s*$", clean, re.M)
        if states != ["READY"] or failures != ["0"]:
            return FAIL, "driver is not healthy READY"
        if not all(re.findall(rf"^\s*{field}: (\w+)\s*$", clean, re.M) == ["yes"]
                   for field in ("Bound", "Initialized", "State alias parity")):
            return FAIL, "driver binding/state aliases invalid"
    elif command == "scan":
        summaries = re.findall(r"^Scan complete: devices=(\d+) errors=(\d+)\s*$", clean, re.M)
        addresses = [int(value, 16) for value in re.findall(r"^\s*Found device at 0x([0-9A-Fa-f]{2})\s*$", clean, re.M)]
        if len(summaries) != 1:
            return UNKNOWN, "missing or ambiguous scan completion"
        devices, errors = map(int, summaries[0])
        if (errors or devices != len(addresses) or len(addresses) != len(set(addresses))
                or any(not 0x08 <= address <= 0x77 for address in addresses)):
            return FAIL, "scan errors or inconsistent device count"
        if not re.search(r"^Scan topology: OK\b.*active_mask=0x[0-9A-Fa-f]{2}\b", clean, re.M):
            return FAIL, "scan topology read failed"
    elif command.startswith("stress"):
        summaries = re.findall(r"^Stress results: completed=(\d+) requested=(\d+) status=(\w+) safe_off=(\w+)\s*$", clean, re.M)
        if len(summaries) != 1:
            return UNKNOWN, "missing or ambiguous stress completion"
        completed, requested, status, safe_off = summaries[0]
        count = int(command.split()[1])
        if int(completed) != count or int(requested) != count or status != "OK" or safe_off != "OK":
            return FAIL, "stress incomplete or safe-off unverified"
        if not re.search(r"^Health delta: success=\d+ failure=0\s*$", clean, re.M):
            return FAIL, "stress health failures or missing counters"
    elif command == "read":
        matches = re.findall(r"^read: OK\b[^\r\n]* mask=0x([0-9A-Fa-f]{2})\b", clean, re.M)
        if len(matches) != 1:
            return UNKNOWN, "missing or ambiguous control-byte read"
        if step.expected_mask is not None and int(matches[0], 16) != step.expected_mask:
            return FAIL, f"mask readback mismatch; expected 0x{step.expected_mask:02X}"
    elif command in ("probe", "off", "recover") or command.startswith(("mask ", "select ")):
        prefix = {"recover": "recover (safe-off write)"}.get(command, command)
        if command.startswith("mask "):
            prefix = f"mask 0x{int(command.split()[1], 0):02X}"
        if not re.search(rf"^{re.escape(prefix)}: OK(?:\s|$)", clean, re.M):
            return FAIL, "operation did not report explicit OK"
    return PASS, "complete response and required checks verified"


def build_plan(args: argparse.Namespace) -> list[Step]:
    status_patterns = COMMON_FAILURE_PATTERNS + STATUS_FAILURE_PATTERNS
    plan = [
        Step(
            "TCA-HIL-001",
            "connectivity",
            "version",
            "Firmware/library version is printed.",
            ("=== Version Info ===", "Library:"),
        ),
        Step(
            "TCA-HIL-002",
            "cli",
            "help",
            "CLI help lists safe HIL commands.",
            ("=== TCA9548A CLI Help ===", "hil [dry|parser|run|run reset]"),
        ),
        Step(
            "TCA-HIL-003",
            "diagnostics",
            "cfg",
            "Configuration snapshot includes a valid bounded I2C timeout.",
            ("=== Configuration ===", "I2C address:"),
        ),
        Step(
            "TCA-HIL-004",
            "health",
            "health",
            "Driver health snapshot is printed.",
            ("=== Driver Health ===", "State:"),
        ),
        Step(
            "TCA-HIL-005",
            "bus",
            "scan",
            "All 112 bounded probes of the reported active topology complete without bus errors.",
            ("Scan topology:", "Scanning I2C bus", "Scan complete: devices="),
        ),
        Step(
            "TCA-HIL-006",
            "probe",
            "probe",
            "Probe reports target status without health side effects.",
            ("probe:",),
            status_patterns,
        ),
        Step(
            "TCA-HIL-007",
            "contract",
            "hil dry",
            "Device-side dry HIL contract checks run.",
            ("=== TCA9548A HIL DRY-RUN ===", "HIL result:"),
            status_patterns,
        ),
        Step(
            "TCA-HIL-008",
            "contract",
            "hil run reset" if args.include_reset else "hil run",
            "Live HIL checks run and restore the verified entry mask.",
            (
                "=== TCA9548A HIL RUN ===",
                "all eight one-hot channels",
                "final verified mask restore",
                "HIL result:",
            ),
            status_patterns,
        ),
    ]

    if args.route:
        plan.append(Step("TCA-HIL-012", "routing", "<host routing isolation checks>",
                         "Declared downstream addresses appear only on their selected branches; entry mask restored.",
                         ("routing complete",), status_patterns))
    if args.sweep_masks:
        plan.append(Step("TCA-HIL-013", "masks", "<host all-256 mask sweep>",
                         "Every control byte reads back exactly; entry mask restored.",
                         ("mask sweep complete",), status_patterns))

    if args.sample_count > 0:
        plan.append(
            Step(
                "TCA-HIL-009",
                "timing",
                f"stress {args.sample_count}",
                "Bounded channel-select rate sample completes.",
                ("Stress results:", "Duration:"),
                status_patterns,
            )
        )

    if args.stress_count > 0:
        plan.append(
            Step(
                "TCA-HIL-010",
                "stress",
                f"stress_mix {args.stress_count}",
                "Bounded mixed-operation stress run completes.",
                ("=== stress_mix summary ===", "Health delta:"),
                status_patterns,
            )
        )

    if args.soak_duration_s > 0:
        plan.append(
            Step(
                "TCA-HIL-011",
                "soak",
                "<host bounded soak loop>",
                "Host repeats safe commands until the bounded duration expires.",
                ("soak complete",),
                status_patterns,
            )
        )

    plan.append(Step("TCA-HIL-014", "health", "health",
                     "Final driver state is READY with no consecutive failures.",
                     ("=== Driver Health ===", "State:")))
    return plan


def git_text(args: list[str], default: str, *, allow_empty: bool = False) -> str:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.TimeoutExpired):
        return default
    if result.returncode != 0:
        return default
    text = result.stdout.strip()
    return text if text or allow_empty else default


def platformio_tool_args(*args: str) -> list[str]:
    """Return the repository-approved PlatformIO invocation for this host."""
    if platform.system() == "Windows":
        wrapper = ROOT / "scripts" / "pio.cmd"
        return ["cmd.exe", "/d", "/c", str(wrapper), *args]
    return [sys.executable, "-m", "platformio", *args]


def tool_text(args: list[str], default: str) -> str:
    try:
        result = subprocess.run(
            args,
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired):
        return default
    text = (result.stdout or result.stderr).strip()
    return text.splitlines()[0] if result.returncode == 0 and text else default


def markdown_escape(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", "<br>")


def status_counts(results: Iterable[Result]) -> dict[str, int]:
    counts = {PASS: 0, FAIL: 0, UNKNOWN: 0, NOT_RUN: 0}
    for result in results:
        counts[result.status] = counts.get(result.status, 0) + 1
    return counts


def result_exit_code(
    results: Iterable[Result], *, dry_run: bool, allow_not_run: bool
) -> int:
    result_list = list(results)
    counts = status_counts(result_list)
    if counts[FAIL] or counts[UNKNOWN]:
        return 1

    required_not_run = any(
        result.status == NOT_RUN and result.step.live_required
        for result in result_list
    )
    if required_not_run and not dry_run and not allow_not_run:
        return 1
    return 0


def write_report(
    path: Path,
    args: argparse.Namespace,
    results: list[Result],
    transcript_path: Path | None,
) -> None:
    now = dt.datetime.now().astimezone()
    dirty = git_text(["status", "--short"], "unknown", allow_empty=True)
    if dirty == "unknown":
        dirty_summary = "unknown"
    elif dirty == "":
        dirty_summary = "clean"
    else:
        dirty_summary = "dirty before report generation or local edits present"

    counts = status_counts(results)
    command_line = subprocess.list2cmdline([sys.executable, *sys.argv])
    platformio_version = tool_text(
        platformio_tool_args("--version"), "not checked"
    )
    live_mode = not args.dry_run and not args.parser_self_test
    port_note = args.port if live_mode else f"{args.port} (not opened in dry-run mode)"
    executed_results = [result for result in results if result.status != NOT_RUN]
    session_executed = live_mode and bool(executed_results)
    probe_result = next(
        (result for result in results if result.step.command == "probe"), None
    )
    soak_result = next(
        (
            result
            for result in results
            if result.step.command == "<host bounded soak loop>"
        ),
        None,
    )

    if session_executed and probe_result is not None:
        device_note = (
            "exact chip identity cannot be proven by the control-byte read; "
            f"configured-address probe result was {probe_result.status}"
        )
    else:
        device_note = "not established by this run"

    if args.dry_run:
        hardware_lines = [
            f"- Fixture: {args.fixture_note}",
            "- Mode: dry-run; the serial port was not opened.",
            "- Wiring and electrical behavior were not tested.",
        ]
    elif session_executed:
        hardware_lines = [
            f"- Fixture note supplied by operator: {args.fixture_note}",
            "- Mode: live serial commands executed.",
            "- Wiring and electrical limits were not independently instrumented "
            "by this runner.",
        ]
    else:
        hardware_lines = [
            f"- Fixture note supplied by operator: {args.fixture_note}",
            "- Mode: live serial session unavailable; no live step executed.",
            "- Wiring and electrical behavior were not tested.",
        ]

    if transcript_path is not None:
        transcript_note = f"- Raw serial transcript: `{transcript_path}`"
    elif session_executed:
        transcript_note = (
            "- Raw serial transcript: not persisted; bounded excerpts are "
            "recorded in the detailed result rows."
        )
    else:
        transcript_note = "- Raw serial transcript: not captured."

    if executed_results:
        worst_elapsed = max(result.elapsed_s for result in executed_results)
        sampling_lines = [
            f"- Live steps executed: `{len(executed_results)}`.",
            f"- Largest recorded step elapsed time: `{worst_elapsed:.3f}` seconds.",
            "- Per-step elapsed values and observations are recorded above.",
        ]
    else:
        sampling_lines = ["- No live timing samples were collected."]

    soak_lines = [
        f"- Requested soak duration: `{args.soak_duration_s}` seconds."
    ]
    if soak_result is None:
        soak_lines.append("- Soak: not requested.")
    elif soak_result.status == NOT_RUN:
        soak_lines.extend(
            [
                "- Soak: not executed.",
                f"- Reason: {soak_result.notes}",
            ]
        )
    else:
        soak_lines.extend(
            [
                f"- Soak result: `{soak_result.status}`.",
                f"- Recorded command time: `{soak_result.elapsed_s:.3f}` seconds.",
                f"- Runner summary: {soak_result.observed}",
            ]
        )

    if args.dry_run:
        limitation_lines = [
            f"- {args.not_run_reason}",
            "- Dry-run validates only the plan; it is not hardware evidence.",
        ]
    elif not session_executed:
        unavailable_note = results[0].notes if results else args.not_run_reason
        limitation_lines = [
            f"- No live step executed: {unavailable_note}",
            "- Firmware upload, boot behavior, routing, RESET, and electrical "
            "behavior are not evidenced by this report.",
        ]
    else:
        not_run_commands = [
            result.step.command for result in results if result.status == NOT_RUN
        ]
        limitation_lines = [
            "- Firmware upload and fixture wiring are outside this runner and "
            "must be evidenced separately.",
            "- Repository commit/dirty metadata describes the host checkout; "
            "it does not prove the connected firmware was built from that commit.",
            "- A responding control byte cannot prove exact chip identity.",
            "- Control-byte readback alone does not prove downstream routing. "
            + ("Declared --route fixtures were checked; undeclared endpoints remain untested."
               if args.route else "No --route fixture was supplied; downstream routing is untested."),
            "- RESET pulse width, STOP timing, voltage translation, rise times, "
            "power-on reset, and stuck-bus recovery require separate fixture/instrument evidence.",
        ]
        if not_run_commands:
            limitation_lines.append(
                "- Live steps not run: " + ", ".join(not_run_commands) + "."
            )
        if not args.include_reset:
            limitation_lines.append(
                "- RESET validation was explicitly omitted with --skip-reset; "
                "this run is not release HIL evidence."
            )

    lines = [
        f"# TCA9548A HIL Validation Report - {args.port} - {now:%Y-%m-%d}",
        "",
        "## Metadata",
        "",
        f"- Date/time: `{now.isoformat(timespec='seconds')}`",
        f"- Timezone: `{now.tzname() or 'local'}`",
        f"- Repository path: `{ROOT}`",
        f"- Branch: `{git_text(['branch', '--show-current'], 'unknown')}`",
        f"- Host checkout commit (not verified firmware identity): `{git_text(['rev-parse', 'HEAD'], 'unknown')}`",
        f"- Dirty status: `{dirty_summary}`",
        f"- Operating system: `{platform.platform()}`",
        f"- Python: `{platform.python_version()}`",
        f"- PlatformIO: `{platformio_version}`",
        f"- Target environment: `{args.target_env}`",
        f"- Serial port: `{port_note}`",
        f"- Baud rate: `{args.baud}`",
        f"- Expected mux address: `0x{args.address:02X}`",
        f"- Routing fixtures (channel, address): `{args.route}`",
        f"- Exhaustive mask sweep requested: `{args.sweep_masks}`",
        "- Host soak duration is bounded by the requested interval plus three "
        "command timeouts and one command delay (including verified all-off cleanup); "
        "command count is capped at 100000.",
        f"- Device identity/address: {device_note}.",
        "",
        "## Hardware Setup",
        "",
        *hardware_lines,
        "",
        "## Reference And Generation Commands",
        "",
        "```powershell",
        "python tools\\tca9548a_hil.py --parser-self-test",
        (
            "python tools\\tca9548a_hil.py --dry-run --port "
            f"{args.port} --baud {args.baud}"
        ),
        f".\\scripts\\pio.cmd run -e {args.target_env}",
        (
            f".\\scripts\\pio.cmd run -e {args.target_env} -t upload "
            f"--upload-port {args.port}"
        ),
        (
            "python tools\\tca9548a_hil.py --port "
            f"{args.port} --baud {args.baud} --timeout-s {args.timeout_s}"
        ),
        "```",
        "",
        f"Report generation command: `{command_line}`",
        "",
        "## Summary",
        "",
        "| PASS | FAIL | UNKNOWN | NOT_RUN |",
        "|------|------|---------|---------|",
        f"| {counts[PASS]} | {counts[FAIL]} | {counts[UNKNOWN]} | {counts[NOT_RUN]} |",
        "",
        "## Detailed Results",
        "",
        "| Test ID | Area | Command | Expected | Observed | Elapsed | Result | Notes |",
        "|---------|------|---------|----------|----------|---------|--------|-------|",
    ]

    for result in results:
        lines.append(
            "| {test_id} | {area} | `{command}` | {expected} | {observed} | "
            "{elapsed:.3f}s | `{status}` | {notes} |".format(
                test_id=result.step.test_id,
                area=markdown_escape(result.step.area),
                command=markdown_escape(result.step.command),
                expected=markdown_escape(result.step.expected),
                observed=markdown_escape(result.observed),
                elapsed=result.elapsed_s,
                status=result.status,
                notes=markdown_escape(result.notes),
            )
        )

    lines.extend(
        [
            "",
            "## Transcript",
            "",
            transcript_note,
            "",
            "## Sampling And Timing",
            "",
            *sampling_lines,
            "",
            "## Soak Summary",
            "",
            *soak_lines,
            "",
            "## Limitations And Tests Not Run",
            "",
            *limitation_lines,
            "",
            "## Operator-Supplied Change Notes (Not Executed By Runner)",
            "",
        ]
    )

    if args.fix_note:
        lines.extend(f"- {note}" for note in args.fix_note)
    else:
        lines.append("- None recorded by the runner.")

    lines.extend(
        ["", "## Operator-Supplied Verification Assertions (Not Executed By Runner)", ""]
    )
    if args.verification_result:
        lines.extend(f"- {item}" for item in args.verification_result)
    else:
        lines.append("- Not recorded by the runner.")

    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def dry_run(args: argparse.Namespace) -> list[Result]:
    reason = args.not_run_reason
    return [
        Result(
            step=step,
            status=NOT_RUN,
            observed="not executed",
            notes=reason,
        )
        for step in build_plan(args)
    ]


class SerialRunner:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.serial = None
        self.synchronized = True

    def __enter__(self) -> "SerialRunner":
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise RuntimeError("pyserial is required for live HIL runs") from exc

        self.serial = serial.Serial(
            self.args.port,
            self.args.baud,
            timeout=0.05,
            write_timeout=self.args.timeout_s,
        )
        try:
            time.sleep(self.args.boot_settle_s)
        except BaseException:
            self.serial.close()
            raise
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self.serial is not None:
            self.serial.close()

    def read_until_idle(self) -> str:
        """Drain startup chatter only; idle never completes a command."""
        assert self.serial is not None
        deadline = time.monotonic() + self.args.timeout_s
        idle_deadline = time.monotonic() + self.args.idle_timeout_s
        chunks: list[bytes] = []
        size = 0

        while time.monotonic() < deadline:
            waiting = getattr(self.serial, "in_waiting", 0)
            data = self.serial.read(min(waiting or 1, MAX_RESPONSE_BYTES - size))
            if data:
                chunks.append(data)
                size += len(data)
                if size >= MAX_RESPONSE_BYTES:
                    raise SessionLost("startup output exceeded bounded receive buffer")
                idle_deadline = time.monotonic() + self.args.idle_timeout_s
                continue
            if chunks and time.monotonic() >= idle_deadline:
                break

        return b"".join(chunks).decode("utf-8", errors="replace")

    def command(self, command: str, timeout_s: float | None = None) -> Response:
        assert self.serial is not None
        if not self.synchronized:
            raise SessionLost("session lost its command prompt; reconnect before further I/O")
        if timeout_s is None:
            timeout_s = self.args.scan_timeout_s if command == "scan" else (
                self.args.stress_timeout_s if command.startswith("stress") else self.args.timeout_s)
        start = time.monotonic()
        deadline = start + timeout_s
        request = (command + "\n").encode("utf-8")
        self.serial.write_timeout = timeout_s
        self.synchronized = False
        # pyserial flush() can wait without a deadline. A bounded write already
        # queues this tiny command; the returned prompt proves it was consumed.
        if self.serial.write(request) != len(request):
            raise SessionLost("short serial command write")
        received = bytearray()
        completion = "timeout waiting for CLI prompt"
        while time.monotonic() < deadline:
            self.serial.timeout = min(0.05, max(0.0, deadline - time.monotonic()))
            waiting = getattr(self.serial, "in_waiting", 0)
            data = self.serial.read(min(waiting or 1, MAX_RESPONSE_BYTES - len(received)))
            received.extend(data)
            if len(received) >= MAX_RESPONSE_BYTES:
                completion = "response exceeded bounded receive buffer"
                break
            clean = strip_ansi(received.decode("utf-8", errors="replace"))
            if PROMPT_RE.search(clean):
                if len(ANY_PROMPT_RE.findall(clean)) != 1:
                    completion = "multiple CLI responses; command attribution lost"
                    break
                self.synchronized = True
                completion = "prompt"
                break
        return Response(received.decode("utf-8", errors="replace"), time.monotonic() - start, completion)


def operation_step(command: str, expected_mask: int | None = None) -> Step:
    expected = ("=== Driver Health ===", "State:") if command == "health" else (
        ("Scan topology:", "Scan complete:") if command == "scan" else (command.split()[0],))
    if command.startswith("stress"):
        expected = ("Stress results:",)
    patterns = COMMON_FAILURE_PATTERNS
    if command != "health":
        patterns += STATUS_FAILURE_PATTERNS
    return Step("substep", "live", command, "Complete successful response.", expected,
                patterns, expected_mask=expected_mask)


def checked_command(runner: SerialRunner, command: str, record: Callable[[str], None],
                    expected_mask: int | None = None) -> str:
    response = runner.command(command)
    record(f"\n$ {command}\n{response.text}")
    status, notes = classify(response.text, operation_step(command, expected_mask), response.completion)
    if status != PASS:
        if response.completion != "prompt":
            raise SessionLost(f"{command}: {notes}")
        raise ValueError(f"{command}: {status}: {notes}")
    return response.text


def read_mask(runner: SerialRunner, record: Callable[[str], None], expected: int | None = None) -> int:
    text = checked_command(runner, "read", record, expected)
    return int(re.search(r" mask=0x([0-9A-Fa-f]{2})\b", strip_ansi(text)).group(1), 16)


def run_mask_checks(runner: SerialRunner, args: argparse.Namespace, record: Callable[[str], None],
                    *, routing: bool) -> tuple[str, float]:
    """Host fixture checks restore only a mask first verified by a real read."""
    started = time.monotonic()
    label = "routing" if routing else "mask sweep"
    original: int | None = None
    completed = 0
    errors: list[str] = []
    try:
        original = read_mask(runner, record)
        if routing:
            declared = {address for _, address in args.route}
            checked_command(runner, "off", record)
            read_mask(runner, record, 0)
            baseline = scan_addresses(checked_command(runner, "scan", record), 0)
            if baseline & declared:
                raise ValueError("declared downstream address is visible with all channels disabled")
            if args.address not in baseline:
                raise ValueError("configured mux address is missing from upstream scan")
            for channel in range(8):
                checked_command(runner, f"select {channel}", record)
                read_mask(runner, record, 1 << channel)
                visible = scan_addresses(checked_command(runner, "scan", record), 1 << channel)
                expected = {address for branch, address in args.route if branch == channel}
                if visible & declared != expected:
                    raise ValueError(f"CH{channel}: expected fixture addresses {sorted(expected)}, observed {sorted(visible & declared)}")
                if not baseline.issubset(visible):
                    raise ValueError(f"CH{channel}: upstream devices disappeared")
                completed += 1
            checked_command(runner, "off", record)
            read_mask(runner, record, 0)
            final = scan_addresses(checked_command(runner, "scan", record), 0)
            if final & declared or not baseline.issubset(final):
                raise ValueError("final all-off scan did not isolate downstream devices")
        else:
            for mask in range(256):
                checked_command(runner, f"mask {mask}", record)
                read_mask(runner, record, mask)
                completed += 1
    except (ValueError, OSError, RuntimeError) as exc:
        errors.append(str(exc))
    finally:
        if original is not None and runner.synchronized:
            try:
                checked_command(runner, f"mask {original}", record)
                read_mask(runner, record, original)
            except (ValueError, OSError, RuntimeError) as exc:
                errors.append(f"entry-mask restore unverified: {exc}")
                record("[FAIL] " + errors[-1])
        elif original is not None:
            errors.append("entry-mask restore NOT_RUN: serial command boundary lost")
            record("[FAIL] " + errors[-1])
    summary = f"{label} complete completed={completed} failures={len(errors)}"
    if errors:
        summary += "\n[FAIL] " + "\n[FAIL] ".join(errors)
    return summary, time.monotonic() - started


def scan_addresses(text: str, expected_mask: int) -> set[int]:
    clean = strip_ansi(text)
    if not re.search(rf"^Scan topology: OK\b.*active_mask=0x{expected_mask:02X}\b", clean, re.M):
        raise ValueError("scan active topology differs from verified selection")
    return {int(value, 16) for value in re.findall(r"Found device at 0x([0-9A-Fa-f]{2})\b", clean)}


def run_soak(runner: SerialRunner, args: argparse.Namespace,
             record: Callable[[str], None]) -> tuple[str, float]:
    commands = ("read", "health", "probe", "recover")
    deadline = time.monotonic() + args.soak_duration_s
    counts = {command: 0 for command in commands}
    failures = 0
    started = time.monotonic()
    worst = 0.0
    failure_note = ""
    index = 0
    command = "<between commands>"

    try:
        while time.monotonic() < deadline and index < MAX_SOAK_COMMANDS:
            command = commands[index % len(commands)]
            index += 1
            response = runner.command(command)
            record(f"\n$ {command}\n{response.text}")
            counts[command] += 1
            worst = max(worst, response.elapsed_s)
            expected_mask = 0 if command == "read" and counts["recover"] else None
            status, notes = classify(response.text, operation_step(command, expected_mask), response.completion)
            if status != PASS:
                failures += 1
                failure_note = f" first_anomaly={command}: {notes}"
                break
            time.sleep(args.command_delay_s)
    except (OSError, RuntimeError, KeyboardInterrupt) as exc:
        failures += 1
        failure_note = f" first_anomaly={command}: {str(exc) or 'operator interrupted the run'}"
    if index == MAX_SOAK_COMMANDS and time.monotonic() < deadline:
        failures += 1
        failure_note = " command cap reached before requested soak duration"
    if runner.synchronized:
        try:
            checked_command(runner, "off", record)
            read_mask(runner, record, 0)
        except (ValueError, OSError, RuntimeError) as exc:
            failures += 1
            failure_note += f" all-off cleanup unverified: {exc}"
    else:
        failures += 1
        failure_note += " all-off cleanup NOT_RUN: serial command boundary lost"
    summary = (
        f"soak complete counts={counts} failures={failures} "
        f"worst_latency_s={worst:.3f}{failure_note}"
    )
    return summary, time.monotonic() - started


def append_not_run_results(
    results: list[Result], plan: list[Step], reason: str
) -> None:
    """Complete an interrupted plan without duplicating finished test IDs."""
    for step in plan[len(results) :]:
        results.append(
            Result(
                step=step,
                status=NOT_RUN,
                observed="not executed",
                notes=reason,
            )
        )


def run_live(args: argparse.Namespace) -> tuple[list[Result], Path | None]:
    transcript_path = Path(args.transcript) if args.transcript else None
    results: list[Result] = []
    plan = build_plan(args)
    with contextlib.ExitStack() as stack:
        transcript = None
        if transcript_path is not None:
            transcript_path.parent.mkdir(parents=True, exist_ok=True)
            transcript = stack.enter_context(transcript_path.open("w", encoding="utf-8"))

        def record(text: str) -> None:
            if transcript is not None:
                transcript.write(text + "\n")
                transcript.flush()
            if args.verbose:
                print(text)

        try:
            runner = stack.enter_context(SerialRunner(args))
            boot = runner.read_until_idle()
            if boot:
                record("$ boot\n" + boot)
            for step in plan:
                started = time.monotonic()
                completion = "prompt"
                try:
                    if step.command == "<host bounded soak loop>":
                        text, elapsed = run_soak(runner, args, record)
                    elif step.command in ("<host routing isolation checks>", "<host all-256 mask sweep>"):
                        text, elapsed = run_mask_checks(runner, args, record,
                                                       routing=step.command == "<host routing isolation checks>")
                    else:
                        response = runner.command(step.command)
                        text, elapsed, completion = response.text, response.elapsed_s, response.completion
                    status, notes = classify(text, step, completion)
                    if status != PASS and step.command.startswith("<host "):
                        notes += "; " + short_observed(text, limit=2000)
                    if status == PASS and step.command == "cfg":
                        addresses = re.findall(r"I2C address:\s+0x([0-9A-Fa-f]{2})\b", strip_ansi(text))
                        if len(addresses) != 1 or int(addresses[0], 16) != args.address:
                            status, notes = FAIL, "firmware address differs from --address"
                    record(f"\n$ {step.command}\n{text}")
                except (OSError, RuntimeError, KeyboardInterrupt) as exc:
                    text, elapsed = str(exc), time.monotonic() - started
                    reason = str(exc) or "operator interrupted the run"
                    status, notes = UNKNOWN, f"serial command interrupted: {reason}"
                    record(f"\n$ {step.command}\n[UNKNOWN] {notes}")
                results.append(Result(step=step, status=status, observed=short_observed(text),
                                      elapsed_s=elapsed, notes=notes))
                if status != PASS:
                    append_not_run_results(results, plan, f"stopped after {step.test_id}: {notes}")
                    break
                time.sleep(args.command_delay_s)
        except (OSError, RuntimeError, KeyboardInterrupt) as exc:
            reason = str(exc) or "operator interrupted the run"
            append_not_run_results(results, plan, f"serial session unavailable: {reason}")
    return results, transcript_path


def parser_self_test(args: argparse.Namespace) -> int:
    plan = build_plan(args)
    if not plan:
        print("Parser self-test: FAIL - empty plan")
        return 1
    for step in plan:
        if not step.test_id or not step.command or not step.expected_tokens:
            print(f"Parser self-test: FAIL - incomplete step {step}")
            return 1

    by_id = {step.test_id: step for step in plan}
    version_step = by_id["TCA-HIL-001"]
    scan_step = by_id["TCA-HIL-005"]
    selftest_step = by_id["TCA-HIL-008"]

    pass_status, _ = classify(
        "=== Version Info ===\n  Library: test-version\n",
        version_step,
    )
    fail_status, _ = classify(
        "=== TCA9548A HIL RUN ===\n  [FAIL] probe - I2C_TIMEOUT\nHIL result: pass=1 fail=1 skip=0\n",
        selftest_step,
    )
    unknown_status, _ = classify("unrelated output\n", version_step)

    scan_started_only, _ = classify(
        "Scan topology: OK active_mask=0x00 [none]\nScanning I2C bus...\n",
        scan_step,
    )
    scan_complete, _ = classify(
        "Scan topology: OK active_mask=0x00 [none]\n"
        "Scanning I2C bus...\n  Found device at 0x70\nScan complete: devices=1 errors=0\n",
        scan_step,
    )
    soak_failure = first_failure(
        "soak complete counts={} failures=1 worst_latency_s=5.000",
        COMMON_FAILURE_PATTERNS,
    )
    unverified_safe_off = first_failure(
        "Stress results: completed=8 requested=8 status=OK safe_off=FAILED",
        COMMON_FAILURE_PATTERNS,
    )

    if (
        pass_status != PASS
        or fail_status != FAIL
        or unknown_status != UNKNOWN
        or scan_started_only != UNKNOWN
        or scan_complete != PASS
        or soak_failure is None
        or unverified_safe_off is None
        or selftest_step.command != (
            "hil run reset" if args.include_reset else "hil run"
        )
    ):
        print(
            "Parser self-test: FAIL - "
            f"pass={pass_status} fail={fail_status} unknown={unknown_status}"
        )
        return 1

    required_not_run = [
        Result(
            step=version_step,
            status=NOT_RUN,
            observed="not executed",
            notes="fixture unavailable",
        )
    ]
    failed = [
        Result(step=version_step, status=FAIL, observed="failure")
    ]
    if (
        result_exit_code(required_not_run, dry_run=True, allow_not_run=False)
        != 0
        or result_exit_code(
            required_not_run, dry_run=False, allow_not_run=False
        )
        != 1
        or result_exit_code(required_not_run, dry_run=False, allow_not_run=True)
        != 0
        or result_exit_code(failed, dry_run=False, allow_not_run=True) != 1
    ):
        print("Parser self-test: FAIL - invalid NOT_RUN exit semantics")
        return 1

    partial_results = [
        Result(step=version_step, status=PASS, observed="version observed")
    ]
    append_not_run_results(partial_results, plan, "session interrupted")
    if (
        len(partial_results) != len(plan)
        or partial_results[0].status != PASS
        or any(result.status != NOT_RUN for result in partial_results[1:])
        or len({result.step.test_id for result in partial_results}) != len(plan)
    ):
        print("Parser self-test: FAIL - interrupted-session result identity")
        return 1

    print(f"Parser self-test: PASS ({len(plan)} planned step(s))")
    return 0


def print_plan(results: list[Result]) -> None:
    print("TCA9548A HIL plan")
    print("ID           RESULT    COMMAND")
    for result in results:
        print(f"{result.step.test_id:<12} {result.status:<9} {result.step.command}")


def parse_address(value: str) -> int:
    try:
        address = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("address must be decimal or 0x-prefixed hexadecimal") from exc
    if not 0x70 <= address <= 0x77:
        raise argparse.ArgumentTypeError("mux address must be 0x70..0x77")
    return address


def parse_route(value: str) -> tuple[int, int]:
    try:
        channel_text, address_text = value.split(":")
        channel, address = int(channel_text, 0), int(address_text, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("route must be CH:ADDR, e.g. 0:0x48") from exc
    if not 0 <= channel <= 7 or not 0x08 <= address <= 0x77:
        raise argparse.ArgumentTypeError("route requires channel 0..7 and address 0x08..0x77")
    return channel, address


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--parser-self-test", action="store_true")
    mode.add_argument("--dry-run", action="store_true")

    parser.add_argument("--port", default="COM8")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--address", type=parse_address, default=0x70,
                        help="expected firmware mux address; this does not reconfigure firmware")
    parser.add_argument("--target-env", default="esp32s3dev")
    parser.add_argument("--timeout-s", type=float, default=5.0)
    parser.add_argument("--scan-timeout-s", type=float, default=30.0)
    parser.add_argument("--stress-timeout-s", type=float, default=60.0)
    parser.add_argument("--idle-timeout-s", type=float, default=0.4)
    parser.add_argument("--boot-settle-s", type=float, default=1.0)
    parser.add_argument("--command-delay-s", type=float, default=0.05)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument(
        "--allow-not-run",
        action="store_true",
        help=(
            "allow required live cases to remain NOT_RUN; FAIL and UNKNOWN "
            "still fail"
        ),
    )
    reset_mode = parser.add_mutually_exclusive_group()
    reset_mode.add_argument(
        "--include-reset",
        dest="include_reset",
        action="store_true",
        default=True,
        help="require live RESET validation (default)",
    )
    reset_mode.add_argument(
        "--skip-reset",
        dest="include_reset",
        action="store_false",
        help="explicitly omit RESET validation; this is not release HIL evidence",
    )
    parser.add_argument("--sample-count", type=int, default=0)
    parser.add_argument("--stress-count", type=int, default=0)
    parser.add_argument("--soak-duration-s", type=float, default=0.0)
    parser.add_argument("--route", type=parse_route, action="append", default=[],
                        help="repeatable downstream fixture CH:ADDR; verifies all eight branch isolations")
    parser.add_argument("--sweep-masks", action="store_true",
                        help="write/read all 256 masks; use a collision-safe fixture (every channel can be enabled)")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--transcript", type=Path)
    parser.add_argument(
        "--not-run-reason",
        default="NOT RUN: no board with TCA9548A attached to the host.",
    )
    parser.add_argument(
        "--fixture-note",
        default="Fixture details not supplied; use --fixture-note to record them.",
    )
    parser.add_argument("--fix-note", action="append", default=[])
    parser.add_argument("--verification-result", action="append", default=[])
    args = parser.parse_args(argv)
    for name in ("timeout_s", "scan_timeout_s", "stress_timeout_s", "idle_timeout_s"):
        value = getattr(args, name)
        if not math.isfinite(value) or not 0 < value <= 600:
            parser.error(f"--{name.replace('_', '-')} must be finite and within (0, 600]")
    for name, maximum in (("boot_settle_s", 60), ("command_delay_s", 60), ("soak_duration_s", 86400)):
        value = getattr(args, name)
        if not math.isfinite(value) or not 0 <= value <= maximum:
            parser.error(f"--{name.replace('_', '-')} must be finite and within [0, {maximum}]")
    for name in ("sample_count", "stress_count"):
        if not 0 <= getattr(args, name) <= 1000:
            parser.error(f"--{name.replace('_', '-')} must be 0 (omitted) or 1..1000")
    if not 0 < args.baud <= 4000000:
        parser.error("--baud must be within 1..4000000")
    if len(args.route) > 112 or len(set(args.route)) != len(args.route):
        parser.error("--route must contain at most 112 distinct channel/address pairs")
    if any(address == args.address for _, address in args.route):
        parser.error("downstream route address must differ from the mux address")
    if args.report is not None and args.transcript is not None and args.report.resolve() == args.transcript.resolve():
        parser.error("--report and --transcript must use different files")
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    if args.parser_self_test:
        return parser_self_test(args)

    if args.dry_run:
        results = dry_run(args)
        print_plan(results)
        transcript_path = None
    else:
        results, transcript_path = run_live(args)
        counts = status_counts(results)
        print(
            "HIL summary: "
            f"pass={counts[PASS]} fail={counts[FAIL]} "
            f"unknown={counts[UNKNOWN]} not_run={counts[NOT_RUN]}"
        )

    if args.report is not None:
        write_report(args.report, args, results, transcript_path)
        print(f"Report written: {args.report}")

    return result_exit_code(
        results,
        dry_run=args.dry_run,
        allow_not_run=args.allow_not_run,
    )


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
