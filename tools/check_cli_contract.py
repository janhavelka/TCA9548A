#!/usr/bin/env python3
"""Enforce the shared Arduino/native ESP-IDF TCA9548A CLI contract."""

from __future__ import annotations

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
ARDUINO_MAIN = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
IDF_MAIN = ROOT / "examples" / "espidf_basic" / "main" / "main.cpp"

COMMAND_TOKENS = (
    "help",
    "?",
    "version",
    "ver",
    "cfg",
    "health",
    "drv",
    "state",
    "read",
    "dump",
    "select",
    "mask",
    "off",
    "probe",
    "recover",
    "reset",
    "hardreset",
    "invalidate",
    "begin",
    "end",
    "scan",
    "stress",
    "stress_mix",
    "selftest",
    "hil",
)

PUBLIC_SURFACE = (
    ".begin(",
    ".tick(",
    ".end(",
    ".probe(",
    ".recover(",
    ".hardReset(",
    ".selectChannel(",
    ".writeChannelMask(",
    ".disableAll(",
    ".readChannelMask(",
    ".getSettings(",
    ".state(",
    ".isBound(",
    ".isInitialized(",
    ".isOnline(",
    ".lastOkMs(",
    ".lastErrorMs(",
    ".lastError(",
    ".consecutiveFailures(",
    ".totalFailures(",
    ".totalSuccess(",
    ".channelMaskObservation(",
    ".invalidateChannelMask(",
)

PARITY_OUTPUTS = (
    "Bound:",
    "Initialized:",
    "I2C address:",
    "I2C timeout:",
    "RESET timeout:",
    "nowMs hook:",
    "RESET callback:",
    "Offline threshold:",
    "probe no-health-side-effects",
    "disableAll write",
    "disableAll readback",
    "select CH3",
    "select CH3 readback",
    "write mask 0xA5",
    "read mask 0xA5",
    "recover safe-off write",
    "recover readback 0x00",
    "hardReset exact-zero verification",
    "hardReset leaves verified all-off",
    "final verified safe-off",
)


def fail(messages: list[str]) -> int:
    print("CLI contract FAILED:")
    for message in messages:
        print(f"- {message}")
    return 1


def read(path: pathlib.Path, errors: list[str]) -> str:
    if not path.is_file():
        errors.append(f"missing CLI source: {path.relative_to(ROOT).as_posix()}")
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def main() -> int:
    errors: list[str] = []
    arduino = read(ARDUINO_MAIN, errors)
    idf = read(IDF_MAIN, errors)

    for label, text in (("Arduino", arduino), ("ESP-IDF", idf)):
        for token in COMMAND_TOKENS:
            if f'"{token}"' not in text and f" {token}" not in text:
                errors.append(f"{label} CLI missing command token: {token}")
        for token in PUBLIC_SURFACE:
            if token not in text:
                errors.append(f"{label} CLI does not expose public API: {token}")
        for token in PARITY_OUTPUTS:
            if token not in text:
                errors.append(f"{label} CLI missing parity output/check: {token}")
        color_tokens = (
            ("cli::printSection", "cli::resultColor")
            if label == "Arduino"
            else ("COLOR_CYAN", "COLOR_GREEN", "COLOR_RED")
        )
        if any(token not in text for token in color_tokens):
            errors.append(f"{label} CLI missing shared color formatting")
        for token in ("=== Version Info ===", "=== Configuration ===", "=== Driver Health ==="):
            title = token.removeprefix("=== ").removesuffix(" ===")
            if title not in text:
                errors.append(f"{label} CLI missing formatted section: {token}")

    for path in (
        ROOT / "examples" / "common" / "CliShell.h",
        ROOT / "examples" / "common" / "CliStyle.h",
    ):
        if not path.is_file():
            errors.append(f"missing Arduino shared CLI helper: {path.name}")
    if (ROOT / "examples" / "common" / "CommandHandler.h").exists():
        errors.append("deprecated parallel CommandHandler.h still exists")
    if "cli_shell::readLine" not in arduino or "cli::printPrompt" not in arduino:
        errors.append("Arduino CLI does not use the shared shell/style helpers")
    if "char command[128]" not in arduino:
        errors.append("Arduino CLI must retain fixed command storage")
    if "char line[LINE_LEN]" not in idf or "fgets" not in idf:
        errors.append("ESP-IDF CLI must retain fixed command storage")

    if errors:
        return fail(errors)
    print("CLI contract PASSED (Arduino and native ESP-IDF parity)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
