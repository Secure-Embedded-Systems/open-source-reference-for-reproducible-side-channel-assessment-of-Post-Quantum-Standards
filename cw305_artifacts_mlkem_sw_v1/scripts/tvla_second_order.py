#!/usr/bin/env python3
"""
Second-order TVLA: centred-product t-test.

Step 1: per-sample mean over each trace pool, then centre traces around it.
Step 2: for each sample i, square the centred trace x[i]^2 (univariate
        2nd-order leakage detector).  Apply Welch's t-test on the squared
        traces between fixed and random pools.

A correctly-masked d=1 implementation MUST show second-order leakage
(t > 4.5) at the masked operation samples.  Its absence is a sign that
the masking is not actually doing anything (e.g., one share is constant).

USAGE:
    python3 tvla_second_order.py \
        --fixed traces/cw305_basemul_fixed.npy \
        --random traces/cw305_basemul_random.npy \
        --out reports/tvla_basemul_t2.npz
"""

import argparse
import os
import numpy as np


def centred_square_t(path_fixed: str, path_random: str, chunk: int = 4096):
    f = np.load(path_fixed,  mmap_mode="r")
    r = np.load(path_random, mmap_mode="r")
    T = f.shape[1]

    # Pass 1: compute per-pool mean.
    def mean_streaming(arr):
        m = np.zeros(T, dtype=np.float64); n = 0
        for s in range(0, arr.shape[0], chunk):
            blk = arr[s:s+chunk].astype(np.float64)
            m += blk.sum(axis=0); n += blk.shape[0]
        return m / max(n, 1), n
    print("[tvla-2o] computing per-pool means...")
    mF, NF = mean_streaming(f)
    mR, NR = mean_streaming(r)

    # Pass 2: Welford on (x - m)^2.
    def secord_streaming(arr, m_):
        m = np.zeros(T, dtype=np.float64); M2 = np.zeros(T, dtype=np.float64); n = 0
        for s in range(0, arr.shape[0], chunk):
            blk = arr[s:s+chunk].astype(np.float64) - m_
            blk = blk * blk
            for x in blk:
                n += 1; d = x - m; m += d / n; d2 = x - m; M2 += d * d2
        return m, M2, n
    print("[tvla-2o] computing centred-square stats (fixed)...")
    mF2, M2F2, NF2 = secord_streaming(f, mF)
    print("[tvla-2o] computing centred-square stats (random)...")
    mR2, M2R2, NR2 = secord_streaming(r, mR)

    varF = M2F2 / max(NF2 - 1, 1)
    varR = M2R2 / max(NR2 - 1, 1)
    se   = np.sqrt(varF / NF2 + varR / NR2 + 1e-30)
    t    = (mF2 - mR2) / se
    return t, NF2, NR2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixed",  required=True)
    ap.add_argument("--random", required=True)
    ap.add_argument("--out",    required=True)
    ap.add_argument("--threshold", type=float, default=4.5)
    args = ap.parse_args()
    t, nf, nr = centred_square_t(args.fixed, args.random)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    np.savez_compressed(args.out, t=t, n_fixed=nf, n_random=nr, threshold=args.threshold)

    peak = float(np.abs(t).max())
    print(f"\n[tvla-2o] max|t|={peak:.2f} (threshold {args.threshold:.1f})")
    if peak > args.threshold:
        print(f"[tvla-2o] 2nd-order leakage DETECTED -- masking is real (good).")
    else:
        print(f"[tvla-2o] no 2nd-order leakage -- WARNING: masking may be ineffective.")


if __name__ == "__main__":
    main()
