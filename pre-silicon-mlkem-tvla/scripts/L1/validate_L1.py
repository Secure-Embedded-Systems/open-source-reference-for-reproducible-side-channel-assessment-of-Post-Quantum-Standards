#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
# Copyright (c) 2026 Dillibabu Shanmugam, Patrick Schaumont (WPI)
"""validate_L1.py — independent validation of L1 RTL-level Welch-t artefacts.

Reads L1_n10k_real.npz + L1_n10k_sham.npz directly (no external tool).
Produces:
  - per-cell breakdown CSV (16 cells × leaky-bit counts × max|t|)
  - validation summary on stderr / stdout

Validates the paper's Table IV "L1 bits" column:
  - Unmasked CV-X-IF boundary (rs1_reg, rs2_reg, result_reg): 96/96 bits leak
  - Masked core (DOM gadget + PRNG + Keccak + FSM + ...): 0/240 bits leak

This script ENCODES the validation logic from scratch; it does NOT call
any external tool that produced the npz files. Every number it prints
is recomputed from the raw arrays.
"""

import argparse
import csv
from pathlib import Path

import numpy as np
from scipy.stats import norm


THIS_DIR = Path(__file__).parent
DEFAULT_DATA = THIS_DIR.parent.parent / "data" / "L1"


def per_bit_max(T: np.ndarray) -> np.ndarray:
    """Max |T| per bit, taken over all cycles."""
    return np.abs(T).max(axis=0)


def per_cell_summary(npz: np.lib.npyio.NpzFile, threshold: float = 4.5):
    """Group bits by cell index, compute per-cell statistics."""
    T = npz["T"]
    bit_to_cell = npz["bit_to_cell"]
    widths = npz["widths"]
    names = npz["names"] if "names" in npz.files else None

    pbmax = per_bit_max(T)
    n_cells = len(widths)
    rows = []
    for c in range(n_cells):
        mask = (bit_to_cell == c)
        n_bits = int(mask.sum())
        if n_bits == 0:
            continue
        above = int((pbmax[mask] > threshold).sum())
        cell_max = float(pbmax[mask].max())
        cell_name = str(names[c]) if names is not None else f"cell_{c}"
        rows.append({
            "cell_idx": c,
            "cell_name": cell_name,
            "width": int(widths[c]),
            "n_bits_in_npz": n_bits,
            "bits_above_threshold": above,
            "max_abs_t": round(cell_max, 3),
        })
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--real",      default=str(DEFAULT_DATA / "L1_n10k_real.npz"))
    ap.add_argument("--sham",      default=str(DEFAULT_DATA / "L1_n10k_sham.npz"))
    ap.add_argument("--threshold", type=float, default=4.5, help="TVLA |t| threshold")
    ap.add_argument("--out-csv",   default=str(THIS_DIR.parent.parent / "results" / "L1_per_cell_summary.csv"))
    args = ap.parse_args()

    print("=" * 78)
    print("L1 validation: per-cell Welch-t analysis at N=10,000")
    print("=" * 78)

    real = np.load(args.real, allow_pickle=True)
    sham = np.load(args.sham, allow_pickle=True)
    T_real = real["T"]
    T_sham = sham["T"]

    n_cycles, n_bits = T_real.shape
    n_comp = n_cycles * n_bits
    bonferroni = norm.ppf(1 - 1e-5 / (2 * n_comp))

    print(f"\nDimensions:       {n_cycles} cycles × {n_bits} bits  ({n_comp} comparisons)")
    print(f"Threshold (TVLA): {args.threshold:.2f}σ")
    print(f"Threshold (Bonf): {bonferroni:.3f}σ (α=1e-5)")

    print(f"\n--- REAL stimulus (fixed-vs-random secret pool, masked-DoM OIM) ---")
    pbmax_real = per_bit_max(T_real)
    n_above = int((pbmax_real > args.threshold).sum())
    print(f"  max|t| overall:         {float(np.abs(T_real).max()):.3f}σ")
    print(f"  bits above {args.threshold}σ:        {n_above} / {n_bits}")
    print(f"  bits above Bonferroni:  {int((pbmax_real > bonferroni).sum())} / {n_bits}")

    print(f"\n--- SHAM null (label-scrambled pool, same OIM) ---")
    pbmax_sham = per_bit_max(T_sham)
    print(f"  max|t| overall:         {float(np.abs(T_sham).max()):.3f}σ")
    print(f"  bits above {args.threshold}σ:        {int((pbmax_sham > args.threshold).sum())} / {n_bits}")
    print(f"  bits above Bonferroni:  {int((pbmax_sham > bonferroni).sum())} / {n_bits}")

    # Per-cell summary
    rows = per_cell_summary(real, args.threshold)
    boundary_bits = sum(r["bits_above_threshold"] for r in rows)
    masked_core_widths = sum(r["width"] for r in rows if r["bits_above_threshold"] == 0)

    print(f"\n--- Per-cell breakdown ---")
    print(f"  {'idx':>3}  {'name':<35}  {'width':>5}  {'leaky':>6}  {'max|t|':>9}")
    for r in rows:
        flag = "  <<< LEAK" if r["bits_above_threshold"] > 0 else ""
        print(f"  {r['cell_idx']:>3}  {r['cell_name']:<35}  {r['width']:>5}  "
              f"{r['bits_above_threshold']:>3}/{r['n_bits_in_npz']:<3}  "
              f"{r['max_abs_t']:>9.3f}{flag}")

    print(f"\n--- Summary ---")
    print(f"  Boundary leaky bits (paper claim 96):   {boundary_bits}")
    print(f"  Masked-core total non-leaky width (paper claim 240): {masked_core_widths}")

    # Write per-cell CSV
    out_csv = Path(args.out_csv)
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with open(out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"\nWrote: {out_csv}")


if __name__ == "__main__":
    main()
