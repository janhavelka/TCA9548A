#!/usr/bin/env python3
"""Statically validate the native ESP-IDF component and example boundary."""

from __future__ import annotations

import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
EXAMPLE = ROOT / "examples" / "espidf_basic"

REQUIRED_FILES = (
    ROOT / "CMakeLists.txt",
    ROOT / "idf_component.yml",
    EXAMPLE / "CMakeLists.txt",
    EXAMPLE / "main" / "CMakeLists.txt",
    EXAMPLE / "main" / "main.cpp",
    EXAMPLE / "README.md",
)

FORBIDDEN_TOKENS = (
    "ArduinoCompat",
    "IdfArduinoCompat",
    "Arduino.h",
    "Wire.h",
    "String",
    "Serial",
    "TwoWire",
    "examples/01_basic_bringup_cli/main.cpp",
)

REQUIRED_NATIVE_TOKENS = (
    'extern "C" void app_main(void)',
    "driver/i2c_master.h",
    "i2c_new_master_bus",
    "i2c_master_transmit",
    "i2c_master_receive",
    "esp_timer_get_time",
    "vTaskDelay",
    "fgets",
)


def main() -> int:
    errors: list[str] = []
    for path in REQUIRED_FILES:
        if not path.is_file():
            errors.append(f"missing required file: {path.relative_to(ROOT).as_posix()}")
    if errors:
        return report(errors)

    root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    manifest = (ROOT / "idf_component.yml").read_text(encoding="utf-8")
    metadata = json.loads((ROOT / "library.json").read_text(encoding="utf-8"))
    example_cmake = (EXAMPLE / "CMakeLists.txt").read_text(encoding="utf-8")
    main_cmake = (EXAMPLE / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
    main_cpp = (EXAMPLE / "main" / "main.cpp").read_text(encoding="utf-8")
    readme = (EXAMPLE / "README.md").read_text(encoding="utf-8")

    if "idf_component_register" not in root_cmake:
        errors.append("root CMakeLists.txt does not register an ESP-IDF component")
    if "src/TCA9548A.cpp" not in root_cmake or 'INCLUDE_DIRS "include"' not in root_cmake:
        errors.append("root component does not compile/expose the framework-neutral core")
    if "EXTRA_COMPONENT_DIRS" not in example_cmake:
        errors.append("example does not consume the repository as an external component")
    for dependency in (
        "TCA9548A",
        "esp_driver_gpio",
        "esp_driver_i2c",
        "esp_rom",
        "esp_system",
        "esp_timer",
        "freertos",
        "vfs",
    ):
        if dependency not in main_cmake:
            errors.append(f"example CMake missing dependency: {dependency}")

    version_match = re.search(r'^version:\s*"([^"]+)"\s*$', manifest, re.MULTILINE)
    if version_match is None:
        errors.append("idf_component.yml lacks a quoted version")
    elif version_match.group(1) != metadata.get("version"):
        errors.append("idf_component.yml version differs from library.json")
    if "espidf" not in metadata.get("frameworks", []):
        errors.append("library.json does not advertise the ESP-IDF framework")

    for token in FORBIDDEN_TOKENS:
        if token in main_cpp:
            errors.append(f"native ESP-IDF source contains forbidden token: {token}")
    for token in REQUIRED_NATIVE_TOKENS:
        if token not in main_cpp:
            errors.append(f"native ESP-IDF source missing token: {token}")
    for token in ("not hardware validation", "idf.py", "Arduino", "Wire"):
        if token.lower() not in readme.lower():
            errors.append(f"ESP-IDF README missing integration caveat: {token}")

    legacy = ROOT / "idf_component.yml.orig"
    if legacy.exists():
        errors.append("stale duplicate idf_component.yml.orig still exists")

    return report(errors) if errors else success()


def report(errors: list[str]) -> int:
    print("ESP-IDF example contract FAILED:")
    for error in errors:
        print(f"- {error}")
    return 1


def success() -> int:
    print("ESP-IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
