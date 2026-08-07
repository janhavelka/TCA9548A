#!/usr/bin/env python3
"""Enforce the shared Arduino/native ESP-IDF TCA9548A CLI contract."""

from __future__ import annotations

import pathlib
import re
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

ARGUMENT_COMMANDS = ("select", "mask", "stress", "stress_mix")

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
    ".driverState(",
    ".isBound(",
    ".isInitialized(",
    ".isOnline(",
    ".getConfig(",
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
    "Config reference parity:",
    "State alias parity:",
    "probe no-health-side-effects",
    "capture original mask readback",
    "disableAll write",
    "disableAll readback",
    "all eight one-hot channels",
    "write mask 0xA5",
    "read mask 0xA5",
    "recover safe-off write",
    "recover readback 0x00",
    "hardReset exact-zero verification",
    "hardReset leaves verified all-off",
    "final verified mask restore",
    "Scan topology:",
    "active_mask=",
    "select a one-hot mask before scan to isolate a branch",
    "Snapshot:",
    "read: ",
    " mask=",
    "select %lu: ",
    "mask 0x%02lX: ",
    " (bound=%s)",
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


def function_body(
    text: str, signature: str, label: str, errors: list[str]
) -> str:
    start = text.find(signature)
    if start < 0:
        errors.append(f"{label} missing function: {signature}")
        return ""
    opening = text.find("{", start)
    if opening < 0:
        errors.append(f"{label} function has no body: {signature}")
        return ""
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1 : index]
    errors.append(f"{label} function has an unterminated body: {signature}")
    return ""


def require_commands(label: str, text: str, errors: list[str]) -> None:
    help_body = function_body(text, "void printHelp()", label, errors)
    dispatch_body = function_body(text, "void processCommand(", label, errors)
    for token in COMMAND_TOKENS:
        help_pattern = (
            re.escape(token)
            if token == "?"
            else rf"(?<![A-Za-z0-9_]){re.escape(token)}(?![A-Za-z0-9_])"
        )
        if re.search(help_pattern, help_body) is None:
            errors.append(f"{label} CLI help missing command: {token}")

        if token in ARGUMENT_COMMANDS:
            dispatch_pattern = (
                rf'parseUnsignedArgument\(\s*command,\s*"{re.escape(token)}"'
            )
        else:
            dispatch_pattern = (
                rf'(?:std::)?strcmp\(\s*command,\s*"{re.escape(token)}"\s*\)'
            )
        if re.search(dispatch_pattern, dispatch_body) is None:
            errors.append(f"{label} CLI dispatch missing command: {token}")


def require_executable_public_surface(
    label: str, text: str, entry_signature: str, errors: list[str]
) -> None:
    owner_signatures = (
        "void printObservation()",
        "void printConfig()",
        "void printHealth()",
        "void beginDriver()",
        "bool safeOffVerified()",
        "void scanBus()",
        "bool restoreMaskVerified(",
        "void runHil(",
        "void runStress(",
        "void processCommand(",
        entry_signature,
    )
    executable = "\n".join(
        function_body(text, signature, label, errors)
        for signature in owner_signatures
    )
    for token in PUBLIC_SURFACE:
        if token not in executable:
            errors.append(
                f"{label} CLI has no executable owner for public API: {token}"
            )

    run_hil = function_body(text, "void runHil(", label, errors)
    restore = function_body(text, "bool restoreMaskVerified(", label, errors)
    scan = function_body(text, "void scanBus()", label, errors)
    for token in (
        "readChannelMask(originalMask)",
        "index < TCA9548A::cmd::NUM_CHANNELS",
        "finishHilRestored(counts, originalMask)",
    ):
        if token not in run_hil:
            errors.append(f"{label} live selftest missing executable step: {token}")
    for token in ("writeChannelMask(originalMask)", "readChannelMask(observed)"):
        if token not in restore:
            errors.append(f"{label} live selftest restore missing step: {token}")
    if "readChannelMask(visibleMask)" not in scan:
        errors.append(f"{label} scan does not observe active topology")


def main() -> int:
    errors: list[str] = []
    arduino = read(ARDUINO_MAIN, errors)
    idf = read(IDF_MAIN, errors)

    for label, text, entry_signature in (
        ("Arduino", arduino, "void loop()"),
        ("ESP-IDF", idf, 'extern "C" void app_main(void)'),
    ):
        require_commands(label, text, errors)
        require_executable_public_surface(
            label, text, entry_signature, errors
        )
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
        ROOT / "examples" / "common" / "CliLineBuffer.h",
        ROOT / "examples" / "common" / "CliShell.h",
        ROOT / "examples" / "common" / "CliStyle.h",
    ):
        if not path.is_file():
            errors.append(f"missing Arduino shared CLI helper: {path.name}")
    if (ROOT / "examples" / "common" / "CommandHandler.h").exists():
        errors.append("deprecated parallel CommandHandler.h still exists")
    if (
        "cli_shell::pollLine" not in arduino
        or "cli::printPrompt" not in arduino
        or "FixedLineBuffer" not in read(
            ROOT / "examples" / "common" / "CliShell.h", errors
        )
    ):
        errors.append("Arduino CLI does not use the shared shell/style helpers")
    if "char command[128]" not in arduino:
        errors.append("Arduino CLI must retain fixed command storage")
    if "MAX_STRESS_COUNT = 1000" not in arduino:
        errors.append("Arduino CLI must retain the strict 1000-operation stress cap")
    for token in (
        "char line[LINE_LEN]",
        "pollConsoleLine",
        "FixedLineBuffer",
        "getchar()",
        "clearerr(stdin)",
    ):
        if token not in idf:
            errors.append(f"ESP-IDF CLI missing bounded input token: {token}")
    if "fgets(" in idf:
        errors.append("ESP-IDF CLI must not dispatch partial nonblocking fgets input")
    if "MAX_STRESS_COUNT = 1000" not in idf:
        errors.append("ESP-IDF CLI must retain the strict 1000-operation stress cap")
    for label, text in (("Arduino", arduino), ("ESP-IDF", idf)):
        for command in ("stress", "stress_mix"):
            pattern = (
                rf'parseUnsignedArgument\(\s*command,\s*"{command}",'
                rf'\s*MAX_STRESS_COUNT,\s*value\s*\)\s*&&\s*value\s*>\s*0U'
            )
            if re.search(pattern, text) is None:
                errors.append(
                    f"{label} {command} parser is not strictly bounded to 1..1000"
                )
        if "completed < count" not in text:
            errors.append(f"{label} stress loop is not count-bounded")
    tests = read(ROOT / "test" / "test_driver.cpp", errors)
    if "test_fixed_cli_line_buffer_is_trimmed_bounded_and_recoverable" not in tests:
        errors.append("shared fixed-line parser lacks its native regression test")

    if errors:
        return fail(errors)
    print("CLI contract PASSED (Arduino and native ESP-IDF parity)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
