#!/usr/bin/env python3
"""Repository hygiene guard.

Durable properties only. Checks that no generated or one-time artifact is
tracked, that every local Markdown link resolves, that text files are valid
UTF-8 without mojibake, that the library core (include/ and src/) stays
framework-neutral, that the examples use the core enum-name helpers instead of
private string tables, and that CI still runs this guard.
"""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MARKDOWN_LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
MOJIBAKE_MARKERS = ("\u00c3", "\u00e2", "\ufffd")
FORBIDDEN_PREFIXES = (
    ".doxygen/",
    "docs/doxygen/",
    "docs/prompts/",
    "docs/reports/",
    "prompts/",
)
FORBIDDEN_SUFFIXES = (
    ".pyc",
    ".orig",
    ".rej",
    ".runner.md",
    ".serial.txt",
    ".transcript.txt",
)
TEXT_SUFFIXES = {
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
CORE_PREFIXES = ("include/", "src/")
EXAMPLE_CLIS = (
    "examples/01_basic_bringup_cli/main.cpp",
    "examples/espidf_basic/main/main.cpp",
)
CORE_NAME_HELPERS = ("errorName(", "driverStateName(", "maskProvenanceName(")
# A CLI that defines its own enum-to-string function drifts from the core table.
LOCAL_ENUM_MAPPER = re.compile(
    r"(?:const\s+char\s*\*)\s*"
    r"(?:errToStr|errName|stateToStr|stateName|provenanceToStr|provenanceName)"
    r"\s*\("
)
FRAMEWORK_INCLUDE = re.compile(
    r'^\s*#\s*include\s*[<"](?:Arduino\.h|Wire\.h|driver/|esp_|freertos/|sdkconfig)',
    re.MULTILINE,
)


def repository_files() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return sorted(
        line.strip().replace("\\", "/")
        for line in result.stdout.splitlines()
        if line.strip()
    )


def check_tracked_artifacts(files: list[str]) -> list[str]:
    errors: list[str] = []
    for relative in files:
        lower = relative.lower()
        name = pathlib.PurePosixPath(lower).name
        if lower.startswith(FORBIDDEN_PREFIXES):
            errors.append(f"generated/one-time path is tracked: {relative}")
        if lower.endswith(FORBIDDEN_SUFFIXES):
            errors.append(f"generated/duplicate artifact is tracked: {relative}")
        if "not-run" in name or "not_run" in name:
            errors.append(f"NOT-RUN-only artifact path is tracked: {relative}")
        if "prompt" in name and name.endswith((".md", ".txt")):
            errors.append(f"completed prompt-like artifact is tracked: {relative}")
    return errors


def check_encoding(files: list[str]) -> list[str]:
    errors: list[str] = []
    for relative in files:
        path = ROOT / relative
        if not path.is_file() or path.suffix.lower() not in TEXT_SUFFIXES:
            continue
        try:
            content = path.read_text(encoding="utf-8")
        except UnicodeDecodeError as exc:
            errors.append(f"{relative}: invalid UTF-8: {exc}")
            continue
        for marker in MOJIBAKE_MARKERS:
            if marker in content:
                errors.append(f"{relative}: contains mojibake marker {marker!r}")
                break
    return errors


def check_local_links(files: list[str]) -> list[str]:
    errors: list[str] = []
    for relative in files:
        source = ROOT / relative
        if not relative.endswith(".md") or not source.is_file():
            continue
        lines = source.read_text(encoding="utf-8").splitlines()
        for line_number, line in enumerate(lines, start=1):
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


def check_core_is_framework_neutral(files: list[str]) -> list[str]:
    errors: list[str] = []
    for relative in files:
        if not relative.startswith(CORE_PREFIXES):
            continue
        path = ROOT / relative
        if not path.is_file() or path.suffix.lower() not in {".h", ".hpp", ".cpp"}:
            continue
        match = FRAMEWORK_INCLUDE.search(path.read_text(encoding="utf-8"))
        if match is not None:
            errors.append(
                f"{relative}: framework include in library core: {match.group(0).strip()}"
            )
    return errors


def check_examples_use_core_names() -> list[str]:
    errors: list[str] = []
    for relative in EXAMPLE_CLIS:
        path = ROOT / relative
        if not path.is_file():
            errors.append(f"missing example CLI: {relative}")
            continue
        text = path.read_text(encoding="utf-8")
        for helper in CORE_NAME_HELPERS:
            if helper not in text:
                errors.append(f"{relative}: does not use core {helper}")
        if LOCAL_ENUM_MAPPER.search(text) is not None:
            errors.append(f"{relative}: defines a local enum-name mapper")
    return errors


def check_ci_runs_this_guard() -> list[str]:
    workflow = ROOT / ".github" / "workflows" / "ci.yml"
    if not workflow.is_file():
        return ["missing .github/workflows/ci.yml"]
    if "tools/check_repository_hygiene.py" not in workflow.read_text(encoding="utf-8"):
        return ["CI does not run the repository hygiene guard"]
    return []


def main() -> int:
    files = repository_files()
    errors: list[str] = []
    errors.extend(check_tracked_artifacts(files))
    errors.extend(check_encoding(files))
    errors.extend(check_local_links(files))
    errors.extend(check_core_is_framework_neutral(files))
    errors.extend(check_examples_use_core_names())
    errors.extend(check_ci_runs_this_guard())

    if errors:
        print("Repository hygiene guard FAILED:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("Repository hygiene guard PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
