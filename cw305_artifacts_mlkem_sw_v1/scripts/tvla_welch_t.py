#!/usr/bin/env python3
"""
First-order TVLA via Welch's t-test, sample-by-sample.

Input  : two trace pools (fixed, random) of shape (N, T) each.
Output : t-curve of length T plus a hit-list of samples crossing ±4.5σ
         for >= 5 consecutive points.

Streaming-friendly: handles N up to a few million by reading the
ChipWhisperer .cwp file in chunks via numpy memmap.

USAGE:
    python3 tvla_welch_t.py \
        --fixed traces/cw305_basemul_fixed.npy \
        --random traces/cw305_basemul_random.npy \
        --out reports/tvla_basemul_t.npz
"""

import argparse
import os
import sys
import numpy as np


def welch_t_streaming(path_fixed: str, path_random: str, chunk: int = 4096):
    """
    Compute Welch's t per sample, using running mean / running variance
    so we never load the full trace pool into RAM.
    """
    f = np.load(path_fixed, mmap_mode="r")
    r = np.load(path_random, mmap_mode="r")
    if f.shape[1] != r.shape[1]:
        raise ValueError(f"sample count mismatch: {f.shape} vs {r.shape}")
    T = f.shape[1]

    # Welford's running statistics for both pools
    def init_stats():
        return np.zeros(T, dtype=np.float64), np.zeros(T, dtype=np.float64), 0
    mF, M2F, NF = init_stats()
    mR, M2R, NR = init_stats()

    def update(m, M2, N, batch):
        for x in batch:
            N += 1
            d = x - m; m = m + d / N; d2 = x - m; M2 = M2 + d * d2
        return m, M2, N

    print(f"[tvla] streaming through {f.shape[0]} fixed + {r.shape[0]} random traces, T={T}")
    for s in range(0, f.shape[0], chunk):
        mF, M2F, NF = update(mF, M2F, NF, f[s:s+chunk].astype(np.float64))
        if (s // chunk) % 8 == 0:
            print(f"  fixed  {s}/{f.shape[0]}", end="\r")
    print()
    for s in range(0, r.shape[0], chunk):
        mR, M2R, NR = update(mR, M2R, NR, r[s:s+chunk].astype(np.float64))
        if (s // chunk) % 8 == 0:
            print(f"  random {s}/{r.shape[0]}", end="\r")
    print()

    varF = M2F / max(NF - 1, 1)
    varR = M2R / max(NR - 1, 1)
    se   = np.sqrt(varF / NF + varR / NR + 1e-30)
    t    = (mF - mR) / se
    return t, NF, NR


def find_crossings(t: np.ndarray, threshold: float = 4.5, run_length: int = 5):
    """Indices where |t| > threshold for at least `run_length` consecutive samples."""
    above = np.abs(t) > threshold
    hits = []
    i = 0
    while i < len(above):
        if above[i]:
            j = i
            while j < len(above) and above[j]: j += 1
            if j - i >= run_length:
                hits.append((i, j, t[i:j].max()))
            i = j
        else:
            i += 1
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixed",  required=True)
    ap.add_argument("--random", required=True)
    ap.add_argument("--out",    required=True)
    ap.add_argument("--threshold", type=float, default=4.5)
    args = ap.parse_args()

    t, nf, nr = welch_t_streaming(args.fixed, args.random)
    hits = find_crossings(t, args.threshold)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    np.savez_compressed(args.out, t=t, n_fixed=nf, n_random=nr, threshold=args.threshold)

    print(f"\n[tvla] N_fixed={nf}, N_random={nr}, max|t|={np.abs(t).max():.2f}")
    if hits:
        print(f"[tvla] {len(hits)} threshold crossings (run≥5 samples) -- LEAKAGE FLAGGED")
        for i, j, peak in hits[:10]:
            print(f"  samples [{i}:{j}], peak |t|={abs(peak):.2f}")
    else:
        print(f"[tvla] no first-order leakage above ±{args.threshold:.1f}σ")
    sys.exit(0 if not hits else 1)


if __name__ == "__main__":
    main()
