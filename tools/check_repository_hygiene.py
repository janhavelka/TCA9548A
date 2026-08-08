#!/usr/bin/env python3
"""Enforce durable public naming and repository-hygiene decisions."""

from __future__ import annotations

import json
import pathlib
import re
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
MARKDOWN_LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
PLATFORMIO_COMMAND = re.compile(r"(?m)^\s*python\s+-m\s+platformio\b")
MOJIBAKE_MARKERS = ("\u00c3", "\u00e2", "\ufffd")
FORBIDDEN_PREFIXES = (".doxygen/", "docs/doxygen/", "docs/prompts/", "prompts/")
FORBIDDEN_SUFFIXES = (
    ".pyc",
    ".runner.md",
    ".serial.txt",
    ".transcript.txt",
)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def repository_files() -> set[str]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return {
        line.strip().replace("\\", "/")
        for line in result.stdout.splitlines()
        if line.strip()
    }


def check_local_links(files: set[str]) -> list[str]:
    errors: list[str] = []
    for relative in sorted(path for path in files if path.endswith(".md")):
        source = ROOT / relative
        if not source.is_file():
            continue
        for line_number, line in enumerate(
            source.read_text(encoding="utf-8").splitlines(), start=1
        ):
            for match in MARKDOWN_LINK.finditer(line):
                target = match.group(1).strip().strip("<>").split("#", 1)[0]
                if not target or "://" in target or target.startswith("mailto:"):
                    continue
                target = target.split(maxsplit=1)[0]
                if not (source.parent / target).resolve().exists():
                    errors.append(
                        f"{relative}:{line_number}: missing local link: {target}"
                    )
    return errors


def check_artifacts_and_encoding(files: set[str]) -> list[str]:
    errors: list[str] = []
    text_suffixes = {
        ".c",
        ".cc",
        ".cpp",
        ".h",
        ".hpp",
        ".ini",
        ".json",
        ".md",
        ".py",
        ".txt",
        ".yml",
        ".yaml",
    }
    for relative in sorted(files):
        lower = relative.lower()
        name = pathlib.PurePosixPath(lower).name
        if lower.startswith(FORBIDDEN_PREFIXES):
            errors.append(f"generated/one-time path is tracked: {relative}")
        if lower.endswith(FORBIDDEN_SUFFIXES):
            errors.append(f"generated/duplicate artifact is tracked: {relative}")
        if "not-run" in name or "not_run" in name:
            errors.append(f"NOT-RUN-only artifact path is tracked: {relative}")
        if "prompt" in name and pathlib.PurePosixPath(lower).suffix in {
            ".md",
            ".txt",
        }:
            errors.append(f"completed prompt-like artifact is tracked: {relative}")
        path = ROOT / relative
        if path.is_file() and path.suffix.lower() in text_suffixes:
            try:
                content = path.read_text(encoding="utf-8")
            except UnicodeDecodeError as exc:
                errors.append(f"{relative}: invalid UTF-8: {exc}")
                continue
            for marker in MOJIBAKE_MARKERS:
                if marker in content:
                    errors.append(
                        f"{relative}: contains mojibake marker {marker!r}"
                    )
                    break
    return errors


def main() -> int:
    errors: list[str] = []
    files = repository_files()
    status = read("include/TCA9548A/Status.h")
    public = read("include/TCA9548A/TCA9548A.h")
    core = read("src/TCA9548A.cpp")
    arduino = read("examples/01_basic_bringup_cli/main.cpp")
    idf = read("examples/espidf_basic/main/main.cpp")
    tests = read("test/test_driver.cpp")
    hil = read("tools/tca9548a_hil.py")
    ci = read(".github/workflows/ci.yml")
    doxyfile = read("Doxyfile")
    gitignore = read(".gitignore")
    component = read("idf_component.yml")
    manifest = json.loads(read("library.json"))

    for token in (
        "enum class Err : uint8_t",
        "constexpr const char* errorName(Err error)",
        "constexpr const char* toString(Err error)",
        "struct Status",
    ):
        if token not in status:
            errors.append(f"Status.h missing compatibility contract: {token}")

    for token in (
        "enum class DriverState : uint8_t",
        "constexpr const char* driverStateName(DriverState state)",
        "constexpr const char* toString(DriverState state)",
        "enum class MaskProvenance : uint8_t",
        "constexpr const char* maskProvenanceName(MaskProvenance provenance)",
        "constexpr const char* toString(MaskProvenance provenance)",
        "DriverState state() const",
        "DriverState driverState() const",
        "bool isBound() const",
        "bool isInitialized() const",
        "bool isOnline() const",
        "uint32_t lastOkMs() const",
        "uint32_t lastErrorMs() const",
        "Status lastError() const",
        "uint8_t consecutiveFailures() const",
        "uint32_t totalFailures() const",
        "uint32_t totalSuccess() const",
        "ChannelMaskObservation channelMaskObservation() const",
    ):
        if token not in public:
            errors.append(f"public naming/accessor contract missing: {token}")

    for token in (
        "_i2cWriteRaw",
        "_i2cWriteReadRaw",
        "_i2cWriteTracked",
        "_i2cWriteReadTracked",
        "_updateHealth",
        "_writeControlByte",
        "_readControlByte",
        "_readControlByteRaw",
        "_recordMask",
        "_resetBindingState",
    ):
        if token not in public or token not in core:
            errors.append(f"private transport/cache layer missing: {token}")

    if "resetHealth" in public or "clearHealth" in public:
        errors.append("unjustified public health-reset surface was added")

    for cli_name, cli in (("Arduino", arduino), ("ESP-IDF", idf)):
        for token in ("errorName(", "driverStateName(", "maskProvenanceName("):
            if token not in cli:
                errors.append(f"{cli_name} CLI does not use core {token}")
        local_mapper = re.compile(
            r"(?:const\s+char\s*\*)\s*"
            r"(?:errToStr|errName|stateToStr|stateName|provenanceToStr|"
            r"provenanceName)\s*\("
        )
        if local_mapper.search(cli):
            errors.append(f"{cli_name} CLI restored a local enum-name mapper")

    if (
        "void test_status_and_transport_helpers()" not in tests
        or "RUN_TEST(test_status_and_transport_helpers);" not in tests
        or "static_cast<Err>(0xFFU)" not in tests
        or "static_cast<DriverState>(0xFFU)" not in tests
        or "static_cast<MaskProvenance>(0xFFU)" not in tests
    ):
        errors.append("enum-name/invalid-cast regression is missing or unregistered")

    removed_tokens = (
        "static constexpr int LED",
        "inline bool readLine(",
        "log_bool_str",
        "LOG_COLOR_RESULT",
        "LOG_COLOR_STATE",
        "LOG_COLOR_BLUE",
        "LOGD(",
        "LOGT(",
        "LOGV(",
    )
    example_helpers = "\n".join(
        read(relative)
        for relative in (
            "examples/common/BoardConfig.h",
            "examples/common/CliShell.h",
            "examples/common/Log.h",
        )
    )
    for token in removed_tokens:
        if token in example_helpers:
            errors.append(f"zero-call example helper was restored: {token}")

    for relative in ("README.md", "CONTRIBUTING.md", "docs/PORTING.md"):
        document = read(relative)
        if PLATFORMIO_COMMAND.search(document):
            errors.append(f"{relative} bypasses the Windows PlatformIO wrapper")
        if ".\\scripts\\pio.cmd" not in document:
            errors.append(f"{relative} does not name .\\scripts\\pio.cmd")

    if 'platformio_tool_args("--version")' not in hil:
        errors.append("HIL metadata does not use the platform-aware PIO command")
    if 'ROOT / "scripts" / "pio.cmd"' not in hil:
        errors.append("HIL runner does not resolve the Windows PIO wrapper")
    if re.search(r"[f]?['\"]pio run\b", hil) or "['pio', '--version']" in hil:
        errors.append("HIL runner restored a bare Windows pio invocation")
    if "Library: 1.1." in hil:
        errors.append("HIL parser fixture embeds a stale library version")
    if 'allow_empty=True' not in hil:
        errors.append("HIL report cannot distinguish a clean Git status")
    for token in (
        'parser.add_argument("--report", type=Path)',
        'parser.add_argument("--transcript", type=Path)',
        "if args.report is not None:",
    ):
        if token not in hil:
            errors.append(f"HIL evidence output is not explicitly opt-in: {token}")

    description = str(manifest.get("description", ""))
    if "production-grade" in description.lower():
        errors.append("library.json implies unproven production readiness")
    if "production-grade" in component.lower():
        errors.append("idf_component.yml implies unproven production readiness")
    version = str(manifest.get("version", ""))
    security = read("SECURITY.md")
    if f"The `{version}` manifest is currently staged" not in security:
        errors.append("SECURITY.md staged manifest is stale against library.json")
    if f"| Staged {version} development | No |" not in security:
        errors.append("SECURITY.md staged-version table is stale")

    report = "docs/NAMING_HYGIENE.md"
    if report not in files or not (ROOT / report).is_file():
        errors.append("durable naming/hygiene report is missing")
    export = manifest.get("export", {}).get("include", [])
    for relative in (report, "tools/check_repository_hygiene.py"):
        if relative not in export:
            errors.append(f"package export omits hygiene artifact: {relative}")
    if "docs/NAMING_HYGIENE.md" not in doxyfile:
        errors.append("Doxygen input omits the naming/hygiene report")
    if f"Target version: {version}" not in read(report):
        errors.append("naming/hygiene report target version is stale")
    if re.search(r"(?m)^OUTPUT_DIRECTORY\s*=\s*\.doxygen\s*$", doxyfile) is None:
        errors.append("Doxygen output is not owned by root-local .doxygen/")
    if ".doxygen/" not in gitignore or "docs/doxygen/" in gitignore:
        errors.append(".gitignore does not match the Doxygen output owner")
    for relative in ("README.md", "CONTRIBUTING.md"):
        if "docs/doxygen/" in read(relative):
            errors.append(f"{relative} documents the obsolete Doxygen output path")
    if "python tools/check_repository_hygiene.py" not in ci:
        errors.append("CI does not run the repository hygiene guard")
    checkout_pin = (
        "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 "
        "# v7.0.1"
    )
    if ci.count(checkout_pin) != 3:
        errors.append("CI checkout jobs are not all pinned to audited v7.0.1")

    errors.extend(check_artifacts_and_encoding(files))
    errors.extend(check_local_links(files))

    if errors:
        print("Repository hygiene guard FAILED:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("Repository hygiene guard PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
