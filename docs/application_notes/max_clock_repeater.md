# Maximum Clock Frequency of I2C Bus Using Repeaters
**Source:** max_clock_repeater.pdf | **Doc #:** SLVA695 | **Pages:** 7

## Key Takeaways
- Repeaters add propagation delay that reduces the maximum achievable I2C clock frequency; the FM spec has 300 ns timing margin but FM+ has 0 ns margin for repeater delays
- With TCA9617B repeater at FM spec: fSCL(max) ≈ 503.6 kHz (meets 400 kHz requirement); at FM+ spec: fSCL(max) ≈ 872.8 kHz (does not meet 1 MHz requirement under full loading)
- Maximum bus capacitance: 400 pF for FM, 550 pF for FM+; each device adds ~15 pF
- Repeater propagation delays (TCA9617B): tPHL;AB = 144 ns, tPHL;BA = 140 ns

## Summary
I2C repeaters solve the bus capacitance problem by isolating capacitance between master and slave sides, allowing each segment to independently support up to 400 pF (FM) or 550 pF (FM+). However, repeaters introduce propagation delay on both SCL and SDA that must be accounted for in the timing budget when determining the maximum clock frequency.

For an I2C bus without a repeater, fSCL(max) = 1 / (tLOW + tHIGH + tR + tF), yielding exactly 400 kHz for FM and 1000 kHz for FM+. With a repeater in the path, the worst-case timing occurs when the slave sends data to the master: SCL propagates A→B through the repeater (delay tPHL;AB), the slave responds within tVD;DAT, and SDA propagates B→A (delay tPHL;BA) — all of which must complete within tLOW.

The critical constraint is: tLOW(min) ≥ tPHL;AB + tVD;DAT(max) + tPHL;BA + tSU;DAT(min). FM spec has 300 ns of margin (1300 − 900 − 100 = 300 ns) to absorb repeater delays, but FM+ has zero margin (500 − 450 − 50 = 0 ns), making 1 MHz operation with repeaters achievable only under reduced loading conditions.

## Technical Details
**Fundamental formulas:**

Without repeater:
$$f_{SCL(max)} = \frac{1}{t_{LOW} + t_{HIGH} + t_R + t_F}$$

With repeater (worst case, HIGH→LOW SDA transition):
$$f_{SCL(max)} = \frac{1}{t_{PHL;AB} + t_{VD;DAT} + t_{PHL;BA} + t_{SU;DAT} + t_F + t_{HIGH} + t_R}$$

**FM spec timing budget (without/with TCA9617B):**

| Parameter | FM Spec | FM + TCA9617B delays | FM + TCA9617B delays+rise/fall |
|---|---|---|---|
| tVD;DAT(max) | 900 ns | 900 ns | 900 ns |
| tSU;DAT(min) | 100 ns | 100 ns | 100 ns |
| tHIGH(min) | 600 ns | 600 ns | 600 ns |
| tPHL;BA | — | 140 ns | 140 ns |
| tPHL;AB | — | 144 ns | 144 ns |
| tF(max) | 300 ns | 300 ns | 13.8 ns |
| tR(max) | 300 ns | 300 ns | 88 ns |
| **fSCL(max)** | **400 kHz** | **402.6 kHz** | **503.6 kHz** |

**FM+ spec timing budget (without/with TCA9617B):**

| Parameter | FM+ Spec | FM+ + TCA9617B delays | FM+ + TCA9617B delays+rise/fall |
|---|---|---|---|
| tVD;DAT(max) | 450 ns | 450 ns | 450 ns |
| tSU;DAT(min) | 50 ns | 50 ns | 50 ns |
| tHIGH(min) | 260 ns | 260 ns | 260 ns |
| tPHL;BA | — | 140 ns | 140 ns |
| tPHL;AB | — | 144 ns | 144 ns |
| tF(max) | 120 ns | 120 ns | 13.8 ns |
| tR(max) | 120 ns | 120 ns | 88 ns |
| **fSCL(max)** | **1000 kHz** | **778.8 kHz** | **872.8 kHz** |

**Key timing constraints:**
- tLOW timing margin = tLOW(min) − tVD;DAT(max) − tSU;DAT(min)
  - FM: 1300 − 900 − 100 = **300 ns** margin
  - FM+: 500 − 450 − 50 = **0 ns** margin
- Reducing bus loading reduces repeater rise/fall times, allowing higher fSCL

## Relevance to TCA9548A Implementation
While the TCA9548A is a switch (not a repeater), this analysis is relevant because switches and repeaters are often used together in complex I2C trees. If repeaters (TCA9617B) are placed on TCA9548A downstream channels to extend capacitance, the combined propagation delays of switch Rdson plus repeater delay must be budgeted into the timing calculation. For FM (400 kHz) operation, there is sufficient margin. For FM+ (1 MHz), the system designer must verify that the total delay chain (switch + repeater + slave tVD;DAT) fits within tLOW. The TCA9548A driver should allow configurable clock speed per channel to accommodate channels with and without repeaters.
