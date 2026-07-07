# CW305 artefacts — 2026-05-15

Self-contained CW305 (XC7A35T-2FTG256) archive: RTL → bitstream →
firmware → captured traces → analysis, for two CCA-PC TVLA campaigns
on ML-KEM-1024 (naive vs DOM d=1 masked decapsulation). Hardware-matched
FvR pool (10-CT_B pool with HW(CT_B_i) within ±10 bits of HW(CT_A)),
PRNG-randomised A/B selection via 32-bit Galois LFSR seeded from
`mcycle`, explicit pool label in MBOX[3], per-trace DC subtract on host.

ADC: 64 MHz (4× target clock 16 MHz), presamples=250, samples=6000,
gain 25 dB, ChipWhisperer-Lite scope.

## Layout
```
GIT_COMMIT.txt        base commit hash of the parent repo
GIT_DIRTY.txt         non-empty -> working tree had uncommitted edits when this archive was built;
                      ship-as-working-tree, not as-committed
rtl/                  SystemVerilog for the SCA_PQC coprocessor + ISE units + secure-arith primitives
  ise/                ISE per-op modules (mask_ops, mod_reduce, ntt_butterfly, keccak_ops, ...)
  obi/                OBI peripherals (TRNG, A2B/B2A converters, sec_add, sec_and, ...)
sw/                   masked-SW C library + custom-instruction headers (.c/.h)
firmware_mlkem1024_fips203/
                      pq-crystals reference (kem, indcpa, poly, polyvec, ntt, reduce, verify,
                      fips202, cbd, randombytes, symmetric-shake) + main_saarinen_{naive,masked}.c
                      with the v2-widetrig trigger placement + ct-pool header
                      (the .c here are widetrig; verify-window source no longer on disk)
bitstreams/           4 × .bit (2 trigger windows × naive/masked)
firmware_snapshots/   convenience copies of the share-recombine main_*.c
vectors/              v2 FvR pool definition (.npz)
traces/
  verify_window/      5k+5k naive, 5k+5k masked   trigger on verify(c,c')
  share_recombine_n5k/ 5k+5k naive, 5k+5k masked  trigger on poly_tomsg / unmask+tomsg
  share_recombine_n10k/ 10k+10k masked            ISO 17825 minimum on the masked side
  logs/               capture stdout per campaign
reports/              .npz Welch-t / .npz varratio / .npy mean-diff per campaign
figs/                 final overlay PDFs
```

## Headline numbers (read directly from the .npz files in this archive)

| campaign | post-trigger max\|t\| 1st-order | 2nd-order | varratio max\|1-r\| |
|---|---|---|---|
| verify-window, naive  N=5k+5k  | 3.91  (clean) | — | 0.091 |
| verify-window, masked N=5k+5k  | 3.67  (clean) | — | 0.111 |
| share-recombine, naive  N=5k+5k | **71.55 (LEAK)** | 53.82 (LEAK) | 0.847 |
| share-recombine, masked N=5k+5k | 3.61 (clean) | 2.91 (clean) | 0.088 |
| share-recombine, masked N=10k+10k | **3.10 (clean)** | 4.01 (clean) | **0.0784** (inside Bonferroni null) |

Suppression: naive 71.55σ → masked 3.10σ at ISO 17825 N — **≥16× to
below the 4.5σ detection threshold** (using 4.5σ as the upper bound
on the masked side since 3.10σ is a noise sample).

√N scaling cross-check: masked 1st-order goes 3.61σ at N=5k →
3.10σ at N=10k (real leakage would predict 3.61·√2 = 5.11σ). The
decrease confirms the masked statistic at N=5k was pure noise
sampling, not a sub-detection leakage signal.

## Bitstream → firmware → trace provenance

| Bitstream | Firmware (in firmware_snapshots/) | Trace files |
|---|---|---|
| `sca_pqc_cw305_saarinen_naive_v2_fvr.bit`      | (verify-window naive, source no longer on disk; bit-only)   | `traces/verify_window/cw305_saarinen_naive_v2_set{A,B}.npy` |
| `sca_pqc_cw305_saarinen_masked_v2_fvr.bit`     | (verify-window masked, source no longer on disk; bit-only)  | `traces/verify_window/cw305_saarinen_masked_v2_set{A,B}.npy` |
| `sca_pqc_cw305_saarinen_naive_v2_widetrig.bit` | `main_saarinen_naive_widetrig.c`  | `traces/share_recombine_n5k/cw305_saarinen_naive_v2_widetrig_set{A,B}.npy` |
| `sca_pqc_cw305_saarinen_masked_v2_widetrig.bit`| `main_saarinen_masked_widetrig.c` | `traces/share_recombine_n5k/cw305_saarinen_masked_v2_widetrig_set{A,B}.npy`  + `traces/share_recombine_n10k/…_n10k_set{A,B}.npy` |

```python
import numpy as np
d = np.load("reports/share_recombine_n10k/tvla_share_recombine_masked_n10k_1o_t.npz")
abst = np.abs(d["t"])
print(abst[250:].max())   # -> 3.10
print(int(d["n_fixed"]))   # -> 10000
```

## To rebuild from this archive

1. **Bitstream**: requires Yosys (+ slang plugin), nextpnr-xilinx, prjxray-db; point a Makefile-like driver at `rtl/` + `firmware_mlkem1024_fips203/` + `sw/`. The original build flow lived at `integration/Makefile` upstream of this archive (`FW=fips203_saarinen` for naive, `FW=fips203_saarinen_masked` for masked).
2. **Firmware**: `riscv64-unknown-elf-gcc -O2 -march=rv32imc -mabi=ilp32 …` against `firmware_mlkem1024_fips203/{src,include}` + `sw/`.
3. **Capture**: ChipWhisperer-Lite + CW305, `chipwhisperer` Python API, 16 MHz target / 64 MHz ADC, presamples=250, samples=6000, gain 25 dB. The `traces/logs/*.log` files contain the exact `capture_cw305_saarinen.py` invocations.
4. **Analyse**: `tvla_welch_t.py` (1st-order Welch-t streaming) and `tvla_second_order.py` (centred-square 2nd-order) — both upstream of this archive; their outputs (the .npz files in `reports/`) are included so analysis is verifiable without re-running.

## Excluded by design

- v1 single-CT/deterministic-A/B confound traces (replaced by v2 protocol)
- Aborted partial-N captures
- Stratified-subset npz (intermediate, derivable from full-N traces)
- 1500×1500 / 2500×2500 subsets (intermediate)
- Python analysis tooling source (the .npz outputs are here; the .py is upstream)
- Build intermediates (.json synth output, .fasm — large, regenerable from RTL)
- ASIC / paper-LaTeX (not CW305 artefacts)
- Verify-window firmware mains (the `.c` source was overwritten on disk by the widetrig edits; the verify-window bitstreams ship as binaries only — reconstructable from this archive's RTL/SW by reverting `main_saarinen_{naive,masked}.c` to put `MBOX(1)=1` around `verify(c, c')` instead of around `poly_tomsg`)

## Bundled artefact folders

Two companion artefact trees ship alongside the 2026-05-15 snapshot above. Each is self-contained, has its own `README.md`, and reuses the same `rtl/` + `sw/` baseline.

### [`cw305_artifacts_mlkem_sw_v1/`](cw305_artifacts_mlkem_sw_v1/README.md)
CW305 silicon artefacts for the **pure-software ML-KEM-1024 baseline** (Custom-3 ISE present in fabric but never issued by the firmware — Makefile-enforced). Ships two bitstreams (KAT + widetrig), the `mlkem-sw` firmware, FIPS-203 KAT-PASS evidence with live `mcycle` cycle counts (Decap = 7,402 k cycles), and CCA-PC TVLA captures at N=1k and N=10k for the share-recombine window. This is the **SW-only positive-control point** for the three-way SW vs ISE vs ISE+DOM comparison.

### [`pre-silicon-mlkem-tvla/`](pre-silicon-mlkem-tvla/README.md)
Pre-silicon SCA evaluation framework at **two abstraction layers**: L1 (Yosys-flattened RTL, per-bit Welch-t, N=10k FvR) and L3.5 (XC7A35T routed fabric, per-PIP and per-tile Welch-t projected through Project X-Ray chipdb). Ships the L1 and L3 result NPZ/CSVs, all four CW305 bitstreams (naive/masked × FvR/widetrig), validation scripts that re-derive every paper number from the raw data, and the cross-layer leakage-attribution evidence behind Table tab:presi.

## License
Unless otherwise noted, everything in this repository is covered by the Apache License, Version 2.0 (see LICENSE for full text).
