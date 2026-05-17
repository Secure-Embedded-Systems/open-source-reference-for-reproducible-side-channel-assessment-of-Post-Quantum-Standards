#!/usr/bin/env python3
"""
ChipWhisperer-Lite / CW305 capture loop for Saarinen CCA-PC TVLA --
free-running variant (no host->FPGA write path required).

Firmware (main_saarinen_naive.c / main_saarinen_masked.c) loops decap(CT_A) /
decap(CT_B) continuously and publishes its trace index in MBOX[2].  The host
arms the scope, captures one TIO4 rising edge, then reads MBOX[2] to learn
which side this trace belongs to.

trace_idx parity tells us the side:
    even -> CT_A (Set A: valid under sk_fixed)
    odd  -> CT_B (Set B: invalid under sk_fixed, FO verify fails)

Drops: if MBOX[2] advances by k > 1 between captures we lost k-1 traces;
recover by re-arming and re-checking.  Duplicates: if MBOX[2] does NOT
advance, the scope captured but firmware hasn't completed the next decap
yet; drop that capture and re-arm.
"""
from __future__ import annotations
import argparse, os, sys, time
import numpy as np


def program_and_open(bitstream: str, target_clock_mhz: float,
                     presamples: int, samples: int, gain_db: int):
    import chipwhisperer as cw  # type: ignore
    target = cw.target(None, cw.targets.CW305,
                       fpga_id="35t", bsfile=bitstream, force=True)
    target.pll.pll_enable_set(True)
    target.pll.pll_outenable_set(True, 1)
    target.pll.pll_outfreq_set(int(target_clock_mhz * 1e6), 1)
    time.sleep(1.0)

    scope = cw.scope()
    scope.default_setup()
    scope.gain.gain = int(gain_db)
    scope.adc.samples = int(samples)
    scope.adc.presamples = int(presamples)
    scope.adc.basic_mode = "rising_edge"
    scope.clock.adc_src = "extclk_x4"
    scope.clock.clkgen_freq = int(target_clock_mhz * 1e6)
    scope.io.tio4 = "high_z"
    scope.trigger.triggers = "tio4"
    return scope, target


def read_mbox(target, idx: int) -> int:
    return int.from_bytes(target.fpga_read(0x0B + idx, 4), "little")


def wait_boot(target, deadline_s: float = 120.0) -> int:
    t0 = time.time()
    while time.time() - t0 < deadline_s:
        m0 = read_mbox(target, 0)
        m3 = read_mbox(target, 3)
        if m0 == 0xCAFE0010 and (m3 & 0xFFFF) in (0x0020, 0x0021, 0x0022):
            return int(time.time() - t0)
        time.sleep(0.2)
    raise RuntimeError(
        f"firmware never reached boot-ready in {deadline_s:.0f}s "
        f"(last MBOX[0]=0x{m0:08X} MBOX[3]=0x{m3:08X})")


def capture_one_with_label(scope, target):
    """Arm the scope, wait for one trigger, then read MBOX[3] for the
    firmware-published pool label and MBOX[2] for trace_idx.  Returns
    (trace, fw_idx, pool_label) where pool_label is 0 for Set A and 1 for
    Set B.  Trace is per-trace DC-subtracted to remove slow drift."""
    scope.arm()
    if scope.capture():
        return None, read_mbox(target, 2), -1
    # Re-read MBOX[3] to be sure the firmware has finished writing the label
    # for THIS decap (not the previous one).  Two consecutive reads must agree.
    m3 = read_mbox(target, 3)
    time.sleep(0.0005)
    m3_2 = read_mbox(target, 3)
    m3 = m3 if m3 == m3_2 else m3_2
    fw_idx = read_mbox(target, 2)
    pool = (m3 >> 16) & 0x1                # bit 16: 0 = A, 1 = B
    raw = scope.get_last_trace()
    # Scale CW-Lite float [-0.5, 0.5] to int16 full range, then per-trace DC
    # subtract (reviewer fix: removes slow PSU/thermal drift correlated to time).
    tr_f = raw * 32767.0 * 2.0
    tr_f = tr_f - tr_f.mean()
    tr   = np.clip(tr_f, -32768, 32767).astype(np.int16)
    return tr, fw_idx, pool


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bitstream", required=True)
    ap.add_argument("--vectors", required=False, default=None,
                    help="optional Saarinen pool .npz, only used for "
                         "CT_A/CT_B sanity hex print")
    ap.add_argument("--N", type=int, default=1000,
                    help="number of A and B traces per pool")
    ap.add_argument("--target-clock-mhz", type=float, default=16.0,
                    help="default 16 MHz: PnR closes timing here (20 MHz "
                         "target hits cv32e40x_core.clk max ~17.2 MHz)")
    ap.add_argument("--presamples", type=int, default=250)
    ap.add_argument("--samples", type=int, default=6000)
    ap.add_argument("--gain-db", type=int, default=25)
    ap.add_argument("--out", required=True)
    ap.add_argument("--max-drops", type=int, default=200,
                    help="abort if more than this many traces are dropped")
    args = ap.parse_args()

    if args.vectors and os.path.exists(args.vectors):
        pool = np.load(args.vectors)
        if "CT_A" in pool:        # v2 layout: 1 fixed A + 10-pool B
            ctA = pool["CT_A"][:4].tobytes().hex()
            ctB = pool["CT_B_POOL"][0,:4].tobytes().hex()
            print(f"[capture] v2 pool sanity: CT_A[:4]={ctA}  CT_B[0][:4]={ctB}  "
                  f"HW(A)={int(pool['HW_A'])}  HW(B)={list(map(int, pool['HW_B_POOL']))}")
        elif "setA_ct" in pool:   # v1 layout: setA_ct/setB_ct (1000 each)
            print(f"[capture] v1 pool sanity: CT_A[0:4]="
                  f"{pool['setA_ct'][0,:4].tobytes().hex()} "
                  f"CT_B[0:4]={pool['setB_ct'][0,:4].tobytes().hex()}")

    print(f"[capture] programming {args.bitstream} onto CW305...")
    scope, target = program_and_open(args.bitstream, args.target_clock_mhz,
                                     args.presamples, args.samples, args.gain_db)
    boot_s = wait_boot(target)
    print(f"[capture] firmware ready after {boot_s}s")

    out_dir = os.path.dirname(args.out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    setA = np.empty((args.N, args.samples), dtype=np.int16)
    setB = np.empty((args.N, args.samples), dtype=np.int16)
    cnt_a = cnt_b = drops = 0
    last_idx = -1

    print(f"[capture] target N={args.N}/pool, samples={args.samples}, "
          f"pool label from MBOX[3][16] (PRNG-randomised A/B)")
    t0 = time.time()
    while cnt_a < args.N or cnt_b < args.N:
        trace, fw_idx, pool = capture_one_with_label(scope, target)
        if trace is None:
            continue

        if fw_idx == last_idx:
            continue
        if fw_idx > last_idx + 1:
            drops += fw_idx - last_idx - 1
            if drops > args.max_drops:
                print(f"\n[capture] aborting -- {drops} drops > "
                      f"--max-drops {args.max_drops}")
                break
        last_idx = fw_idx

        if pool == 0 and cnt_a < args.N:
            setA[cnt_a] = trace; cnt_a += 1
        elif pool == 1 and cnt_b < args.N:
            setB[cnt_b] = trace; cnt_b += 1

        if ((cnt_a + cnt_b) & 0x1F) == 0:
            elapsed = max(time.time() - t0, 1e-3)
            rate = (cnt_a + cnt_b) / elapsed
            eta  = (2 * args.N - cnt_a - cnt_b) / max(rate, 1e-3)
            print(f"  A={cnt_a}/{args.N}  B={cnt_b}/{args.N}  "
                  f"drops={drops}  rate={rate:.1f} t/s  ETA={eta/60:.1f} min",
                  end="\r")
    print()

    np.save(args.out + "_setA.npy", setA[:cnt_a])
    np.save(args.out + "_setB.npy", setB[:cnt_b])
    sz = (setA.nbytes + setB.nbytes) / 1e6
    print(f"[capture] wrote {args.out}_setA.npy ({cnt_a} traces) "
          f"and _setB.npy ({cnt_b} traces); {drops} dropped; {sz:.1f} MB")

    scope.dis(); target.dis()
    return 0 if min(cnt_a, cnt_b) >= args.N else 2


if __name__ == "__main__":
    sys.exit(main())
