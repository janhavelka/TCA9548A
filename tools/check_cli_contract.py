#!/usr/bin/env python3
"""Check that firmware frameworks and native tests use the same example CLI."""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLI = ROOT / "examples/common/Tca9548aCli.cpp"
ADAPTERS = (
    ROOT / "examples/01_basic_bringup_cli/main.cpp",
    ROOT / "examples/espidf_basic/main/main.cpp",
)
COMMANDS = (
    "help", "?", "version", "ver", "color", "verbose", "bus", "console", "diag",
    "cfg", "settings", "snapshot", "addr", "timeout", "resettimeout", "offline",
    "begin", "init", "end", "unbind", "scan", "discover", "detect",
    "read", "dump", "select", "mask", "enable", "disable", "verify", "off",
    "recover", "reset", "hardreset", "invalidate", "health", "drv", "state",
    "online", "probe", "selftest", "hil", "hilmark", "sweep", "stress", "stress_mix",
    "watch", "soak", "stop", "cancel", "stats", "result", "xfer_stats",
    "counters", "xfer_reset", "xfer_assert",
)
PUBLIC_SURFACE = (
    "begin", "tick", "end", "probe", "recover", "hardReset", "selectChannel",
    "writeChannelMask", "disableAll", "readChannelMask", "getSettings", "state",
    "driverState", "isBound", "isInitialized", "isOnline", "getConfig",
    "lastOkMs", "lastErrorMs", "lastError", "consecutiveFailures", "totalFailures",
    "totalSuccess", "channelMaskObservation", "invalidateChannelMask",
)


def function_body(text: str, signature: str, errors: list[str]) -> str:
    # Skip declarations instead of borrowing the following function's body.
    definition = re.search(re.escape(signature) + r"[^;{}]*\{", text)
    if definition is not None:
        opening = definition.end() - 1
        depth = 0
        for index in range(opening, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    return text[opening + 1:index]
    errors.append(f"missing function body: {signature}")
    return ""


def main() -> int:
    errors: list[str] = []
    text = CLI.read_text(encoding="utf-8")
    dispatch = function_body(text, "void Cli::processCommand(", errors)
    help_body = function_body(text, "void Cli::printHelp(", errors)
    for command in COMMANDS:
        if f'"{command}"' not in dispatch:
            errors.append(f"shared dispatch missing command: {command}")
        if re.search(rf"(?<![A-Za-z0-9_]){re.escape(command)}(?![A-Za-z0-9_])", help_body) is None:
            errors.append(f"shared help missing command: {command}")
    for method in PUBLIC_SURFACE:
        if f"_device.{method}(" not in text:
            errors.append(f"shared CLI does not exercise public API: {method}")
    for token in ("MAX_COUNT = 1000", "MAX_SOAK_SECONDS = 86400",
                  "MAX_INTERVAL_MS = 60000", "%-32s", "Session transfers:",
                  "final verified mask restore", "hardReset seed readback",
                  "line parser rejects controls and overflow"):
        if token not in text:
            errors.append(f"shared CLI missing diagnostic contract: {token}")
    for relative in ("examples/common/Tca9548aCli.h", "examples/common/Tca9548aCli.cpp", "examples/common/ConsoleOutput.h"):
        source = (ROOT / relative).read_text(encoding="utf-8")
        if re.search(r'#\s*include\s*[<"](?:Arduino|Wire|driver/|esp_|freertos/)', source):
            errors.append(f"{relative} must remain framework-neutral")
    for path in ADAPTERS:
        source = path.read_text(encoding="utf-8")
        for token in ("Tca9548aCli.h", ".feed(", ".tick(", "prepareAddress", "probeAddress"):
            if token not in source:
                errors.append(f"{path.relative_to(ROOT)} missing shared adapter hook: {token}")
        if re.search(r"\bvoid\s+processCommand\s*\(", source):
            errors.append(f"{path.relative_to(ROOT)} duplicates the shared dispatcher")
    ini = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    arduino = ADAPTERS[0].read_text(encoding="utf-8")
    for token in ('#error "The Arduino example supports ESP32-S2 and ESP32-S3 only"',
                  '#error "The S2 example requires TinyUSB CDC on boot"'):
        if token not in arduino:
            errors.append(f"Arduino adapter lacks explicit USB target guard: {token}")
    cmake = (ROOT / "examples/espidf_basic/main/CMakeLists.txt").read_text(encoding="utf-8")
    if "examples/common/Tca9548aCli.cpp" not in ini or "../../common/Tca9548aCli.cpp" not in cmake:
        errors.append("both firmware builds must compile the shared CLI")
    if "[env:native_cli_commands]" not in ini or not (ROOT / "test/test_cli_commands/test_cli_commands.cpp").is_file():
        errors.append("shared command handlers lack their executable native test suite")
    workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    if "-e native_cli_commands" not in workflow:
        errors.append("CI does not execute the shared CLI command tests")
    if "[env:native_console]" not in ini or "-e native_console" not in workflow:
        errors.append("CI does not execute bounded console tests")
    if errors:
        print("CLI contract FAILED:\n" + "\n".join(f"- {error}" for error in errors))
        return 1
    print("CLI contract PASSED (shared Arduino, native ESP-IDF, and host commands)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
