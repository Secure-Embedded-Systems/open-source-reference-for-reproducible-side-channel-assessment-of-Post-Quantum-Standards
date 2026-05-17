# cw305_artifacts_mlkem_sw_v1

Self-contained CW305 artefact tree for the **mlkem-sw (no Custom-3 ISE issued)**
ML-KEM-1024 / FIPS-203 silicon experiment. Two bitstreams ship:

| Bitstream | Firmware | Purpose |
|---|---|---|
| `bitstreams/sca_pqc_cw305_mlkem_sw_kat_artifactrtl_cyc.bit` | `firmware/src/main_sw_cw305.c` | FIPS-203 KAT (byte-exact pk/sk/ct/ss vs reference) with `mcycle` live cycle counts |
| `bitstreams/sca_pqc_cw305_mlkem_sw_widetrig_artifactrtl_cyc.bit` | `firmware/src/main_sw_cw305_widetrig.c` | CCA-PC TVLA widetrig (decap loop, TIO4 pulse around `mlk_poly_tomsg`) |

Both bitstreams are built against the *clean artifact RTL* (commit `d3d21d9` of
the upstream OSPQC-Pro tree, snapshotted in `../cw305_artifacts_2026-05-15/rtl/`),
not against the dirty working-tree `rtl/`. Both pass timing closure on
`cv32e40x_core.clk` at 20.46 MHz (target 20 MHz). The SCA_PQC coprocessor +
ISE modules are present in fabric but the mlkem-sw firmware never issues a
Custom-3 (`0x7B`) opcode — the Makefile recipe asserts this and fails the
build if a single such opcode appears in `.text`.

## Headline numbers (read directly from this archive)

### Cycle counts (CV32E40X @ 16 MHz, live `mcycle` CSR, KAT firmware)

| Phase | Cycles (dec) | Cycles (hex) | k cycles | Wall time |
|---|---|---|---|---|
| KeyGen | 5 892 477 | `0x0059E97D` | 5 892 | 368.3 ms |
| Encap | 6 026 116 | `0x005BF384` | 6 026 | 376.6 ms |
| **Decap** | **7 401 699** | **`0x0070F0E3`** | **7 402** | 462.6 ms |
| Sentinel | — | `0xCAFE0001` | — | KAT **PASS** byte-exact vs FIPS-203 reference |

Source: live MBOX[0..2] read with `scripts/probe_boot.py` on the KAT bitstream.

Decap speed-up vs mlkem-ISE configurations on the same CV32E40X (180 k / 401 k k-cyc
quoted from the masked-ISE paper):

| Configuration | Decap (k cyc) | vs mlkem-sw |
|---|---|---|
| mlkem-sw (this work) | 7 402 | 1.0× |
| mlkem-ISE (naive) | 180 | **41.1×** |
| mlkem-ISE + DOM (masked) | 401 | 18.5× |

### Share-recombine TVLA (Saarinen-PQC, ISO 17825-compliant)

Trigger window: `mlk_poly_tomsg` bracket inside `indcpa.c` (the
Fujisaki–Okamoto share-recombine point where `m'` is unmasked and the
plaintext-check oracle bit transiently exists). HW-matched 10-pool fixed-vs-random
ciphertext set, LFSR-driven A/B coin published in MBOX[3] bit 16.

| $N$/pool | 1st-order $\|t\|$ | 2nd-order $\|t\|$ | Var(A)/Var(B) max $\|1{-}r\|$ | Verdict (vs 4.5 σ) |
|---|---|---|---|---|
| 1 k | 4.66 σ | 3.84 σ | 0.2696 | marginal (preliminary, below ISO floor) |
| **10 k** | **13.96 σ** | 3.17 σ | **0.0701** | **LEAK** (broadband; 149/5750 isolated samples > 4.5 σ, no run ≥ 5) |

Sources: `reports/mlkem_sw_widetrig_n10k/tvla_mlkem_sw_widetrig_n10k_1o_t.npz`
(and the analogous `_2o_t.npz`, `varratio_..npz`).

Reading: the SW Decap takes 41× more cycles than the ISE Decap; the share-recombine
event is therefore diluted across many more cycles in software, producing a real but
broadband first-order leak (about 5× smaller magnitude than the unmasked-ISE
positive control at the same $N$). The variance ratio is actually tighter than the
masked-ISE row (0.0701 vs 0.0784) — the leak lives entirely in the per-sample mean.

## Layout

```
README.md                    (this file)
bitstreams/                  KAT + widetrig .bit + .elf + build logs + probe logs + BUILD_*.md
firmware/src/                main_sw_cw305.c (KAT), main_sw_cw305_widetrig.c (TVLA),
                             crt0_mlkem_sw.S (phase markers + mcountinhibit clear),
                             link.ld, test_vectors_1024.h, saarinen_ct_pool_1024_v2.h
traces/mlkem_sw_widetrig_n1k/   N=1k A and B .npy (24 MB) + capture log
traces/mlkem_sw_widetrig_n10k/  N=10k A and B .npy (240 MB) + capture log
reports/mlkem_sw_widetrig_n1k/  1st-order, 2nd-order, varratio .npz (preliminary)
reports/mlkem_sw_widetrig_n10k/ same, ISO-17825-compliant
vectors/saarinen_pool_kem_v2.npz   pool definition (HW-matched 10-CT_B + 1 CT_A)
paper/                       oscar_17may26_v1.tex (paper), oscar_17may26_v1_results.tex (detailed journal)
scripts/                     probe_boot.py (boot diagnostic),
                             capture_cw305_saarinen.py (TVLA capture loop),
                             tvla_welch_t.py (1st-order),
                             tvla_second_order.py (centred-square 2nd-order)
```

## Re-derive any number in the paper

```sh
python3 - <<'PY'
import numpy as np
d  = np.load('reports/mlkem_sw_widetrig_n10k/tvla_mlkem_sw_widetrig_n10k_1o_t.npz')
t  = d['t']; post = np.abs(t[250:])
print(f'post-trigger max|t| = {post.max():.2f}')
print(f'N_fixed = {int(d["n_fixed"])}, N_random = {int(d["n_random"])}')
PY
```

The same recipe with the `_2o_t.npz` file returns the 2nd-order $|t|$, and
`varratio_..npz` carries `var_A`, `var_B`, `ratio`, `n`; `max|1-r|` is
`np.abs(np.load(...)["ratio"][250:] - 1).max()`.

## Reproduce a probe on bench

```sh
python3 scripts/probe_boot.py \
    --bitstream bitstreams/sca_pqc_cw305_mlkem_sw_kat_artifactrtl_cyc.bit \
    --target-clock-mhz 16 --poll-s 60
```

Expected output: MBOX[3] walks `0xCAFE0001..0007` (boot phase markers from
diagnostic crt0) → `0xCAFE00B0..B3` (KeyGen / Encap / Decap from `main()`) →
sticky `0xCAFE0001` (PASS). MBOX[0..2] settle to the three cycle counts.

## Reproduce a capture on bench

```sh
python3 scripts/capture_cw305_saarinen.py \
    --bitstream bitstreams/sca_pqc_cw305_mlkem_sw_widetrig_artifactrtl_cyc.bit \
    --vectors   vectors/saarinen_pool_kem_v2.npz \
    --N 10000 \
    --target-clock-mhz 16 --presamples 250 --samples 6000 --gain-db 25 \
    --out traces/mlkem_sw_widetrig_n10k/cw305_mlkem_sw_widetrig_artifactrtl_n10k
```

Followed by:

```sh
python3 scripts/tvla_welch_t.py \
    --fixed  traces/mlkem_sw_widetrig_n10k/cw305_mlkem_sw_widetrig_artifactrtl_n10k_setA.npy \
    --random traces/mlkem_sw_widetrig_n10k/cw305_mlkem_sw_widetrig_artifactrtl_n10k_setB.npy \
    --out    reports/mlkem_sw_widetrig_n10k/tvla_mlkem_sw_widetrig_n10k_1o_t.npz
```

ETA on a CW305-Lite at 16 MHz with 4× ADC (presamples 250, samples 6000, gain 25 dB):
≈ 175 min for 20 000 traces.

## What is **not** in this archive (by design)

- **RTL.** The SCA_PQC + ISE + OBI SystemVerilog is not "mlkem-sw related" —
  the mlkem-sw firmware never issues a Custom-3 opcode and never touches the
  ISE / OBI peripherals. The bitstreams in `bitstreams/` were built against
  the artifact RTL snapshot at `../cw305_artifacts_2026-05-15/rtl/` (commit
  `d3d21d9`). To regenerate the bitstreams, point a Yosys + nextpnr-xilinx
  flow at that RTL plus the firmware in `firmware/src/`.
- **mlkem-native upstream library.** The bitstreams already embed the linked
  ELF; the source library lives at `integration/firmware/mlkem_sw_baseline/upstream/mlkem`
  in the parent repo (pq-code-package/mlkem-native, portable-C SCU).
- **ISE TVLA traces and reports.** Those live in `../cw305_artifacts_2026-05-15/`
  (verify-window N=5k and share-recombine N=5k/N=10k for the naive and masked
  configurations) and are the comparison baselines, not artefacts of this folder.

## Provenance

- Parent repo HEAD at build time: `46dbacf` (master).
- Build dates: 2026-05-17 (both bitstreams + N=1k capture + N=10k capture).
- Toolchain: yosys (oss-cad-suite), nextpnr-xilinx, prjxray-db artix7
  (`xc7a35tftg256-2`), `riscv64-unknown-elf-gcc`, ChipWhisperer 5.6.1.
- Target board: ChipWhisperer CW305 (USB 2b3e:c305), XC7A35T-2 @ 16 MHz.
