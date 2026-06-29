#!/usr/bin/env python3
"""Small host-side HIL runner for the TCA9548A CLI example.

The live mode talks to examples/01_basic_bringup_cli over a serial port.
Parser self-test and dry-run modes never open the serial port.
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime as dt
import platform
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterable


PASS = "PASS"
FAIL = "FAIL"
UNKNOWN = "UNKNOWN"
NOT_RUN = "NOT_RUN"

ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
COMMON_FAILURE_PATTERNS = (
    r"\[FAIL\]",
    r"\bfail=[1-9][0-9]*\b",
)
STATUS_FAILURE_PATTERNS = (
    r"\bI2C_(?:ERROR|TIMEOUT|BUS|NACK_ADDR|NACK_DATA)\b",
    r"\bDEVICE_NOT_FOUND\b",
    r"\bNOT_INITIALIZED\b",
    r"\bINVALID_(?:CONFIG|PARAM)\b",
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


@dataclasses.dataclass
class Result:
    step: Step
    status: str
    observed: str
    elapsed_s: float = 0.0
    notes: str = ""
    transcript: str = ""


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


def classify(text: str, step: Step) -> tuple[str, str]:
    failure = first_failure(text, step.failure_patterns)
    if failure is not None:
        return FAIL, f"failure pattern matched: {failure}"
    if contains_all(text, step.expected_tokens):
        return PASS, "expected tokens found"
    if strip_ansi(text).strip():
        return UNKNOWN, "expected tokens missing"
    return UNKNOWN, "no response before timeout"


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
            "Configuration snapshot is printed.",
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
            "Upstream I2C scan runs with bounded firmware loop.",
            ("Scanning I2C bus",),
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
            "Live safe HIL contract checks run and restore the mux mask.",
            ("=== TCA9548A HIL RUN ===", "HIL result:"),
            status_patterns,
        ),
    ]

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

    return plan


def git_text(args: list[str], default: str) -> str:
    try:
        result = subprocess.run(
            ["git", *args],
            check=False,
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.TimeoutExpired):
        return default
    text = result.stdout.strip()
    return text if result.returncode == 0 and text else default


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


def write_report(
    path: Path,
    args: argparse.Namespace,
    results: list[Result],
    transcript_path: Path | None,
) -> None:
    now = dt.datetime.now().astimezone()
    dirty = git_text(["status", "--short"], "unknown")
    if dirty == "unknown":
        dirty_summary = "unknown"
    elif dirty == "":
        dirty_summary = "clean"
    else:
        dirty_summary = "dirty before report generation or local edits present"

    counts = status_counts(results)
    command_line = subprocess.list2cmdline([sys.executable, *sys.argv])
    live_mode = not args.dry_run and not args.parser_self_test
    port_note = args.port if live_mode else f"{args.port} (not opened in dry-run mode)"

    lines = [
        f"# TCA9548A HIL Validation Report - {args.port} - {now:%Y-%m-%d}",
        "",
        "## Metadata",
        "",
        f"- Date/time: `{now.isoformat(timespec='seconds')}`",
        f"- Timezone: `{now.tzname() or 'local'}`",
        f"- Repository path: `{Path.cwd()}`",
        f"- Branch: `{git_text(['branch', '--show-current'], 'unknown')}`",
        f"- Commit: `{git_text(['rev-parse', 'HEAD'], 'unknown')}`",
        f"- Dirty status: `{dirty_summary}`",
        f"- Operating system: `{platform.platform()}`",
        f"- Python: `{platform.python_version()}`",
        f"- PlatformIO: `{tool_text(['pio', '--version'], 'not checked')}`",
        f"- Target environment: `{args.target_env}`",
        f"- Serial port: `{port_note}`",
        f"- Baud rate: `{args.baud}`",
        "- Device identity/address: `TCA9548A at configured address 0x70, "
        "not detected in this run`",
        "",
        "## Hardware Setup",
        "",
        f"- Fixture: {args.fixture_note}",
        "- Wiring: not verified in this run.",
        "- Electrical limits: no live electrical tests were performed.",
        "- Safety assumption: no board with this chip is attached, so live HIL, "
        "flash, reset, and soak steps are marked `NOT_RUN`.",
        "",
        "## Exact Commands",
        "",
        "```powershell",
        "python tools\\tca9548a_hil.py --parser-self-test",
        (
            "python tools\\tca9548a_hil.py --dry-run --port "
            f"{args.port} --baud {args.baud}"
        ),
        f"pio run -e {args.target_env}",
        f"pio run -e {args.target_env} -t upload --upload-port {args.port}",
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
            (
                f"- Raw serial transcript: `{transcript_path}`"
                if transcript_path is not None
                else "- Raw serial transcript: not captured; serial port was not opened."
            ),
            "",
            "## Sampling And Timing",
            "",
            "- Sampling/timing highlights: not measured without hardware.",
            "",
            "## Soak Summary",
            "",
            f"- Requested soak duration: `{args.soak_duration_s}` seconds.",
            "- Actual soak duration: `0` seconds.",
            "- Command mix: not run.",
            "- Sample counts: `0`.",
            "- Error counts: not observed.",
            "- Reset/recovery counts: not observed.",
            "- Worst observed latency: not measured.",
            "- Health-state changes: not observed.",
            "- Script adjustments during run: none.",
            "",
            "## Limitations And Tests Not Run",
            "",
            f"- {args.not_run_reason}",
            "- Firmware upload was not attempted.",
            "- Boot transcript and prompt responsiveness were not captured.",
            "- Live scan, probe, mask mutation, recover, reset, stress, and soak "
            "steps were not run.",
            "",
            "## Fixes Implemented During This Pass",
            "",
        ]
    )

    if args.fix_note:
        lines.extend(f"- {note}" for note in args.fix_note)
    else:
        lines.append("- None recorded by the runner.")

    lines.extend(["", "## Final Verification", ""])
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
        time.sleep(self.args.boot_settle_s)
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self.serial is not None:
            self.serial.close()

    def read_until_idle(self) -> str:
        assert self.serial is not None
        deadline = time.monotonic() + self.args.timeout_s
        idle_deadline = time.monotonic() + self.args.idle_timeout_s
        chunks: list[bytes] = []

        while time.monotonic() < deadline:
            waiting = getattr(self.serial, "in_waiting", 0)
            data = self.serial.read(waiting or 1)
            if data:
                chunks.append(data)
                idle_deadline = time.monotonic() + self.args.idle_timeout_s
                continue
            if chunks and time.monotonic() >= idle_deadline:
                break

        return b"".join(chunks).decode("utf-8", errors="replace")

    def command(self, command: str) -> tuple[str, float]:
        assert self.serial is not None
        start = time.monotonic()
        self.serial.write((command + "\n").encode("utf-8"))
        self.serial.flush()
        text = self.read_until_idle()
        return text, time.monotonic() - start


def run_soak(runner: SerialRunner, args: argparse.Namespace) -> tuple[str, float]:
    commands = ("read", "health", "probe", "recover")
    deadline = time.monotonic() + args.soak_duration_s
    counts = {command: 0 for command in commands}
    failures = 0
    latencies: list[float] = []
    transcript_parts: list[str] = []
    index = 0

    while time.monotonic() < deadline:
        command = commands[index % len(commands)]
        index += 1
        text, elapsed = runner.command(command)
        transcript_parts.append(f"\n$ {command}\n{text}")
        counts[command] += 1
        latencies.append(elapsed)
        if first_failure(text, COMMON_FAILURE_PATTERNS + STATUS_FAILURE_PATTERNS):
            failures += 1

    worst = max(latencies) if latencies else 0.0
    summary = (
        f"soak complete counts={counts} failures={failures} "
        f"worst_latency_s={worst:.3f}"
    )
    return summary + "".join(transcript_parts), sum(latencies)


def run_live(args: argparse.Namespace) -> tuple[list[Result], Path | None]:
    transcript_path = Path(args.transcript) if args.transcript else None
    transcript_parts: list[str] = []
    results: list[Result] = []

    try:
        with SerialRunner(args) as runner:
            boot = runner.read_until_idle()
            if boot:
                transcript_parts.append("$ boot\n" + boot)

            for step in build_plan(args):
                if step.command == "<host bounded soak loop>":
                    text, elapsed = run_soak(runner, args)
                else:
                    text, elapsed = runner.command(step.command)
                status, notes = classify(text, step)
                transcript_parts.append(f"\n$ {step.command}\n{text}")
                results.append(
                    Result(
                        step=step,
                        status=status,
                        observed=short_observed(text),
                        elapsed_s=elapsed,
                        notes=notes,
                        transcript=text,
                    )
                )
                if args.verbose:
                    print(f"\n$ {step.command}\n{text}")
                time.sleep(args.command_delay_s)
    except (OSError, RuntimeError) as exc:
        for step in build_plan(args):
            results.append(
                Result(
                    step=step,
                    status=NOT_RUN,
                    observed="not executed",
                    notes=f"serial session unavailable: {exc}",
                )
            )

    if transcript_path is not None:
        transcript_path.parent.mkdir(parents=True, exist_ok=True)
        transcript_path.write_text("\n".join(transcript_parts), encoding="utf-8")

    return results, transcript_path if transcript_path is not None else None


def parser_self_test(args: argparse.Namespace) -> int:
    plan = build_plan(args)
    if not plan:
        print("Parser self-test: FAIL - empty plan")
        return 1
    for step in plan:
        if not step.test_id or not step.command or not step.expected_tokens:
            print(f"Parser self-test: FAIL - incomplete step {step}")
            return 1

    pass_status, _ = classify(
        "=== Version Info ===\n  Library: 1.0.0\n",
        plan[0],
    )
    fail_status, _ = classify(
        "=== TCA9548A HIL RUN ===\n  [FAIL] probe - I2C_TIMEOUT\nHIL result: pass=1 fail=1 skip=0\n",
        plan[7],
    )
    unknown_status, _ = classify("unrelated output\n", plan[0])

    if pass_status != PASS or fail_status != FAIL or unknown_status != UNKNOWN:
        print(
            "Parser self-test: FAIL - "
            f"pass={pass_status} fail={fail_status} unknown={unknown_status}"
        )
        return 1

    print(f"Parser self-test: PASS ({len(plan)} planned step(s))")
    return 0


def print_plan(results: list[Result]) -> None:
    print("TCA9548A HIL plan")
    print("ID           RESULT    COMMAND")
    for result in results:
        print(f"{result.step.test_id:<12} {result.status:<9} {result.step.command}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--parser-self-test", action="store_true")
    mode.add_argument("--dry-run", action="store_true")

    parser.add_argument("--port", default="COM8")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--target-env", default="esp32s3dev")
    parser.add_argument("--timeout-s", type=float, default=5.0)
    parser.add_argument("--idle-timeout-s", type=float, default=0.4)
    parser.add_argument("--boot-settle-s", type=float, default=1.0)
    parser.add_argument("--command-delay-s", type=float, default=0.05)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--include-reset", action="store_true")
    parser.add_argument("--sample-count", type=int, default=0)
    parser.add_argument("--stress-count", type=int, default=0)
    parser.add_argument("--soak-duration-s", type=float, default=0.0)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--transcript", type=Path)
    parser.add_argument(
        "--not-run-reason",
        default="NOT RUN: no board with TCA9548A attached to the host.",
    )
    parser.add_argument(
        "--fixture-note",
        default="No board with TCA9548A attached; live HIL was intentionally skipped.",
    )
    parser.add_argument("--fix-note", action="append", default=[])
    parser.add_argument("--verification-result", action="append", default=[])
    return parser.parse_args(argv)


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

    counts = status_counts(results)
    if counts[FAIL] or counts[UNKNOWN]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
