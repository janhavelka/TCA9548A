# PassFET Hang Time with TCA39306 I2C/I3C Level Translator
**Source:** pass_fet.pdf | **Doc #:** SLUAAY3 | **Pages:** 9

## Key Takeaways
- PassFET-based I2C switches and level translators (including TCA9548A) exhibit a "hang time" effect — a brief delay during HIGH→LOW transitions caused by parasitic bus capacitance charge balancing
- Hang time is typically nanoseconds (7.85 ns to 111.85 ns in testing) and does not cause data corruption even at FM+ (1 MHz) speeds
- Hang time increases with: larger parasitic bus capacitance, higher supply voltage on the opposite side, and pulling LOW from the lower-voltage side
- The TCA9548A uses the same passFET architecture as the TCA39306 and PCA9306, so the same hang-time behavior applies

## Summary
I2C devices that use passFET (pass-gate FET) architecture — including level translators (TCA39306, PCA9306) and I2C switches (TCA9548A) — introduce a brief "hang time" during bus transitions. This occurs because the passFET connects two bus segments with different parasitic capacitances (and potentially different voltages), and when one side is pulled LOW, the charge stored on the opposite side's parasitic capacitance must equalize before the bus can settle to VOL.

The passFET operates at the edge of its cutoff region when both sides are at VCC (VGS ≈ VTH). When a driver pulls one side to VOL (~0.4 V), VGS increases dramatically (e.g., to 3.5 V for a 3.3 V / 5.0 V translation setup), turning the FET on strongly in its linear region. Current then flows from both supply rails through the pull-ups and open-drain driver to GND. The bidirectional nature of the passFET (drain and source are interchangeable) enables seamless level translation — the controller sees VCC1 (3.3 V) in the HIGH state while the target sees VCC2 (5.0 V).

The hang time is the period during which the bus "stalls" at an intermediate voltage while charge on both sides' parasitic capacitors reaches equilibrium through the passFET. Despite being measurable on an oscilloscope, the effect is harmless to I2C communication integrity.

## Technical Details
**PassFET bias equations (TCA39306 level-translating mode):**
- Gate voltage: VGATE = VCC1 + VTH (with EN+VREF2 shorted)
- Idle state: VGS = VTH (~0.6 V) → FET at edge of cutoff
- Active state (one side pulled LOW): VGS = VGATE − VOL = 3.9 V − 0.4 V = 3.5 V → FET strongly ON
- I2C spec: VOL = 0.4 V with IOL ≥ 3 mA minimum sink current

**Hang time measurements (TCA39306, RPU = 10 kΩ, 100 kHz I2C):**

| Condition | CBUS1 | CBUS2 | VCC1 | VCC2 | Pull LOW side | Hang Time |
|---|---|---|---|---|---|---|
| Baseline | 100 pF | 100 pF | 3.3 V | 5.0 V | Target (SDA2) | 7.85 ns |
| Reversed drive | 100 pF | 100 pF | 3.3 V | 5.0 V | Controller (SDA1) | 36.25 ns |
| Asymmetric cap | 100 pF | 400 pF | 3.3 V | 5.0 V | Controller (SDA1) | 85.45 ns |
| Worst case | 100 pF | 400 pF | 3.3 V | 6.0 V | Controller (SDA1) | 111.85 ns |

**Factors affecting hang time magnitude:**
1. **Parasitic bus capacitance** — larger capacitance holds more charge (Q = C × V), extending equalization time
2. **Supply voltage levels** — higher voltage on opposite side means more stored charge
3. **Drive side** — pulling LOW from the lower-voltage side creates longer hang time because a higher-voltage capacitor must discharge through the FET
4. **Drive strength** — stronger drivers (lower Rdson open-drain) discharge faster

**Key result:** Even in the worst-case test scenario (111.85 ns), hang time is negligible compared to I2C bit periods (2.5 µs at 400 kHz, 1 µs at 1 MHz). I2C address 0x74 transmitted correctly without data corruption across the level translator.

## Relevance to TCA9548A Implementation
The TCA9548A uses the same passFET architecture internally for each of its 8 channels. When the switch connects the upstream bus to a downstream channel, the passFET hang-time effect occurs during every HIGH→LOW transition. For the TCA9548A driver design, this means: (1) The hang time is inherent and cannot be eliminated — it is a fundamental property of the CMOS pass-gate topology. (2) Bus capacitance per downstream channel should be minimized to keep hang time short. (3) When level-translating between significantly different voltages (e.g., 1.8 V upstream, 5.0 V downstream) with high downstream capacitance, the hang time will be at its maximum — but still well within I2C timing margins. (4) No software compensation is needed; the effect is transparent to the I2C protocol.
