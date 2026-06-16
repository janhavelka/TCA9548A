# TCA9548A Docs

This folder is intentionally small. The main user documentation lives in the
repository [README](../README.md), the public API is documented in
`include/TCA9548A/`, and runnable usage lives in [examples](../examples).

## Local Documents

- [HARDWARE_NOTES.md](HARDWARE_NOTES.md) - concise chip and board-design notes
- [PORTING.md](PORTING.md) - transport adapter and framework-neutral porting notes
- Doxygen output may be generated under `docs/doxygen/`; generated output is not
  source documentation

## Hardware Notes

Keep these core protocol details visible when changing the driver or examples:

- The device address range is `0x70` through `0x77`.
- The control register is one byte wide and has no register address byte.
- Bit `N` enables downstream channel `N`; multiple channels may be enabled at
  the same time.
- A control-register write must complete with a STOP condition before selected
  downstream buses are used.
- Power-on reset and hardware RESET leave all channels disabled (`0x00`).
- Multi-byte writes are not useful for this driver; the chip stores only the
  last byte.
- There is no interrupt output and no sensor or measurement function.
- Treat each enabled bus segment as part of the active I2C capacitance budget.
  Fast-mode operation is limited to 400 kHz and the usual 400 pF I2C bus budget
  per segment.

See [HARDWARE_NOTES.md](HARDWARE_NOTES.md) for the preserved design details from
the original chip documentation extracts.

## External References

- [TI TCA9548A product page and datasheet](https://www.ti.com/product/TCA9548A)
- [NXP UM10204 I2C-bus specification](https://www.nxp.com/docs/en/user-guide/UM10204.pdf)

Vendor datasheets and application notes are linked instead of copied into this
repository. This keeps the library docs focused on API behavior and avoids
stale extracted audit material.
