# Contributing

Contributions are welcome when they preserve the driver's small ownership
boundary and deterministic transport contract. Read [AGENTS.md](AGENTS.md)
before changing library behavior; it is the binding engineering guidance for
this repository.

## Development Workflow

1. Fork the repository and create a focused branch.
2. Inspect the current implementation and tests before adding a new owner,
   abstraction, file, or dependency.
3. Make the smallest coherent change and preserve unrelated local work.
4. Add or update tests for behavioral changes.
5. Update public API comments and focused Markdown documentation in the same
   change.
6. Add a concise entry under `[Unreleased]` in [CHANGELOG.md](CHANGELOG.md).
7. After each completed prompt or logical block, run the relevant validation,
   commit the scoped changes, and push the working branch to its upstream.
   Check CI for that commit and fix failures in follow-up commits. Use a pull
   request when the branch or repository workflow requires one.

## Engineering Expectations

- Follow the existing formatting and naming conventions in nearby code.
- Prefer `constexpr` constants over macros except for conditional compilation
  and generated build overrides.
- Keep the library core framework-neutral: no `Wire`, Arduino, ESP-IDF,
  FreeRTOS, logging, or board-pin ownership in `include/` or `src/`.
- Do not allocate, wait, retry, queue, or loop without a documented finite
  bound in steady-state library paths.
- Preserve the injected transport boundary and distinct transport errors.
- Do not add product topology, scheduling, retry, recovery, or admission policy
  to the chip driver.
- Treat public enum numeric values and callback signatures as compatibility
  contracts.

## Documentation Expectations

- Public declarations in `include/TCA9548A/` require complete Doxygen comments,
  including parameters, return values, ownership, side effects, and I/O bounds
  where relevant.
- `library.json` is the version source of truth. Never edit generated
  `include/TCA9548A/Version.h`, `PROJECT_NUMBER` in `Doxyfile`, or the version
  field in `idf_component.yml` directly.
- Keep [README.md](README.md) task-oriented. Put electrical details in
  [docs/HARDWARE_NOTES.md](docs/HARDWARE_NOTES.md) and adapter details in
  [docs/PORTING.md](docs/PORTING.md).
- Do not commit completed task prompts, superseded audit working notes, or
  dry-run reports as product documentation. Move durable conclusions into the
  current owner document. Retain a HIL report only when it contains useful live
  fixture evidence for a release or investigation.
- Do not commit generated `.doxygen/` or legacy `docs/doxygen/` output.

## Local Validation

Run these checks from the root of a Git checkout. The distributed library
archive contains the core, examples, and live HIL runner; it deliberately omits
development tests and repository checkers.

On Windows, use the checked-in wrapper so these commands resolve the existing
VS Code-managed PlatformIO installation:

```powershell
python scripts/generate_version.py check
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python tools/check_repository_hygiene.py
.\scripts\pio.cmd test -e native
.\scripts\pio.cmd test -e native_cli
.\scripts\pio.cmd test -e native_transport
.\scripts\pio.cmd run -e native_core_no_arduino
cmake -S test/core_no_arduino -B .pio/cmake-core -G Ninja
cmake --build .pio/cmake-core
cmake -E chdir .pio/cmake-core ctest --output-on-failure
.\scripts\pio.cmd run -e esp32s3dev
.\scripts\pio.cmd run -e esp32s2dev
python tools/tca9548a_hil.py --parser-self-test
python tools/test_tca9548a_hil.py
doxygen Doxyfile
.\scripts\pio.cmd pkg pack . --output .pio\TCA9548A.tar.gz
python tools/check_package.py .pio\TCA9548A.tar.gz
git diff --check
```

Linux CI invokes its separately installed, pinned Core with
`python -m platformio`; do not copy that CI-only path into Windows workflows.

If the Windows build cannot find `xtensa-esp32s2-elf-g++` or
`xtensa-esp32s3-elf-g++`, first check that the executable exists in the managed
installation's `packages/toolchain-xtensa-esp-elf/bin` directory. If it does,
add that directory to the current shell's PATH and rerun the wrapper. Do not
install a second Core or change the system-wide PATH to work around it.

With ESP-IDF 5.4 or 5.5 installed, follow the
[native example build commands](examples/espidf_basic/README.md) for both
`esp32s2` and `esp32s3`. CI builds both targets with pinned SDK versions 5.4.4
and 5.5.5, even when a local ESP-IDF installation is unavailable.

The package checker verifies archive contents, then builds and runs a separate
native application from that archive with strict dependency compatibility.
It uses the existing Windows wrapper and retains its isolated project under
`.pio/package-check/` for inspection. No repository include paths are used by
the consumer. A successful package command alone does not prove this.

The CI workflow additionally compiles the framework-neutral core with strict
C++17 warnings and builds a standalone CMake consumer to verify the target's
public include path and language requirement. CMake validation requires CMake
3.16 or later and an available C++ compiler; use an appropriate installed
generator if Ninja is unavailable. CI pins PlatformIO and Doxygen versions; when local tool
versions differ, passing CI with the pinned versions is authoritative.

A parser self-test or dry run is not hardware evidence. Live HIL requires the
documented fixture and must not use `--skip-reset` for release evidence.

## Pull Requests

- Keep one coherent feature, fix, or documentation objective per pull request.
- Explain ownership, timing, memory, error, and compatibility effects when they
  change.
- Include exact validation results and identify tests that were not run.
- Never describe a dry-run, parser test, target build, or unavailable fixture
  as live hardware validation.
- Use conventional prefixes such as `feat:`, `fix:`, `docs:`, `refactor:`,
  `test:`, `build:`, or `chore:` for ordinary commits.

## Maintainer Release Checklist

1. Choose the SemVer change and run
   `python scripts/generate_version.py set X.Y.Z`.
2. Finalize the changelog and all affected user/API documentation.
3. Run the complete validation above and review the package contents.
4. Commit with subject `Release vX.Y.Z`, push the candidate, and require branch
   CI to pass.
5. Create and push an annotated `vX.Y.Z` tag at that exact commit; never move a
   published tag.
6. Require tag CI to pass, then record the tag object and peeled commit SHA in
   release evidence.
7. Downstream products verify the tag and pin the peeled 40-character commit
   SHA, never a floating branch or tag name.

Security reports do not belong in public issues; follow
[SECURITY.md](SECURITY.md).
