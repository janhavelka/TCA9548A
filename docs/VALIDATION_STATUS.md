# TCA9548A validation status

Last reviewed: 2026-08-07.

## Authoritative device review

The driver protocol and constants were reviewed against Texas Instruments
TCA9548A datasheet SCPS207H, revision H, September 2024. The audit confirmed:

- the strap-selected 7-bit address range is `0x70` through `0x77`;
- the device exposes one 8-bit control byte with no register-address phase;
- bit N enables channel N and any combination of the eight channels is valid;
- selection takes effect only after the write ACK is followed immediately by
  STOP, and a multi-byte write retains only the final byte;
- POR and RESET clear the control byte to `0x00`;
- RESET is active low, requires at least 6 ns low, permits the next START with
  zero minimum recovery time, and releases SDA within 500 ns maximum;
- Standard-mode and Fast-mode operation are supported through 400 kHz.

No unsupported identity register, interrupt, measurement, general-call reset,
or undocumented register behavior is modeled. A successful control-byte read
proves that something responded at the configured address, not the exact chip
identity.

Primary source: [TI TCA9548A datasheet SCPS207H, Rev. H](https://www.ti.com/lit/ds/symlink/tca9548a.pdf).

## Cross-library consistency review

The framework boundary, injected transport, structured status, passive
four-state health, saturating counters, generated version flow, pinned
pioarduino `55.03.311` environment, and CLI conventions were compared read-only
with mature local I2C libraries including LDC1614, INA228, INA3221, BME280,
RV3032-C7, MB85RC, PCA9555, and SSD1315. Device-specific behavior remains
local: this pure switch has no conversion state machine, FIFO, threshold,
interrupt, or sensor data types.

The audit added native ESP-IDF/Arduino CLI parity and contract enforcement,
public allocation-free enum names, synchronized component/version metadata,
and truthful RESET callback failure identity. The core transaction protocol did
not require a corrective change.

## Automated evidence

The repository validation suite covers exact transaction address/length/data,
STOP-complete write contracts, read-only control access, all strap addresses,
all channel masks, output assignment only on success, distinct transport error
mapping, cache provenance/invalidation, lifecycle behavior, passive health
transitions/counters/timestamps, bounded recovery, and RESET verification.

Run the current gates from the repository root:

```powershell
python scripts\generate_version.py check
python tools\check_cli_contract.py
python tools\check_idf_example_contract.py
.\scripts\pio.cmd test -e native
.\scripts\pio.cmd run -e native_core_no_arduino
.\scripts\pio.cmd run -e esp32s3dev
.\scripts\pio.cmd run -e esp32s2dev
python tools\tca9548a_hil.py --parser-self-test
doxygen Doxyfile
.\scripts\pio.cmd pkg pack . --output .pio\TCA9548A.tar.gz
git diff --check
```

CI additionally builds `examples/espidf_basic` with ESP-IDF for ESP32-S2 and
ESP32-S3.

## Evidence not available

No PCB or TCA9548A fixture was available for this audit. No claim is made for
live device identity, electrical timing, voltage translation, address straps,
RESET wiring, simultaneous-channel loading, downstream routing, hot insertion,
or long-duration hardware stability. Firmware compilation, static CLI checks,
HIL parser self-tests, and HIL dry runs are not physical validation.
