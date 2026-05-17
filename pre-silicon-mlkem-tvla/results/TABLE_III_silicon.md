# Paper Table III — Silicon CCA-PC TVLA (validated from raw .npz)

Source paper: `oscar_17may26_v1.pdf` Section IV.B + Table III.
All numbers below recomputed by `scripts/silicon/validate_silicon_ccapc.py`
from the raw `.npz` files in `data/silicon/`.

Common protocol:
- **Hardware**: CW305 XC7A35T-2FTG256, ChipWhisperer-Lite scope
- **Clock**: 16 MHz design clock, 64 MHz ADC sample rate (4× oversample)
- **Trace shape**: 6,000 samples per trace
- **Post-trigger statistic**: max\|t[250:]\| (skip first 250 samples for trigger transient)
- **Pool construction**: HW-matched 10-CT_B pool with HW(CT_B_i) within ±10 bits of HW(CT_A); PRNG-randomised A/B coin via 32-bit Galois LFSR seeded from `mcycle`
- **Per-trace DC subtract** on host
- **Threshold**: 4.5σ TVLA, ≥5 consecutive samples for hit detection

## Table III (validated)

| Configuration | N/pool | 1st-order \|t\| | 2nd-order \|t\| | Verdict |
|---|---|---|---|---|
| SW (no ISE issued) | 10k | **13.958σ** | **3.174σ** | **LEAK** |
| ISE naive (pqc_only, positive control) | 5k | **71.552σ** | **53.823σ** | **LEAK** |
| ISE + DOM (masked) | 10k | **3.098σ** | **4.008σ** | **PASS** |

All values match paper claims to within rounding (≤0.02σ).

## Source files

```
data/silicon/share_recombine_n5k/
├── tvla_share_recombine_naive_1o_t.npz   ← 71.55σ
├── tvla_share_recombine_naive_2o_t.npz   ← 53.82σ
├── tvla_share_recombine_masked_1o_t.npz  ← (N=5k masked, screening grade)
├── tvla_share_recombine_masked_2o_t.npz
├── varratio_naive_share_recombine.npz
├── varratio_masked_share_recombine.npz
├── mean_diff_naive_share_recombine.npy
└── mean_diff_masked_share_recombine.npy

data/silicon/share_recombine_n10k/
├── tvla_share_recombine_masked_n10k_1o_t.npz   ← 3.10σ  (ISO 17825)
├── tvla_share_recombine_masked_n10k_2o_t.npz   ← 4.01σ
└── mean_diff_masked_n10k.npy

data/silicon/mlkem_sw_widetrig_n10k/
├── tvla_mlkem_sw_widetrig_n10k_1o_t.npz        ← 13.96σ
├── tvla_mlkem_sw_widetrig_n10k_2o_t.npz        ← 3.17σ
└── varratio_mlkem_sw_widetrig_n10k.npz
```

## √N scaling interpretation

The paper's masked 1o went from 3.61σ at N=5k → 3.10σ at N=10k (a *decrease*).
A real first-order leak under H1 would scale as √N: 3.61 × √2 = 5.11σ at
N=10k.  The decrease confirms the N=5k value was a noise sample from the
H0 distribution, not a sub-detection leak.

This is the strongest available evidence that the masked design's residual
3.10σ is statistical noise, not real leakage.

## Suppression ratio

NAIVE 1o = 71.55σ → MASKED 1o = 3.10σ at the same trigger window:
- Absolute reduction: 71.55 − 3.10 = 68.45σ
- Multiplicative: 71.55 / 3.10 = 23.1×
- The paper claims "≥16× suppression below the 4.5σ detection threshold"

Validation: 71.55 × (4.5/71.55) = 4.5σ would put the masked design at the
detection threshold; the actual masked = 3.10σ is below threshold by 1.4σ.

## Reproduction

```bash
python3 scripts/silicon/validate_silicon_ccapc.py
```
