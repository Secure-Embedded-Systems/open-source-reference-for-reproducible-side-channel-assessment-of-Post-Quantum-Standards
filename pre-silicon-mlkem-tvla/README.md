# pre-silicon-mlkem-tvla

Consolidated artefact folder for the pre-silicon evaluation framework
described in `paper/oscar_17may26_v1.pdf` Section V.

Two abstraction layers cover the open-flow pre-silicon SCA assessment of the
open-source PQC coprocessor (CV32E40X + CV-X-IF + 38-instruction PQC ISE):

- **L1 — RTL-level per-bit Welch-t** on the Yosys-flattened netlist of the
  masked (ISE + DoM) bitstream, N=10,000 fixed-vs-random pools.
- **L3.5 — Routed-fabric per-PIP and per-tile** Welch-t projection onto the
  XC7A35T routed netlist, annotated with Project X-Ray chipdb timing.

Silicon CCA-PC TVLA on CW305 (Tables III) covers three configurations on the
same bitstream-RTL via firmware swap:
- **mlkem-sw** (no Custom-3 ISE issued)         — full FIPS-203 in software
- **mlkem-ise NAIVE** (pqc_only, positive ctl)  — Custom-3 ISE, no masking
- **mlkem-ise + DoM MASKED**                    — Custom-3 ISE + DOM gadget

All numbers in this folder are validated against raw `.npz` / `.csv` on disk
(see `results/VALIDATION_LOG.md`). Validation scripts are independent
re-implementations, not blind copies of the upstream tools.

## Quick start

```bash
# 1. Validate L1 RTL Welch-t numbers
python3 scripts/L1/validate_L1.py

# 2. Validate L3.5 per-tile Welch-t numbers
python3 scripts/L35/validate_L35.py

# 3. Validate silicon CCA-PC TVLA numbers
python3 scripts/silicon/validate_silicon_ccapc.py

# 4. (Re-)generate the full 14,951-row leaky PIPs CSV
python3 scripts/L35/enumerate_leaky_pips_full.py
```

Run all four; expected output matches:

| Source | Number | Paper claim | Status |
|---|---|---|---|
| L1 RTL, real stim, masked-DoM OIM | max\|t\|=534.63σ, 96/336 bits leak | 96/96 boundary | ✅ |
| L1 RTL, SHAM null | max\|t\|=3.83σ, 0/336 bits leak | (implied null) | ✅ |
| L3.5 per-tile real | max\|t\|=351.40σ, 961 tiles leak | 351.40 / 961 | ✅ |
| L3.5 per-tile SHAM | max\|t\|=3.08σ, 0 tiles leak | 3.08 / 0 | ✅ (1 NaN tile) |
| L3.5 leaky PIPs (rs1+rs2+result_reg fanout) | 14,951 PIPs / 1,671 INT tiles | 14,951 / 1,671 | ✅ |
| Silicon mlkem-sw 1o (decap, N=10k) | 13.96σ LEAK | 13.96σ LEAK | ✅ |
| Silicon mlkem-ise NAIVE 1o (share-recombine, N=5k) | 71.55σ LEAK | 71.55σ LEAK | ✅ |
| Silicon mlkem-ise+DOM 1o (share-recombine, N=10k) | 3.10σ PASS | 3.10σ PASS | ✅ |
| Silicon mlkem-ise+DOM 2o (share-recombine, N=10k) | 4.01σ PASS | 4.01σ PASS | ✅ |

## Directory layout

```
pre-silicon-mlkem-tvla/
├── README.md                       # this file
├── data/
│   ├── L1/
│   │   ├── L1_n10k_real.npz        # 400 cyc × 336 bits, real FvR stimulus
│   │   └── L1_n10k_sham.npz        # same shape, label-scrambled null
│   ├── L35/
│   │   ├── L35_tile_welch_real.npz # 400 cyc × 128 × 108 tile grid
│   │   ├── L35_tile_welch_sham.npz # same, SHAM null
│   │   ├── L35_leaky_pips_top500.csv     # top-500 (upstream artefact)
│   │   ├── L35_spatial_summary.csv       # per-tile spatial summary
│   │   ├── per_net_attribution.csv       # nextpnr-xilinx per-net driver list
│   │   ├── net_to_pip.csv                # 688,153 (net, PIP) traversals
│   │   └── leaky_pips_full.csv           # ← generated: full 14,951 PIPs
│   ├── chipdb/
│   │   ├── chipdb_per_tiletype.json      # 112 XC7A35T tile types
│   │   └── chipdb_timing_268pipclasses.json
│   ├── silicon/
│   │   ├── share_recombine_n5k/    # mlkem-ise naive + masked, N=5k
│   │   ├── share_recombine_n10k/   # mlkem-ise+DOM masked, N=10k (ISO 17825)
│   │   └── mlkem_sw_widetrig_n10k/ # mlkem-sw decap, N=10k
│   └── bitstreams/                 # 4× .bit (2 trig windows × naive/masked)
├── scripts/
│   ├── L1/validate_L1.py           # per-cell Welch breakdown + summary CSV
│   ├── L35/validate_L35.py         # per-tile Welch + falsifiability witness
│   ├── L35/enumerate_leaky_pips_full.py  # rs1/rs2/result_reg → 14,951 PIPs
│   └── silicon/validate_silicon_ccapc.py # Table III silicon numbers
├── src/
│   ├── rtl/                        # ise/, sca_pqc_pkg.sv, masking_cone_top*
│   ├── tsim_kernel/tsim.h          # rtl_level_tsim per-bit tensor sim
│   └── oim/gen_oim_fullchip.py     # OIM C-header generator (yosys-json → OIM)
├── results/
│   ├── VALIDATION_LOG.md           # what's verified vs. discrepant
│   ├── TABLE_III_silicon.md        # paper Table III, all numbers verified
│   ├── TABLE_IV_cross_layer.md     # paper Table IV, with caveats
│   ├── L1_per_cell_summary.csv     # per-cell L1 breakdown (16 cells × stats)
│   └── agreement_table.csv         # L1 → synth → routed → silicon chain
├── paper_section/                  # crosslayer_section.tex + drafts
├── methodology/                    # methodology .md files (one per layer)
└── docs/                           # supplementary notes
```

## What is "pqc_only" vs "DoM" at L1?

**Important methodological note** (audited and confirmed by re-reading the npz):

The L1 npz files `L1_n10k_real.npz` and `L1_n10k_sham.npz` are BOTH from the
**masked (ISE + DoM) Yosys-flattened OIM**. The `real` / `sham` suffix
distinguishes pool construction (real fixed-vs-random secret stimulus vs
label-scrambled null), NOT bitstream variant.

The paper's "pqc_only" configuration at L1 is inferred as follows:
- The masked DoM OIM contains both the DoM gadget (cells `r_latched_q`,
  `prng.s`, etc.) AND the unmasked CV-X-IF boundary registers (`rs1_reg`,
  `rs2_reg`, `result_reg`).
- The 96-bit leak on the boundary registers (cells 4, 5, 8 in the npz; cell
  names tagged `u_keccak.acc_c[N]` due to an OIM-generator naming artefact)
  represents what an unmasked (pqc_only) bitstream's boundary transport
  would leak, since those bytes carry plaintext operands regardless of DoM.
- The silicon positive control (71.55σ at N=5k) comes from a separately-built
  naive bitstream `sca_pqc_cw305_saarinen_naive_v2_widetrig.bit`.

If a reviewer asks for a "true" pqc_only L1 npz produced from a naive-RTL
OIM, that artefact does not exist in this tree and is future work.

## What is "L3.5"?

L3.5 lives between L3 (gate-level netlist, post-techmap) and L4 (signal-level
EM/power silicon measurement). It is the **post-place-and-route routed
netlist** projected onto the FPGA's physical fabric coordinates (tile X/Y,
PIP class), with Project X-Ray chipdb timing annotations.

The L3.5 layer's contribution beyond L1 is:
- Physical tile coordinates for EM-probe placement
- PIP-class delay context to rule out (or implicate) glitch-driven leakage
- Spatial clustering analysis (9.4× over random baseline)

See `paper_section/PAPER_L35_STANDALONE.tex` for the standalone L3.5 writeup.

## Provenance

All raw data files are byte-identical copies of:
- `../artifact_presilicon/data/*.npz`, `*.csv`, `*.json`
- `../cw305_artifacts_2026-05-15/reports/share_recombine_*/*.npz`
- `../../OSPQC_Pro_MLKEM1024_MLDSA44_CW305_mlkem_sw/cw305_artifacts_mlkem_sw_v1/reports/mlkem_sw_widetrig_n10k/*.npz`
- `../cw305_artifacts_2026-05-15/bitstreams/*.bit`
- `../tsim_mlkem_mldsa/phase_fabric_tsim/build/{per_net_attribution,net_to_pip}.csv`

Scripts under `scripts/` are NEW re-implementations written for this folder
that independently produce the same numbers (validated; see VALIDATION_LOG).
