#!/usr/bin/env python3
"""Check exported files, then build/run a native consumer of the packed archive.

Run from a source checkout after `pio pkg pack`. Build outputs are retained in
a unique ignored directory under .pio/package-check (or --work-dir beneath
the repository's .pio directory). No files are extracted by this checker.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MAX_ARCHIVE_BYTES = 8 * 1024 * 1024
MAX_CONTENT_BYTES = 8 * 1024 * 1024
MAX_MEMBERS = 512
BUILD_TIMEOUT_S = 600
REQUIRED_FILES = (
    "AGENTS.md",
    "CHANGELOG.md",
    "CMakeLists.txt",
    "CONTRIBUTING.md",
    "Doxyfile",
    "LICENSE",
    "README.md",
    "SECURITY.md",
    "docs/FEATURE_MATRIX.md",
    "docs/HARDWARE_NOTES.md",
    "docs/PORTING.md",
    "docs/VALIDATION_STATUS.md",
    "examples/01_basic_bringup_cli/main.cpp",
    "examples/espidf_basic/CMakeLists.txt",
    "examples/espidf_basic/README.md",
    "examples/espidf_basic/main/CMakeLists.txt",
    "examples/espidf_basic/main/main.cpp",
    "idf_component.yml",
    "library.json",
    "platformio.ini",
    "scripts/generate_version.py",
    "scripts/pio.cmd",
    "tools/tca9548a_hil.py",
)
REQUIRED_GLOBS = (
    "include/TCA9548A/*.h",
    "src/*.cpp",
    "examples/common/*.h",
)


def expected_files() -> dict[str, bytes]:
    names = set(REQUIRED_FILES)
    for pattern in REQUIRED_GLOBS:
        paths = sorted(ROOT.glob(pattern))
        if not paths:
            raise ValueError(f"source pattern has no files: {pattern}")
        names.update(path.relative_to(ROOT).as_posix() for path in paths)
    return {name: (ROOT / name).read_bytes() for name in sorted(names)}


def check_archive(archive: Path) -> int:
    expected = expected_files()
    directories = {
        parent.as_posix()
        for name in expected
        for parent in PurePosixPath(name).parents
        if parent != PurePosixPath(".")
    }
    seen: set[str] = set()
    files: set[str] = set()
    total_bytes = 0
    with tarfile.open(archive, "r:gz") as package:
        for index, member in enumerate(package):
            if index >= MAX_MEMBERS:
                raise ValueError("archive has too many members")
            name = member.name.rstrip("/") if member.isdir() else member.name
            path = PurePosixPath(name)
            parts = name.split("/")
            if (
                not name or path.is_absolute() or "\\" in name or ":" in name
                or any(part in {"", ".", ".."} for part in parts)
                or any(part.endswith((" ", ".")) for part in parts)
                or any(ord(character) < 32 for character in name)
            ):
                raise ValueError(f"unsafe archive path: {member.name!r}")
            if name.casefold() in seen:
                raise ValueError(f"duplicate archive path: {name}")
            seen.add(name.casefold())
            if member.isdir():
                if name not in directories:
                    raise ValueError(f"unexpected archive directory: {name}")
                continue
            if not member.isfile() or member.issparse():
                raise ValueError(f"archive links/special files are forbidden: {name}")
            if name not in expected:
                raise ValueError(f"unexpected exported file: {name}")
            total_bytes += member.size
            if member.size < 0 or total_bytes > MAX_CONTENT_BYTES:
                raise ValueError("archive content exceeds its size limit")
            stream = package.extractfile(member)
            if stream is None:
                raise ValueError(f"cannot read archive member: {name}")
            with stream:
                actual = stream.read(MAX_CONTENT_BYTES + 1)
            if len(actual) != member.size:
                raise ValueError(f"invalid archive member length: {name}")
            # PlatformIO currently preserves metadata bytes. Semantic comparison
            # also accepts harmless JSON formatting from another Core release.
            same = (
                json.loads(actual) == json.loads(expected[name])
                if name == "library.json" else actual == expected[name]
            )
            if not same:
                raise ValueError(f"exported file differs from source: {name}")
            files.add(name)
    missing = sorted(expected.keys() - files)
    if missing:
        raise ValueError("missing exported files: " + ", ".join(missing))
    print(f"Package contents: {len(files)} source-matched files", flush=True)
    return len(files)


def create_work_directory(parent: Path) -> Path:
    allowed = ROOT / ".pio"
    if allowed.resolve() != allowed:
        raise ValueError("repository .pio directory must not redirect outside the checkout")
    parent = parent.resolve()
    if not parent.is_relative_to(allowed):
        raise ValueError("--work-dir must be inside the repository's .pio directory")
    parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=parent)).resolve()
    print(f"Package check outputs retained at: {work}", flush=True)
    return work


def build_consumer(work: Path, archive: Path) -> None:
    source = work / "src"
    source.mkdir()
    (source / "main.cpp").write_bytes(
        (ROOT / "test/core_no_arduino/compile_main.cpp").read_bytes()
    )
    (work / "platformio.ini").write_text(
        "[env:consumer]\n"
        "platform = platformio/native@1.2.1\n"
        "framework =\n"
        "build_flags = -std=c++17\n"
        "lib_compat_mode = strict\n"
        f"lib_deps = {archive.as_posix()}\n",
        encoding="utf-8",
    )
    if os.name == "nt":
        environment = os.environ.copy()
        environment["TCA9548A_PACKAGE_PIO_WRAPPER"] = str(ROOT / "scripts/pio.cmd")
        environment["TCA9548A_PACKAGE_PROJECT"] = str(work)
        # Expand quoted environment values once, with delayed expansion off.
        # This avoids treating spaces/metacharacters in paths as shell syntax.
        # A string preserves cmd's required outer quotes (list2cmdline does not).
        command = (
            f'"{os.environ.get("COMSPEC", "cmd.exe")}" /d /v:off /s /c '
            '""%TCA9548A_PACKAGE_PIO_WRAPPER%" run '
            '--project-dir "%TCA9548A_PACKAGE_PROJECT%" -e consumer"'
        )
        subprocess.run(command, cwd=work, env=environment, check=True,
                       timeout=BUILD_TIMEOUT_S)
    else:
        subprocess.run(
            [sys.executable, "-m", "platformio", "run", "--project-dir",
             str(work), "-e", "consumer"],
            cwd=work, check=True, timeout=BUILD_TIMEOUT_S,
        )
    program = work / ".pio/build/consumer" / (
        "program.exe" if os.name == "nt" else "program"
    )
    subprocess.run([str(program)], cwd=work, check=True, timeout=10)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="PlatformIO .tar.gz package")
    parser.add_argument("--work-dir", type=Path, default=ROOT / ".pio/package-check",
                        help="scratch parent beneath this checkout's .pio directory")
    args = parser.parse_args()
    try:
        # Validate and consume the same private copy even if the input is rebuilt.
        with args.archive.open("rb") as source:
            packed = source.read(MAX_ARCHIVE_BYTES + 1)
        if len(packed) > MAX_ARCHIVE_BYTES:
            raise ValueError("compressed archive exceeds its size limit")
        work = create_work_directory(args.work_dir)
        archive = work / "TCA9548A.tar.gz"
        archive.write_bytes(packed)
        check_archive(archive)
        build_consumer(work, archive)
    except (OSError, ValueError, tarfile.TarError, subprocess.SubprocessError) as error:
        print(f"Package check FAILED: {error}", file=sys.stderr)
        return 1
    print("Package check PASSED: archive content and isolated consumer", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
