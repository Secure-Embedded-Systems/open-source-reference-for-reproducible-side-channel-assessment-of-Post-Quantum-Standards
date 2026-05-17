#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
# Copyright (c) 2026 Dillibabu Shanmugam, Patrick Schaumont (WPI)
"""validate_L35.py — independent validation of L3.5 per-tile Welch-t.

Reads L35_tile_welch_real.npz + L35_tile_welch_sham.npz directly.
Reports: max|t|, tiles above threshold, NaN counts, per-cycle peak location.

Does NOT call any L3.5 generation tool — only re-validates the npz contents.
"""
import argparse
from pathlib import Path

import numpy as np


THIS_DIR = Path(__file__).parent
DEFAULT_DATA = THIS_DIR.parent.parent / "data" / "L35"


def report_tile_welch(npz_path: Path, threshold: float = 4.5):
    d = np.load(npz_path)
    t = d["t"]                 # (cycles, X, Y)
    per_tile_max = d["per_tile_max"]   # (X, Y)
    per_tile_argmax_c = d.get("per_tile_argmax_c", None)
    N_F = int(d["N_F"])
    N_R = int(d["N_R"])

    nan_t = int(np.isnan(t).sum())
    nan_pt = int(np.isnan(per_tile_max).sum())
    pt_above = int(np.nansum(per_tile_max > threshold))

    print(f"\n--- {npz_path.name} ---")
    print(f"  shape t: {t.shape}  (cycles × X × Y),  N_F={N_F}, N_R={N_R}")
    print(f"  abs(t) overall max:    {float(np.nanmax(np.abs(t))):.3f}σ")
    print(f"  per_tile_max nanmax:   {float(np.nanmax(per_tile_max)):.3f}σ")
    print(f"  tiles above {threshold}σ:      {pt_above} / {per_tile_max.size}")
    print(f"  NaN in t:              {nan_t}")
    print(f"  NaN in per_tile_max:   {nan_pt}")

    # Find peak location
    if not np.all(np.isnan(per_tile_max)):
        flat = np.nan_to_num(per_tile_max, nan=-1)
        ix, iy = np.unravel_index(int(np.argmax(flat)), flat.shape)
        peak_c = int(per_tile_argmax_c[ix, iy]) if per_tile_argmax_c is not None else -1
        print(f"  peak tile coord:       (X={ix}, Y={iy}) at cycle {peak_c}")

    return {
        "max_t": float(np.nanmax(per_tile_max)),
        "tiles_above": pt_above,
        "nan_in_pt": nan_pt,
        "N_F": N_F, "N_R": N_R,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--real",      default=str(DEFAULT_DATA / "L35_tile_welch_real.npz"))
    ap.add_argument("--sham",      default=str(DEFAULT_DATA / "L35_tile_welch_sham.npz"))
    ap.add_argument("--threshold", type=float, default=4.5)
    args = ap.parse_args()

    print("=" * 78)
    print("L3.5 validation: per-tile Welch-t across XC7A35T fabric")
    print("=" * 78)

    r_real = report_tile_welch(Path(args.real), args.threshold)
    r_sham = report_tile_welch(Path(args.sham), args.threshold)

    print(f"\n--- Falsifiability witness (REAL vs SHAM) ---")
    print(f"  REAL: max|t|={r_real['max_t']:.2f}σ, tiles_above={r_real['tiles_above']}")
    print(f"  SHAM: max|t|={r_sham['max_t']:.2f}σ, tiles_above={r_sham['tiles_above']}")
    print(f"  Separation: {r_real['max_t'] / max(r_sham['max_t'], 0.01):.1f}×")
    print(f"  Paper claim: REAL 351.40σ / 961 tiles, SHAM 3.08σ / 0 tiles")
    print(f"  Caveat: SHAM has {r_sham['nan_in_pt']} NaN tile(s) (zero-activity tile)")


if __name__ == "__main__":
    main()
