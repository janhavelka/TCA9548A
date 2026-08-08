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
7. Run the validation below, then commit with a clear message and open a pull
   request.

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
- Do not commit generated `docs/doxygen/` output.

## Local Validation

Run the repository checks from its root:

On Windows, use the checked-in wrapper so these commands resolve the existing
VS Code-managed PlatformIO installation:

```powershell
python scripts/generate_version.py check
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python tools/check_repository_hygiene.py
.\scripts\pio.cmd test -e native
.\scripts\pio.cmd run -e native_core_no_arduino
.\scripts\pio.cmd run -e esp32s3dev
.\scripts\pio.cmd run -e esp32s2dev
python tools/tca9548a_hil.py --parser-self-test
doxygen Doxyfile
.\scripts\pio.cmd pkg pack . --output .pio\TCA9548A.tar.gz
git diff --check
```

Linux CI invokes its separately installed, pinned Core with
`python -m platformio`; do not copy that CI-only path into Windows workflows.

With ESP-IDF 5.4 or 5.5 installed, build `examples/espidf_basic` for both
`esp32s2` and `esp32s3`. CI performs those native-IDF builds even when a local
ESP-IDF installation is unavailable.

The CI workflow additionally compiles the framework-neutral core with strict
C++17 warnings. CI pins PlatformIO and Doxygen versions; when local tool
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
