# TCA9548A HIL Validation Report - COM8 - 2026-06-29

## Metadata

- Date/time: `2026-06-29T16:58:15+02:00`
- Timezone: `Central Europe Daylight Time`
- Repository path: `C:\Users\Honza\Documents\Projects\TCA9548A`
- Branch: `main`
- Commit: `b943d19a98bc086986d31ace8a642a831aa7fed7`
- Dirty status: `dirty before report generation or local edits present`
- Operating system: `Windows-11-10.0.26200-SP0`
- Python: `3.12.10`
- PlatformIO: `PlatformIO Core, version 6.1.18`
- Target environment: `esp32s3dev`
- Serial port: `COM8 (not opened in dry-run mode)`
- Baud rate: `115200`
- Device identity/address: `TCA9548A at configured address 0x70, not detected in this run`

## Hardware Setup

- Fixture: No board with TCA9548A attached; live HIL was intentionally skipped.
- Wiring: not verified in this run.
- Electrical limits: no live electrical tests were performed.
- Safety assumption: no board with this chip is attached, so live HIL, flash, reset, and soak steps are marked `NOT_RUN`.

## Exact Commands

```powershell
python tools\tca9548a_hil.py --parser-self-test
python tools\tca9548a_hil.py --dry-run --port COM8 --baud 115200
pio run -e esp32s3dev
pio run -e esp32s3dev -t upload --upload-port COM8
python tools\tca9548a_hil.py --port COM8 --baud 115200 --timeout-s 5.0
```

Report generation command: `C:\Users\Honza\AppData\Local\Programs\Python\Python312\python.exe tools\tca9548a_hil.py --dry-run --port COM8 --baud 115200 --report docs\reports\hil-validation-COM8-20260629.md --fix-note "Added host-side HIL runner with parser self-test, dry-run planning, bounded serial execution, and Markdown report output." --fix-note "Bounded the example CLI command buffer and discarded overlong serial lines until newline." --fix-note "Tightened example stress, selftest, and channel-scan commands to read and restore the original mux mask explicitly." --verification-result "python tools\\tca9548a_hil.py --parser-self-test: PASS" --verification-result "python tools\\tca9548a_hil.py --dry-run --port COM8 --baud 115200: PASS" --verification-result "python scripts\\generate_version.py check: PASS" --verification-result "pio test -e native: PASS, 70 tests" --verification-result "pio run -e native_core_no_arduino: PASS" --verification-result "pio run -e esp32s3dev: PASS" --verification-result "pio run -e esp32s2dev: PASS" --verification-result "doxygen Doxyfile: PASS, generated docs removed" --verification-result "pio pkg pack: PASS, generated tarball removed" --verification-result "git diff --check: PASS" --verification-result "CI: no local .github workflows and no GitHub workflow runs for HEAD b943d19a98bc086986d31ace8a642a831aa7fed7"`

## Summary

| PASS | FAIL | UNKNOWN | NOT_RUN |
|------|------|---------|---------|
| 0 | 0 | 0 | 8 |

## Detailed Results

| Test ID | Area | Command | Expected | Observed | Elapsed | Result | Notes |
|---------|------|---------|----------|----------|---------|--------|-------|
| TCA-HIL-001 | connectivity | `version` | Firmware/library version is printed. | not executed | 0.000s | `NOT_RUN` | NOT RUN: no board with TCA9548A attached to the host. |
| TCA-HIL-002 | cli | `help` | CLI help lists safe HIL commands. | not executed | 0.000s | `NOT_RUN` | NOT RUN: no board with TCA9548A attached to the host. |
| TCA-HIL-003 | diagnostics | `cfg` | Configuration snapshot is printed. | not executed | 0.000s | `NOT_RUN` | NOT RUN: no board with TCA9548A attached to the host. |
| TCA-HIL-004 | health | `health` | Driver health snapshot is printed. | not executed | 0.000s | `NOT_RUN` | NOT RUN: no board with TCA9548A attached to the host. |
| TCA-HIL-005 | bus | `scan` | Upstream I2C scan runs with bounded firmware loop. | not executed | 0.000s | `NOT_RUN` | NOT RUN: no board with TCA9548A attached to the host. |
| TCA-HIL-006 | probe | `probe` | Probe reports target status without health side effects. | not executed | 0.000s | `NOT_RUN` | NOT RUN: no board with TCA9548A attached to the host. |
| TCA-HIL-007 | contract | `hil dry` | Device-side dry HIL contract checks run. | not executed | 0.000s | `NOT_RUN` | NOT RUN: no board with TCA9548A attached to the host. |
| TCA-HIL-008 | contract | `hil run` | Live safe HIL contract checks run and restore the mux mask. | not executed | 0.000s | `NOT_RUN` | NOT RUN: no board with TCA9548A attached to the host. |

## Transcript

- Raw serial transcript: not captured; serial port was not opened.

## Sampling And Timing

- Sampling/timing highlights: not measured without hardware.

## Soak Summary

- Requested soak duration: `0.0` seconds.
- Actual soak duration: `0` seconds.
- Command mix: not run.
- Sample counts: `0`.
- Error counts: not observed.
- Reset/recovery counts: not observed.
- Worst observed latency: not measured.
- Health-state changes: not observed.
- Script adjustments during run: none.

## Limitations And Tests Not Run

- NOT RUN: no board with TCA9548A attached to the host.
- Firmware upload was not attempted.
- Boot transcript and prompt responsiveness were not captured.
- Live scan, probe, mask mutation, recover, reset, stress, and soak steps were not run.

## Fixes Implemented During This Pass

- Added host-side HIL runner with parser self-test, dry-run planning, bounded serial execution, and Markdown report output.
- Bounded the example CLI command buffer and discarded overlong serial lines until newline.
- Tightened example stress, selftest, and channel-scan commands to read and restore the original mux mask explicitly.

## Final Verification

- python tools\\tca9548a_hil.py --parser-self-test: PASS
- python tools\\tca9548a_hil.py --dry-run --port COM8 --baud 115200: PASS
- python scripts\\generate_version.py check: PASS
- pio test -e native: PASS, 70 tests
- pio run -e native_core_no_arduino: PASS
- pio run -e esp32s3dev: PASS
- pio run -e esp32s2dev: PASS
- doxygen Doxyfile: PASS, generated docs removed
- pio pkg pack: PASS, generated tarball removed
- git diff --check: PASS
- CI: no local .github workflows, no GitHub workflow runs, and no GitHub commit statuses for HEAD b943d19a98bc086986d31ace8a642a831aa7fed7
