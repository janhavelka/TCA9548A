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
import hashlib
import json
import math
import platform
import re
import secrets
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
SOAK_BATCH_ROUNDS = 8
MAX_SOAK_BATCHES = 86401
SOAK_BATCH_INTERVAL_S = 1.0
PROMPT_RE = re.compile(r"(?:^|[\r\n])> $")
ANY_PROMPT_RE = re.compile(r"(?:^|[\r\n])> ")
READ_MASK_RE = re.compile(r"^read: OK\b[^\r\n]* mask=0x([0-9A-Fa-f]{2})\b", re.M)
SCAN_ADDRESS_RE = re.compile(r"^[ \t]*Found device at 0x([0-9A-Fa-f]{2})[ \t]*\r?$", re.M)

ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
RESTART_RE = re.compile(
    r"(?:^|[\r\n])[ \t]*(?:>[ \t]*)?(?:ESP-ROM:|rst:0x|boot:0x|Brownout|"
    r"TCA9548A (?:Arduino|native ESP-IDF) bring-up CLI)"
    r"|\b(?:Guru Meditation|PANIC|Rebooting)\b", re.I
)
COMMON_FAILURE_PATTERNS = (
    r"\[FAIL\]",
    r"\bfail=[1-9][0-9]*\b",
    r"\bfailures?=[1-9][0-9]*\b",
    # Printed by both CLIs when the post-stress all-off could not be verified.
    r"\bsafe_off=FAILED\b",
    r"\boutcome=(?:FAIL|CANCELLED)\b",
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
    wire_text: str | None = None


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


def first_failure(text: str, patterns: Iterable[str]) -> str | None:
    clean = strip_ansi(text)
    if RESTART_RE.search(clean):
        return "firmware restarted during command; captured state is no longer valid"
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
    if not all(token in clean for token in step.expected_tokens):
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
        for field, expected in (("Bound", "yes"), ("Initialized", "yes"),
                                ("Controller restart required", "no"),
                                ("Config reference parity", "yes"),
                                ("Snapshot", "OK")):
            values = re.findall(rf"^[ \t]*{re.escape(field)}:[ \t]*([^\r\n]*)", clean, re.M)
            if len(values) != 1 or values[0].strip() != expected:
                return FAIL, f"configuration requires {field}: {expected}"
        return PASS, f"complete configuration; firmware I2C timeout={timeout_ms} ms (reported setting)"
    if command.startswith("hil "):
        summaries = re.findall(r"^HIL result: pass=([0-9]{1,5}) fail=([0-9]{1,5}) skip=([0-9]{1,5})\s*$", clean, re.M)
        checks = re.findall(r"^\s*\[(PASS|FAIL|SKIP)\] ([^\r\n]+)", clean, re.M)
        if len(summaries) != 1 or not checks:
            return UNKNOWN, "missing or ambiguous HIL summary/check records"
        passed, failed, skipped = map(int, summaries[0])
        if (passed, failed, skipped) != tuple(sum(kind == name for kind, _ in checks) for name in (PASS, FAIL, "SKIP")):
            return FAIL, "HIL summary disagrees with check records"
        labels = {label.split(" - ", 1)[0] for kind, label in checks if kind == PASS}
        required = {"typed ChannelMask is one byte", "address helper", "version"}
        if command == "hil parser":
            required.update(("numeric parser rejects invalid input", "line parser rejects controls and overflow"))
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
        try:
            parse_health(clean)
        except ValueError as exc:
            return FAIL, str(exc)
    elif command in ("discover", "detect"):
        summaries = re.findall(
            r"^Detection: tested=([0-9]{1,2}) ack=([0-9]{1,2}) absent=([0-9]{1,2}) errors=([0-9]{1,2}) "
            r"target=0x([0-9A-Fa-f]{2}) target_ack=(yes|no|error|not-tested) identity=unverified[ \t]*\r?$", clean, re.M)
        addresses = [int(value, 16) for value in SCAN_ADDRESS_RE.findall(clean)]
        terminal = re.findall(r"^CLI result: command=discover outcome=PASS completed=8 requested=8[ \t]*\r?$", clean, re.M)
        if len(summaries) != 1 or len(terminal) != 1:
            return FAIL, "missing or ambiguous completed discovery evidence"
        tested, ack, absent, errors, target, target_ack = summaries[0]
        if (int(tested) != 8 or int(errors) != 0 or int(ack) != len(addresses)
                or int(ack) + int(absent) != 8 or len(addresses) != len(set(addresses))
                or any(not 0x70 <= address <= 0x77 for address in addresses)
                or not 0x70 <= int(target, 16) <= 0x77
                or target_ack != ("yes" if int(target, 16) in addresses else "no")
                or re.findall(r"^Discover complete: devices=([0-9]{1,2}) errors=0[ \t]*\r?$", clean, re.M) != [ack]):
            return FAIL, "discovery counts, candidates or target ACK disagree"
    elif command == "scan":
        summaries = re.findall(r"^Scan complete: devices=([0-9]{1,3}) errors=([0-9]{1,3})\s*$", clean, re.M)
        addresses = [int(value, 16) for value in SCAN_ADDRESS_RE.findall(clean)]
        if len(summaries) != 1:
            return UNKNOWN, "missing or ambiguous scan completion"
        devices, errors = map(int, summaries[0])
        if (errors or devices != len(addresses) or len(addresses) != len(set(addresses))
                or any(not 0x08 <= address <= 0x77 for address in addresses)):
            return FAIL, "scan errors or inconsistent device count"
        if len(re.findall(r"^Scan topology: OK\b.*active_mask=0x[0-9A-Fa-f]{2}\b", clean, re.M)) != 1:
            return FAIL, "scan topology read failed"
    elif command.startswith("stress"):
        summaries = re.findall(r"^Stress results: completed=([0-9]{1,4}) requested=([0-9]{1,4}) status=(\w+) safe_off=(\w+)\s*$", clean, re.M)
        if len(summaries) != 1:
            return UNKNOWN, "missing or ambiguous stress completion"
        completed, requested, status, safe_off = summaries[0]
        count = int(command.split()[1])
        if int(completed) != count or int(requested) != count or status != "OK" or safe_off != "OK":
            return FAIL, "stress incomplete or safe-off unverified"
        deltas = re.findall(r"^Health delta: success=([0-9]{1,10}) failure=0\s*$", clean, re.M)
        if len(deltas) != 1 or int(deltas[0]) > 0xFFFFFFFF:
            return FAIL, "stress health failures or missing counters"
        # Session counts prove every operation even if lifetime success has saturated.
        session_lines = re.findall(r"^Session transfers:([^\r\n]*)", clean, re.M)
        session = re.fullmatch(r" success=([0-9]{1,7}) failure=0", session_lines[0]) if len(session_lines) == 1 else None
        expected = (7 if command.startswith("stress_mix ") else 2) * count + 2
        if session is None or int(session[1]) != expected:
            return FAIL, "missing or invalid per-session transfer evidence for every iteration and cleanup"
    elif command == "read":
        matches = READ_MASK_RE.findall(clean)
        if len(matches) != 1:
            return UNKNOWN, "missing or ambiguous control-byte read"
        if step.expected_mask is not None and int(matches[0], 16) != step.expected_mask:
            return FAIL, f"mask readback mismatch; expected 0x{step.expected_mask:02X}"
    elif command == "console":
        try:
            parse_console(clean)
        except ValueError as exc:
            return FAIL, str(exc)
    elif command.startswith("verbose"):
        values = re.findall(r"^verbose: (on|off)[ \t]*\r?$", clean, re.M)
        requested = None if command == "verbose" else ("on" if command.split()[1] in ("1", "on") else "off")
        if len(values) != 1 or (requested is not None and values[0] != requested):
            return FAIL, "verbose setting missing, ambiguous or different from requested value"
    elif command in ("probe", "off", "recover", "invalidate") or command.startswith(("mask ", "select ", "enable ", "disable ", "verify ")):
        prefix = {"recover": "recover (safe-off write)"}.get(command, command)
        if command.startswith("verify "):
            prefix = "verify"
        if command.startswith("mask "):
            prefix = f"mask 0x{int(command.split()[1], 0):02X}"
        if not re.search(rf"^{re.escape(prefix)}: OK(?:\s|$)", clean, re.M):
            return FAIL, "operation did not report explicit OK"
        if command == "invalidate":
            observations = re.findall(r"^Mask cache: ([^\r\n]*)", clean, re.M)
            if len(observations) != 1 or re.fullmatch(
                    r"UNKNOWN known=no verified=no value=0x[0-9A-Fa-f]{2} \[(?:none|[0-7](?:,[0-7])*)\]",
                    observations[0]) is None:
                return FAIL, "cache invalidation did not report a complete unknown observation"
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
        Step("TCA-HIL-015", "console", "console",
             "Capture complete unsaturated console loss counters before live diagnostics.", ("Console:",)),
        Step(
            "TCA-HIL-004",
            "health",
            "health",
            "Driver health snapshot is printed.",
            ("=== Driver Health ===", "State:"),
        ),
        Step("TCA-HIL-016", "detection", "discover",
             "All eight legal addresses tested; configured target ACKs without claiming silicon identity.",
             ("Detection:", "CLI result:"), status_patterns),
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
        Step("TCA-HIL-018", "contract", "hil parser",
             "Device-side numeric and line parsers reject invalid input without I2C.",
             ("=== TCA9548A HIL DRY-RUN ===", "HIL result:"), status_patterns),
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
        Step("TCA-HIL-019", "controls", "<host control operation checks>",
             "Enable/disable preserve unrelated bits; verify and cache invalidation work; entry mask restored.",
             ("controls complete",), status_patterns),
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
                "Complete eight-round mixed batches with live readback, health and console checks for the bounded duration.",
                ("soak complete",),
                status_patterns,
            )
        )

    plan.append(Step("TCA-HIL-014", "health", "health",
                     "Final driver state is READY with no consecutive failures.",
                     ("=== Driver Health ===", "State:")))
    plan.append(Step("TCA-HIL-017", "console", "console",
                     "No console output loss or observable input overflow since the initial snapshot.", ("Console:",)))
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
    counts = status_counts(results)
    if counts[FAIL] or counts[UNKNOWN]:
        return 1

    if counts[NOT_RUN] and not dry_run and not allow_not_run:
        return 1
    return 0


def overall_status(results: Iterable[Result], artifact_errors: Iterable[str] = ()) -> str:
    """Describe evidence independently of dry-run/NOT_RUN exit-code allowances."""
    counts = status_counts(results)
    if counts[FAIL] or any(artifact_errors):
        return FAIL
    if counts[UNKNOWN]:
        return UNKNOWN
    if counts[NOT_RUN] or not counts[PASS]:
        return NOT_RUN
    return PASS


def repository_provenance(exclude_paths: Iterable[Path] = ()) -> dict[str, str]:
    status_args = ["status", "--short", "--untracked-files=all", "--", "."]
    for path in exclude_paths:
        try:
            relative = path.resolve().relative_to(ROOT)
        except ValueError:
            continue
        status_args.append(f":(exclude){relative.as_posix()}")
    status = git_text(status_args, "unknown", allow_empty=True)
    return {
        "branch": git_text(["branch", "--show-current"], "unknown"),
        "commit": git_text(["rev-parse", "HEAD"], "unknown"),
        "worktree": "unknown" if status == "unknown" else "dirty" if status else "clean",
        "status_short": status,
    }


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def artifact_error(args: argparse.Namespace, message: str) -> None:
    if not hasattr(args, "_artifact_errors"):
        args._artifact_errors = []
    args._artifact_errors.append(message)


def prepare_artifacts(args: argparse.Namespace) -> dict[str, Path]:
    """Reserve all requested outputs exclusively before opening the device."""
    args._artifact_handles = {}
    paths: dict[str, Path] = {}
    if args.output_dir is not None:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        stamp = dt.datetime.now(dt.timezone.utc).strftime("tca9548a_%Y%m%dT%H%M%S_%fZ")
        for attempt in range(1000):
            run_dir = args.output_dir / (stamp if attempt == 0 else f"{stamp}_{attempt:03d}")
            try:
                run_dir.mkdir()
                break
            except FileExistsError:
                continue
        else:
            raise OSError("unable to create a unique HIL output directory")
        args._run["run_id"] = run_dir.name
        args.report = run_dir / "report.md"
        args.transcript = None if args.dry_run else run_dir / "transcript.log"
        paths.update(results=run_dir / "results.json", manifest=run_dir / "manifest.json")
    if args.report is not None:
        paths["report"] = args.report
    if args.transcript is not None and not args.dry_run:
        paths["transcript"] = args.transcript
    # Check every destination before creating any files, including existing aliases.
    requested = [path for path in (args.report, args.transcript) if path is not None]
    resolved = [path.resolve() for path in requested]
    if len(resolved) != len(set(resolved)):
        raise OSError("artifact paths refer to the same file")
    for path in requested + list(paths.values()):
        if path.exists() or path.is_symlink():
            raise FileExistsError(f"refusing to overwrite existing artifact: {path}")
    for path in paths.values():
        path.parent.mkdir(parents=True, exist_ok=True)
        args._artifact_handles[path.resolve()] = path.open("x", encoding="utf-8", newline="\n")
    args._artifact_paths = paths
    return paths


def write_artifact_text(path: Path, args: argparse.Namespace, text: str) -> None:
    handle = getattr(args, "_artifact_handles", {}).get(path.resolve())
    if handle is None:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("x", encoding="utf-8", newline="\n") as output:
            output.write(text)
    else:
        try:
            handle.write(text)
            handle.flush()
        finally:
            # Detect buffered/close failures before sealing the bundle manifest.
            handle.close()


def write_bundle(args: argparse.Namespace, results: list[Result], paths: dict[str, Path],
                 exit_code: int) -> None:
    errors = getattr(args, "_artifact_errors", [])
    settings = {key: str(value) if isinstance(value, Path) else value
                for key, value in vars(args).items() if not key.startswith("_")}
    document = {
        "schema_version": 1,
        "run": args._run,
        "mode": "dry-run" if args.dry_run else "live",
        "settings": settings,
        "overall_status": overall_status(results, errors),
        "exit_code": exit_code,
        "counts": status_counts(results),
        "firmware_version_response": getattr(args, "_firmware_version", None),
        "artifact_errors": errors,
        "completion_contract": "The bundle is complete only when manifest.json is present and its artifact hashes match.",
        "claim_boundary": "Host checkout metadata is not verified firmware identity; dry-run is not hardware evidence.",
        "results": [dataclasses.asdict(result) for result in results],
    }
    write_artifact_text(paths["results"], args, json.dumps(document, indent=2) + "\n")
    artifacts = {name: {"path": str(path.resolve()), "bytes": path.stat().st_size,
                        "sha256": sha256_file(path)}
                 for name, path in paths.items() if name != "manifest"}
    manifest = {
        "schema_version": 1,
        "run_id": args._run["run_id"],
        "overall_status": document["overall_status"],
        "runner": {"path": str(Path(__file__).resolve()),
                   "sha256": args._run["runner_sha256"]},
        "artifacts": artifacts,
    }
    write_artifact_text(paths["manifest"], args, json.dumps(manifest, indent=2) + "\n")


def write_report(
    path: Path,
    args: argparse.Namespace,
    results: list[Result],
    transcript_path: Path | None,
) -> None:
    now = dt.datetime.now().astimezone()
    run = getattr(args, "_run", {})
    provenance = run.get("host_start") or repository_provenance()
    dirty_summary = provenance["worktree"]

    counts = status_counts(results)
    command_line = subprocess.list2cmdline([sys.executable, str(Path(__file__).resolve()),
                                          *getattr(args, "_argv", [])])
    platformio_version = tool_text(
        platformio_tool_args("--version"), "not checked"
    )
    live_mode = not args.dry_run and not args.parser_self_test
    port_note = args.port if live_mode else f"{args.port} (not opened in dry-run mode)"
    executed_results = [result for result in results if result.status != NOT_RUN]
    session_executed = live_mode and getattr(args, "_serial_commands_executed", bool(executed_results))
    if not session_executed:
        executed_results = []
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
    route_result = next(
        (result for result in results if result.step.command == "<host routing isolation checks>"),
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
            + (f"Declared routing fixture result: {route_result.status}; undeclared endpoints remain untested."
               if route_result is not None else "No --route fixture was supplied; downstream routing is untested."),
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
        f"- Run ID: `{run.get('run_id', 'not captured')}`",
        f"- Started UTC: `{run.get('started_utc', 'not captured')}`",
        f"- Finished UTC: `{run.get('finished_utc', 'not captured')}`",
        f"- Overall evidence status: `{overall_status(results, getattr(args, '_artifact_errors', []))}`",
        f"- Timezone: `{now.tzname() or 'local'}`",
        f"- Repository path: `{ROOT}`",
        f"- Branch: `{provenance['branch']}`",
        f"- Host checkout commit (not verified firmware identity): `{provenance['commit']}`",
        f"- Dirty status: `{dirty_summary}`",
        f"- Host provenance changed during run: `{run.get('host_changed', 'not captured')}`",
        f"- Runner source changed during run: `{run.get('runner_changed', 'not captured')}`",
        f"- Operating system: `{platform.platform()}`",
        f"- Python: `{platform.python_version()}`",
        f"- PlatformIO: `{platformio_version}`",
        f"- Operator-supplied target environment (not verified): `{args.target_env}`",
        f"- Serial port: `{port_note}`",
        f"- Baud rate: `{args.baud}`",
        f"- Expected mux address: `0x{args.address:02X}`",
        f"- Routing fixtures (channel, address): `{args.route}`",
        f"- Exhaustive mask sweep requested: `{args.sweep_masks}`",
        "- Host soak may exceed its scheduling interval by the in-flight mixed "
        "batch and bounded all-off/presentation cleanup. Each command has a finite deadline; "
        f"at most {MAX_SOAK_BATCHES} batches start, no faster than one per second.",
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
    firmware_version = getattr(args, "_firmware_version", None)
    if firmware_version is not None:
        lines.extend(["## Captured Firmware Version Response", "", "```text", firmware_version, "```", ""])
    write_artifact_text(path, args, "\n".join(lines))


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
        self.synchronized = False
        self._synchronization_attempted = False

    def __enter__(self) -> "SerialRunner":
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise RuntimeError("pyserial is required for live HIL runs") from exc

        self.serial = serial.Serial(
            port=None,
            baudrate=self.args.baud,
            timeout=0.05,
            write_timeout=self.args.timeout_s,
        )
        try:
            # Match the monitor configuration: configure control lines before
            # opening the port so a long-run connection does not request reset.
            self.serial.dtr = bool(self.args.dtr)
            self.serial.rts = bool(self.args.rts)
            self.serial.port = self.args.port
            self.serial.open()
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
            self.serial.timeout = min(0.05, max(0.0, deadline - time.monotonic()))
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

    def synchronize(self) -> Response:
        """Discard retained input and prove freshness once, before any test I/O."""
        if self._synchronization_attempted:
            raise SessionLost("initial synchronization is not a retry after lost framing")
        self._synchronization_attempted = True
        response = self._exchange("\x15", self.args.timeout_s)
        if response.completion == "prompt":
            acknowledgement = strip_ansi(response.text).replace("\r", "").strip()
            if acknowledgement not in (
                "[E] Command discarded: invalid input\n>",
                "[E] Command discarded: line too long\n>",
            ):
                self.synchronized = False
                response = dataclasses.replace(response, completion="initial command discard was not proven")
        return response

    def command(self, command: str, timeout_s: float | None = None) -> Response:
        assert self.serial is not None
        if not self.synchronized:
            raise SessionLost("session lost its command prompt; reconnect before further I/O")
        if timeout_s is None:
            timeout_s = self.args.scan_timeout_s if command == "scan" else (
                self.args.stress_timeout_s if command.startswith("stress") else self.args.timeout_s)
        return self._exchange(command, timeout_s)

    def _exchange(self, command: str, timeout_s: float) -> Response:
        assert self.serial is not None
        start = time.monotonic()
        deadline = start + timeout_s
        self.synchronized = False
        received = bytearray()
        completion = "timeout waiting for CLI prompt"
        marker = None

        def write(payload: bytes) -> None:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise SessionLost("command deadline expired before serial write")
            self.serial.write_timeout = min(self.args.timeout_s, remaining)
            # flush() can wait without a deadline. The fresh marker below proves
            # the queued command was consumed without an unbounded drain wait.
            if self.serial.write(payload) != len(payload):
                raise SessionLost("short serial command write")
            if time.monotonic() >= deadline:
                raise SessionLost("serial write exceeded command deadline")

        def read() -> bytes:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return b""
            self.serial.timeout = min(0.05, remaining)
            waiting = getattr(self.serial, "in_waiting", 0)
            return self.serial.read(min(waiting or 1, 4096, MAX_RESPONSE_BYTES - len(received)))

        try:
            # Leftover prompts are harmless. Any other pending payload is not a
            # response to the command we are about to send.
            while getattr(self.serial, "in_waiting", 0):
                data = read()
                received.extend(data)
                if len(received) >= MAX_RESPONSE_BYTES:
                    raise SessionLost("pending output exceeded bounded receive buffer")
                if not data or time.monotonic() >= deadline:
                    raise SessionLost("deadline expired draining pending output")
            if strip_ansi(received.decode("utf-8", errors="replace")).strip("> \r\n\t"):
                raise SessionLost("unsolicited serial payload before command; command was not sent")
            received.clear()
            write((command + "\n").encode("utf-8"))
            while time.monotonic() < deadline:
                data = read()
                received.extend(data)
                if len(received) >= MAX_RESPONSE_BYTES:
                    completion = "response exceeded bounded receive buffer"
                    break
                if time.monotonic() >= deadline:
                    if data:
                        completion = "response arrived after command deadline"
                    break
                if not data:
                    continue
                clean = strip_ansi(received.decode("utf-8", errors="replace"))
                if RESTART_RE.search(clean):
                    completion = "firmware restarted; reconnect before further I/O"
                    break
                if not PROMPT_RE.search(clean):
                    continue
                prompts = len(ANY_PROMPT_RE.findall(clean))
                if marker is None:
                    if prompts != 1:
                        completion = "multiple CLI responses; command attribution lost"
                        break
                    if "HIL marker:" in clean:
                        completion = "unsolicited command marker; command attribution lost"
                        break
                    marker = secrets.token_hex(16)
                    write(("hilmark " + marker + "\n").encode("ascii"))
                    completion = "timeout waiting for fresh command marker"
                    continue
                marker_matches = list(re.finditer(r"HIL marker: ([A-Za-z0-9_-]{1,32})\r?\n", clean))
                if len(marker_matches) != 1 or marker_matches[0][1] != marker or prompts != 2:
                    completion = "missing, stale, or duplicate command marker; command attribution lost"
                    break
                marker_start = marker_matches[0].start()
                if (not PROMPT_RE.search(clean[:marker_start]) or
                        clean[marker_matches[0].end():] != "> "):
                    completion = "unexpected payload around command marker; command attribution lost"
                    break
                # Keep the original response and its prompt for the existing
                # command validators. The marker is framing, not chip evidence.
                original = received.decode("utf-8", errors="replace").split("HIL marker:", 1)[0]
                self.synchronized = True
                return Response(original, time.monotonic() - start, "prompt",
                                received.decode("utf-8", errors="replace"))
        except (OSError, SessionLost, KeyboardInterrupt) as exc:
            completion = str(exc) or "operator interrupted the command"
        return Response(received.decode("utf-8", errors="replace"), time.monotonic() - start, completion)


def operation_step(command: str, expected_mask: int | None = None) -> Step:
    expected = ("=== Driver Health ===", "State:") if command == "health" else (
        ("Scan topology:", "Scan complete:") if command == "scan" else (
            ("Console:",) if command == "console" else (command.split()[0],)))
    if command.startswith("stress"):
        expected = ("Stress results:",)
    elif command in ("discover", "detect"):
        expected = ("Detection:", "CLI result:")
    elif command.startswith("hil "):
        expected = ("HIL result:",)
    patterns = COMMON_FAILURE_PATTERNS
    if command != "health":
        patterns += STATUS_FAILURE_PATTERNS
    return Step("substep", "live", command, "Complete successful response.", expected,
                patterns, expected_mask=expected_mask)


def checked_command(runner: SerialRunner, command: str, record: Callable[[str], None],
                    expected_mask: int | None = None) -> str:
    response = runner.command(command)
    if RESTART_RE.search(strip_ansi(response.text)):
        runner.synchronized = False
    record(f"\n$ {command}\n{response.wire_text or response.text}")
    status, notes = classify(response.text, operation_step(command, expected_mask), response.completion)
    if status != PASS:
        if response.completion != "prompt":
            raise SessionLost(f"{command}: {notes}")
        raise ValueError(f"{command}: {status}: {notes}")
    return response.text


def read_mask(runner: SerialRunner, record: Callable[[str], None], expected: int | None = None) -> int:
    text = checked_command(runner, "read", record, expected)
    return int(READ_MASK_RE.search(strip_ansi(text)).group(1), 16)


def run_mask_checks(runner: SerialRunner, args: argparse.Namespace, record: Callable[[str], None],
                    *, mode: str) -> tuple[str, float]:
    """Host fixture checks restore only a mask first verified by a real read."""
    if mode not in ("routing", "sweep", "controls"):
        raise ValueError("unknown mask-check mode")
    started = time.monotonic()
    label = "mask sweep" if mode == "sweep" else mode
    original: int | None = None
    completed = 0
    errors: list[str] = []
    try:
        original = read_mask(runner, record)
        if mode == "routing":
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
        elif mode == "sweep":
            for mask in range(256):
                checked_command(runner, f"mask {mask}", record)
                read_mask(runner, record, mask)
                completed += 1
        else:
            for command, expected in (("off", 0), ("enable 0", 1), ("enable 7", 0x81),
                                      ("invalidate", 0x81), ("disable 0", 0x80), ("disable 7", 0)):
                checked_command(runner, command, record)
                read_mask(runner, record, expected)
                checked_command(runner, f"verify {expected}", record)
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
    return {int(value, 16) for value in SCAN_ADDRESS_RE.findall(clean)}


def parse_health(text: str) -> dict[str, int]:
    """Require every cached health row; historical errors may remain sticky."""
    rows = [line.strip() for line in strip_ansi(text).splitlines() if line.strip()]
    if rows and rows[0] == "health":
        rows.pop(0)
    if rows and rows[-1] == ">":
        rows.pop()
    patterns = (
        r"=== Driver Health ===",
        r"State: READY \(passive; never gates I2C\)",
        r"State alias parity: yes", r"Bound: yes", r"Initialized: yes", r"Online: yes",
        r"Consecutive failures: 0",
        r"Total success/failure: (?P<successes>[0-9]{1,10})/(?P<failures>[0-9]{1,10})",
        r"Last OK/error ms: (?P<last_ok_ms>[0-9]{1,10})/(?P<last_error_ms>[0-9]{1,10})",
        r"Last error: (?:OK|[A-Z][A-Z0-9_]* \(detail=(?P<detail>-?[0-9]{1,10}), [^\r\n]*\))",
        r"Mask cache: (?P<provenance>UNKNOWN|WRITE_COMPLETED|READBACK_OBSERVED) "
        r"known=(?P<known>yes|no) verified=(?P<verified>yes|no) value=0x(?P<mask>[0-9A-Fa-f]{2}) \[(?P<channels>none|[0-7](?:,[0-7])*)\]",
    )
    if len(rows) != len(patterns):
        raise ValueError("incomplete or ambiguous driver health snapshot")
    fields = {}
    for row, pattern in zip(rows, patterns):
        match = re.fullmatch(pattern, row)
        if match is None:
            raise ValueError("malformed or unhealthy driver health row: " + row)
        fields.update({name: value for name, value in match.groupdict().items() if value is not None})
    counters = {name: int(fields[name]) for name in ("successes", "failures", "last_ok_ms", "last_error_ms")}
    if any(value > 0xFFFFFFFF for value in counters.values()):
        raise ValueError("driver health counters exceed uint32 bounds")
    if counters["successes"] == 0:
        raise ValueError("READY health snapshot lacks a tracked success")
    if "detail" in fields and not -0x80000000 <= int(fields["detail"]) <= 0x7FFFFFFF:
        raise ValueError("driver health error detail exceeds int32 bounds")
    if counters["failures"] == 0xFFFFFFFF:
        raise ValueError("saturated driver failure counter cannot prove a fault-free run")
    provenance = fields["provenance"]
    if (fields["known"] != ("no" if provenance == "UNKNOWN" else "yes")
            or fields["verified"] != ("yes" if provenance == "READBACK_OBSERVED" else "no")):
        raise ValueError("driver mask provenance and validity flags disagree")
    mask = int(fields["mask"], 16)
    channels = ",".join(str(channel) for channel in range(8) if mask & (1 << channel)) or "none"
    if fields["channels"] != channels:
        raise ValueError("driver mask value and channel list disagree")
    return counters


def compare_health(text: str, baseline: dict[str, int] | None) -> dict[str, int]:
    current = parse_health(text)
    if baseline is not None:
        if current["failures"] != baseline["failures"]:
            raise ValueError("driver total failures changed during session")
        if current["successes"] < baseline["successes"]:
            raise ValueError("driver success counter moved backwards; possible restart")
    return current


def parse_console(text: str) -> dict[str, int | str]:
    rows = re.findall(r"^Console: ([^\r\n]*)", strip_ansi(text), re.M)
    names = ("queued_bytes", "accepted_bytes", "dropped_bytes", "dropped_messages", "rx_overflows")
    pattern = r"backend=([^\s=]+) connected=(yes|no|unknown)" + "".join(
        rf" {name}=([0-9]{{1,10}})" for name in names)
    match = re.fullmatch(pattern, rows[0]) if len(rows) == 1 else None
    if match is None:
        raise ValueError("console counters missing, ambiguous or malformed")
    result = dict(zip(names, map(int, match.groups()[2:])))
    if any(value > 0xFFFFFFFF for value in result.values()):
        raise ValueError("console counters exceed uint32 bounds")
    if match[2] == "no":
        raise ValueError("console reports a disconnected host")
    if any(result[name] == 0xFFFFFFFF for name in names[2:]):
        raise ValueError("saturated console loss counters cannot prove complete evidence")
    result.update(backend=match[1], connected=match[2])
    return result


def compare_console(text: str, baseline: dict[str, int | str] | None) -> dict[str, int | str]:
    current = parse_console(text)
    if baseline is not None:
        if current["backend"] != baseline["backend"]:
            raise ValueError("console backend changed during session")
        for name in ("dropped_bytes", "dropped_messages", "rx_overflows"):
            if current[name] != baseline[name]:
                raise ValueError(f"console {name} changed during session")
        if current["accepted_bytes"] < baseline["accepted_bytes"]:
            raise ValueError("console accepted counter moved backwards; possible restart")
    return current


def run_soak(runner: SerialRunner, args: argparse.Namespace,
             record: Callable[[str], None]) -> tuple[str, float]:
    """Finish verified mixed batches; retain only bounded aggregate evidence."""
    started = time.monotonic()
    batches = 0
    attempts = 0
    commands = 0
    worst = 0.0
    errors: list[str] = []
    command = "<between commands>"
    baseline = None
    health_baseline = None
    final_console_verified = False
    final_health_verified = False
    previous_verbose = None
    deadline = started

    def run(text: str, expected_mask: int | None = None) -> str:
        nonlocal command, commands, worst
        command = text
        response = runner.command(text)
        if RESTART_RE.search(strip_ansi(response.text)):
            runner.synchronized = False
        commands += 1
        worst = max(worst, response.elapsed_s)
        record(f"\n$ {text}\n{response.wire_text or response.text}")
        status, notes = classify(response.text, operation_step(text, expected_mask), response.completion)
        if status != PASS:
            if response.completion != "prompt":
                raise SessionLost(f"{text}: {notes}")
            raise ValueError(f"{text}: {status}: {notes}")
        return response.text

    def check_console() -> None:
        nonlocal baseline
        baseline = compare_console(run("console"), baseline)

    def check_health() -> None:
        nonlocal health_baseline
        health_baseline = compare_health(run("health"), health_baseline)

    try:
        baseline = parse_console(run("console"))
        check_health()
        previous_verbose = re.search(r"^verbose: (on|off)", strip_ansi(run("verbose")), re.M)[1]
        if previous_verbose == "on":
            run("verbose off")
        deadline = time.monotonic() + args.soak_duration_s
        while time.monotonic() < deadline and attempts < MAX_SOAK_BATCHES:
            batch_started = time.monotonic()
            attempts += 1
            run(f"stress_mix {SOAK_BATCH_ROUNDS}")
            run("read", 0)
            check_health()
            check_console()
            batches += 1
            # One new batch per second bounds even a very fast 24-hour run.
            pause = min(deadline - time.monotonic(),
                        max(args.command_delay_s, batch_started + SOAK_BATCH_INTERVAL_S - time.monotonic()))
            while pause > 0:
                time.sleep(min(pause, 1.0))
                pause = min(deadline, batch_started + max(SOAK_BATCH_INTERVAL_S, args.command_delay_s)) - time.monotonic()
        if batches == 0:
            raise ValueError("soak duration expired without a complete verified batch")
        if time.monotonic() < deadline:
            raise ValueError("batch cap reached before requested soak duration")
    except (ValueError, OSError, RuntimeError, KeyboardInterrupt) as exc:
        errors.append(f"first_anomaly={command}: {str(exc) or 'operator interrupted the run'}")
    finally:
        cleanup = [("off", None), ("read", 0)] if attempts else []
        if previous_verbose == "on":
            cleanup.append(("verbose on", None))
        for text, mask in cleanup:
            if not runner.synchronized:
                errors.append("all-off/presentation cleanup NOT_RUN: serial command boundary lost")
                break
            try:
                run(text, mask)
            except (ValueError, OSError, RuntimeError, KeyboardInterrupt) as exc:
                errors.append(f"cleanup {text} unverified: {str(exc) or 'operator interrupted'}")
        if baseline is not None and runner.synchronized:
            if health_baseline is not None:
                try:
                    check_health()
                    final_health_verified = True
                except (ValueError, OSError, RuntimeError, KeyboardInterrupt) as exc:
                    errors.append(f"final health evidence unverified: {str(exc) or 'operator interrupted'}")
            try:
                check_console()
                final_console_verified = True
            except (ValueError, OSError, RuntimeError, KeyboardInterrupt) as exc:
                errors.append(f"final console evidence unverified: {str(exc) or 'operator interrupted'}")
    summary = (
        f"soak complete batches={batches} cycles={batches * SOAK_BATCH_ROUNDS} failures={len(errors)} "
        f"commands={commands} worst_latency_s={worst:.3f} "
        f"console_loss_checked={'yes' if final_console_verified else 'no'} "
        f"health_history_checked={'yes' if final_health_verified else 'no'} "
        "SDK_RX_loss_observability=backend-dependent"
    )
    if errors:
        summary += "\n[FAIL] " + "\n[FAIL] ".join(errors)
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
    args._serial_commands_executed = False
    transcript_path = Path(args.transcript) if args.transcript else None
    results: list[Result] = []
    plan = build_plan(args)
    console_baseline = None
    health_baseline = None

    def fail_session(note: str) -> None:
        append_not_run_results(results, plan, note)
        if not results:
            artifact_error(args, note)
            return
        result = next((item for item in reversed(results) if item.status == FAIL), None)
        if result is None:
            result = next((item for item in reversed(results) if item.status != NOT_RUN), results[0])
            result.status = UNKNOWN
        if note not in result.notes:
            result.notes += "; " + note

    with contextlib.ExitStack() as stack:
        transcript = None
        transcript_error = ""
        if transcript_path is not None:
            try:
                transcript_path.parent.mkdir(parents=True, exist_ok=True)
                transcript = getattr(args, "_artifact_handles", {}).get(transcript_path.resolve())
                if transcript is None:
                    transcript = transcript_path.open("x", encoding="utf-8", newline="\n")
                if hasattr(args, "_run"):
                    transcript.write("# TCA9548A HIL serial transcript\n"
                                     + json.dumps(args._run, sort_keys=True) + "\n")
                    transcript.flush()
            except OSError as exc:
                artifact_error(args, f"transcript unavailable: {exc}")
                if transcript is not None:
                    with contextlib.suppress(OSError):
                        transcript.close()
                append_not_run_results(results, plan, f"transcript unavailable: {exc}")
                return results, None

            def close_transcript() -> None:
                try:
                    transcript.close()
                except OSError as exc:
                    artifact_error(args, f"transcript close failed: {exc}")
                    # Preserve the result list/report even if the final flush fails.
                    if results:
                        result = next((item for item in reversed(results) if item.status != NOT_RUN), results[0])
                        if result.status != FAIL:
                            result.status = UNKNOWN
                        result.notes += f"; transcript close failed: {exc}"

            stack.callback(close_transcript)

        def record(text: str) -> None:
            nonlocal transcript_error
            if transcript is not None and not transcript_error:
                try:
                    transcript.write(text + "\n")
                    transcript.flush()
                except OSError as exc:
                    # Raise once to stop the active test. Later cleanup commands
                    # still run and their status remains available to the report.
                    transcript_error = f"transcript write failed: {exc}"
                    artifact_error(args, transcript_error)
                    raise RuntimeError(transcript_error) from exc
            if args.verbose:
                print(text)

        try:
            runner = SerialRunner(args).__enter__()

            def close_runner() -> None:
                try:
                    runner.__exit__(None, None, None)
                except (OSError, RuntimeError, KeyboardInterrupt) as exc:
                    reason = str(exc) or "operator interrupted serial close"
                    fail_session("serial close failed: " + reason)

            stack.callback(close_runner)
            boot = runner.read_until_idle()
            if boot:
                record("$ boot\n" + boot)
            synchronization = runner.synchronize()
            record("$ initial input discard and fresh marker\n" +
                   (synchronization.wire_text or synchronization.text))
            if synchronization.completion != "prompt":
                raise SessionLost("initial synchronization failed: " + synchronization.completion)
            for step in plan:
                started = time.monotonic()
                completion = "prompt"
                wire_text = None
                try:
                    args._serial_commands_executed = True
                    if step.command == "<host bounded soak loop>":
                        text, elapsed = run_soak(runner, args, record)
                    elif step.command in ("<host routing isolation checks>", "<host all-256 mask sweep>",
                                          "<host control operation checks>"):
                        text, elapsed = run_mask_checks(runner, args, record,
                                                       mode={"<host routing isolation checks>": "routing",
                                                             "<host all-256 mask sweep>": "sweep",
                                                             "<host control operation checks>": "controls"}[step.command])
                    else:
                        response = runner.command(step.command)
                        text, elapsed, completion = response.text, response.elapsed_s, response.completion
                        wire_text = response.wire_text
                    if step.command == "version":
                        args._firmware_version = text
                    status, notes = classify(text, step, completion)
                    if status != PASS and step.command.startswith("<host "):
                        notes += "; " + short_observed(text, limit=2000)
                    if status == PASS and step.command == "cfg":
                        addresses = re.findall(r"I2C address:\s+0x([0-9A-Fa-f]{2})\b", strip_ansi(text))
                        if len(addresses) != 1 or int(addresses[0], 16) != args.address:
                            status, notes = FAIL, "firmware address differs from --address"
                    if status == PASS and step.command == "scan":
                        addresses = {int(value, 16) for value in SCAN_ADDRESS_RE.findall(strip_ansi(text))}
                        if args.address not in addresses:
                            status, notes = FAIL, "configured mux address missing from scan"
                    if status == PASS and step.command in ("discover", "detect"):
                        addresses = {int(value, 16) for value in SCAN_ADDRESS_RE.findall(strip_ansi(text))}
                        targets = re.findall(r" target=0x([0-9A-Fa-f]{2}) target_ack=yes\b", strip_ansi(text))
                        if args.address not in addresses or [int(value, 16) for value in targets] != [args.address]:
                            status, notes = FAIL, "configured mux address missing or different in discovery"
                    if status == PASS and step.command == "console":
                        try:
                            current = compare_console(text, console_baseline)
                            console_baseline = current
                            notes = "console counters intact; accepted bytes do not prove terminal delivery or unobservable SDK RX loss"
                        except ValueError as exc:
                            status, notes = FAIL, str(exc)
                    if status == PASS and step.command == "health":
                        try:
                            health_baseline = compare_health(text, health_baseline)
                            notes = "complete READY snapshot; no new tracked failures since the initial health baseline"
                        except ValueError as exc:
                            status, notes = FAIL, str(exc)
                    if status == PASS and step.command == "version":
                        clean = strip_ansi(text)
                        if args.expect_commit:
                            commits = re.findall(r"^[ \t]*Firmware commit: ([0-9a-f]{40})[ \t]*$", clean, re.M)
                            if commits != [args.expect_commit]:
                                status, notes = FAIL, "firmware commit differs from --expect-commit"
                        if args.require_clean:
                            trees = re.findall(r"^[ \t]*Firmware tree: (\w+)[ \t]*$", clean, re.M)
                            if trees != ["clean"]:
                                status, notes = FAIL, "firmware was not built from a reported clean checkout"
                    record(f"\n$ {step.command}\n{wire_text or text}")
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
            # Failure between commands still retires the run, including after
            # the final command. An omission allowance must not hide it.
            fail_session(f"serial session interrupted or unavailable: {reason}")
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
    cfg_step = by_id["TCA-HIL-003"]
    cfg_prefix = ("=== Configuration ===\nI2C address: 0x70\nBound: yes\n"
                  "Initialized: yes\nController restart required: no\n"
                  "Config reference parity: yes\nSnapshot: OK\n")
    cfg_status, cfg_notes = classify(cfg_prefix + "I2C timeout: 20 ms\n", cfg_step)
    invalid_cfg = (
        "", "I2C timeout: 0 ms\n", "I2C timeout: 60001 ms\n",
        "I2C timeout: nan ms\n", "I2C timeout: 20 ms\nI2C timeout: 20 ms\n",
    )
    if (cfg_status != PASS or "firmware I2C timeout=20 ms" not in cfg_notes
            or any(classify(cfg_prefix + value, cfg_step)[0] != FAIL for value in invalid_cfg)):
        print("Parser self-test: FAIL - configuration timeout evidence")
        return 1
    oversized = "9" * 5000
    for command, response in (
        ("hil dry", f"hil\n[PASS] version\nHIL result: pass={oversized} fail=0 skip=3\n"),
        ("scan", f"Scan topology: OK active_mask=0x00\nScan complete: devices={oversized} errors=0\n"),
        ("stress 1", f"Stress results: completed={oversized} requested=1 status=OK safe_off=OK\n"),
    ):
        if classify(response, operation_step(command))[0] == PASS:
            print("Parser self-test: FAIL - oversized summary counter")
            return 1

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
    parser.add_argument("--dtr", type=int, choices=(0, 1), default=1,
                        help="DTR state set before opening (1 enables ESP32-S2 native CDC output)")
    parser.add_argument("--rts", type=int, choices=(0, 1), default=0,
                        help="RTS state set before opening; default 0 avoids a reset request")
    parser.add_argument("--address", type=parse_address, default=0x70,
                        help="expected firmware mux address; this does not reconfigure firmware")
    parser.add_argument("--target-env", default="esp32s3dev")
    parser.add_argument("--expect-commit", help="require this full source commit in firmware version output")
    parser.add_argument("--require-clean", action="store_true", help="require firmware to report a clean source checkout")
    parser.add_argument("--timeout-s", type=float, default=5.0)
    parser.add_argument("--scan-timeout-s", type=float, default=30.0)
    parser.add_argument("--stress-timeout-s", type=float, default=240.0)
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
    parser.add_argument("--output-dir", type=Path,
                        help="create a unique run directory with Markdown, JSON, transcript and hash manifest")
    parser.add_argument("--report", type=Path, help="new Markdown report file; existing files are never overwritten")
    parser.add_argument("--transcript", type=Path, help="new transcript file; existing files are never overwritten")
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
    args._argv = list(argv)
    if args.output_dir is not None and (args.report is not None or args.transcript is not None):
        parser.error("--output-dir cannot be combined with --report or --transcript")
    if args.expect_commit is not None:
        if re.fullmatch(r"[0-9a-fA-F]{40}", args.expect_commit) is None:
            parser.error("--expect-commit must be a full 40-character hexadecimal commit")
        args.expect_commit = args.expect_commit.lower()
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

    args._artifact_errors = []
    args._run = {
        "run_id": dt.datetime.now(dt.timezone.utc).strftime("tca9548a_%Y%m%dT%H%M%S_%fZ"),
        "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "argv": list(argv),
        "host_start": repository_provenance(),
        "python": platform.python_version(),
        "platform": platform.platform(),
    }
    try:
        args._run["runner_sha256"] = sha256_file(Path(__file__).resolve())
        paths = prepare_artifacts(args)
    except OSError as exc:
        print(f"Artifact preflight failed: {exc}", file=sys.stderr)
        for handle in getattr(args, "_artifact_handles", {}).values():
            with contextlib.suppress(OSError):
                handle.close()
        return 1

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

    args._run["finished_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    args._run["host_end"] = repository_provenance(paths.values())
    args._run["host_changed"] = args._run["host_start"] != args._run["host_end"]
    try:
        args._run["runner_sha256_end"] = sha256_file(Path(__file__).resolve())
        args._run["runner_changed"] = args._run["runner_sha256"] != args._run["runner_sha256_end"]
    except OSError as exc:
        artifact_error(args, f"runner source hash unavailable after run: {exc}")
    exit_code = result_exit_code(results, dry_run=args.dry_run, allow_not_run=args.allow_not_run)
    if args.report is not None:
        try:
            write_report(args.report, args, results, transcript_path)
            print(f"Report written: {args.report}")
        except OSError as exc:
            artifact_error(args, f"report write failed: {exc}")
            print(args._artifact_errors[-1], file=sys.stderr)
    if args._artifact_errors:
        exit_code = 1
    if args.output_dir is not None:
        try:
            write_bundle(args, results, paths, exit_code)
            print(f"Evidence directory: {args.report.parent}")
        except OSError as exc:
            artifact_error(args, f"evidence bundle write failed: {exc}")
            print(args._artifact_errors[-1], file=sys.stderr)
            exit_code = 1
            # Only this run created this path. A failed seal must not leave a
            # parseable PASS manifest, even when the bytes reached the file.
            with contextlib.suppress(OSError):
                args._artifact_handles[paths["manifest"].resolve()].close()
            with contextlib.suppress(OSError):
                paths["manifest"].unlink()
    for handle in args._artifact_handles.values():
        if not handle.closed:
            try:
                handle.close()
            except OSError as exc:
                print(f"Artifact close failed: {exc}", file=sys.stderr)
                exit_code = 1
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
