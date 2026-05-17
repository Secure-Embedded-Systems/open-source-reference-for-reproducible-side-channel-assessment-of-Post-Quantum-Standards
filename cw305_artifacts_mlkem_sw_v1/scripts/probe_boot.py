"""probe_boot.py -- diagnostic readout for the mlkem-sw widetrig firmware.

Programs the supplied bitstream onto the CW305 and polls MBOX[0..3] for
`poll_s` seconds, printing every transition.  The crt0_mlkem_sw.S phase
markers map to MBOX[3] as:

    0x00000000  : CPU never reached _start (BRAM init or boot-gate failure)
    0xCAFE0001  : _start entered, a0 = MBOX base
    0xCAFE0002  : mtvec installed
    0xCAFE0003  : stack pointer set
    0xCAFE0004  : __global_pointer$ loaded
    0xCAFE0005  : .bss zeroed
    0xCAFE0006  : about to call main()
    0xCAFE0007  : main() returned (should never happen, decap loop is infinite)
    0x000000B0  : main() entered (before KeyGen)            -- lower 8 bits
    0x00000020  : keypair_derand done, decap loop armed     -- lower 8 bits
    0x?? 0021  : per-trace done sentinel                    -- lower 16 bits

If the diagnostic trap fired, MBOX[3] == 0xDEADBEEF and MBOX[0..2] carry
mcause / mepc / mtval respectively (set by _vectors in crt0_mlkem_sw.S).
"""

import argparse
import sys
import time

import chipwhisperer as cw


def _mbox(target, idx: int) -> int:
    return int.from_bytes(target.fpga_read(0x0B + idx, 4), "little")


def _decode_phase(mb3: int) -> str:
    if mb3 == 0xDEADBEEF:
        return "TRAP (see MBOX[0..2] for mcause/mepc/mtval)"
    if (mb3 & 0xFFFF0000) == 0xCAFE0000:
        step = mb3 & 0xFFFF
        names = {
            0x0001: "_start entered",
            0x0002: "mtvec installed",
            0x0003: "stack set",
            0x0004: "gp loaded",
            0x0005: ".bss zeroed",
            0x0006: "about to call main",
            0x0007: "main returned (unexpected)",
            0x00B0: "main entered, pre-KeyGen",
            0x00B1: "main: post-KeyGen",
            0x00B2: "main: post-Encap",
            0x00B3: "main: post-Decap",
            0x00BA: "main: cycle-baseline booted, run incomplete (sticky)",
            0x0010: "post-KeyGen, decap loop armed",
            # kem.c::mlk_kem_keypair_derand wrapper markers
            0x2001: "kem_keypair_derand: entered",
            0x2002: "kem_keypair_derand: indcpa returned",
            0x2003: "kem_keypair_derand: about to hash_h",
            0x2004: "kem_keypair_derand: about to PCT",
            0x2005: "kem_keypair_derand: returning",
            # indcpa.c::mlk_indcpa_keypair_derand subphases
            0x10AA: "indcpa_keypair: stub-entered (prologue OK, body skipped)",
            0x1001: "indcpa_keypair: entered",
            0x1002: "indcpa_keypair: stack frame allocated",
            0x1003: "indcpa_keypair: about to hash_g",
            0x1004: "indcpa_keypair: about to gen_matrix (x4 rejection sampling)",
            0x1005: "indcpa_keypair: about to keypair_getnoise_eta1",
            0x1006: "indcpa_keypair: about to NTT(skpv)",
            0x1007: "indcpa_keypair: about to NTT(e)",
            0x1008: "indcpa_keypair: about to mulcache_compute",
            0x1009: "indcpa_keypair: about to matvec_mul",
            0x100A: "indcpa_keypair: about to polyvec_tomont",
            0x100B: "indcpa_keypair: about to add + reduce",
            0x100C: "indcpa_keypair: about to pack sk/pk",
            0x100D: "indcpa_keypair: fully done",
        }
        if step in names:
            return f"crt0/main/mlk phase: {names[step]}"
        return f"phase: 0xCAFE{step:04X} (unknown)"
    if mb3 == 0:
        return "no firmware execution (CPU stuck or BRAM not initialised)"
    # Decap-loop label: upper 16 bits = pool/B-index, lower 16 bits sentinel.
    low = mb3 & 0xFFFF
    if low == 0x0021:
        pool = (mb3 >> 16) & 0x1
        b_ix = (mb3 >> 24) & 0xFF
        return f"decap-loop trace done (pool={'B' if pool else 'A'}, b_ix={b_ix})"
    return f"unrecognised: 0x{mb3:08X}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bitstream", required=True)
    ap.add_argument("--target-clock-mhz", type=float, default=16.0,
                    help="default 16 MHz (cv32e40x_core max ~18.5 MHz)")
    ap.add_argument("--poll-s", type=float, default=10.0,
                    help="seconds to poll MBOX (default 10 s)")
    ap.add_argument("--poll-period-s", type=float, default=0.05,
                    help="MBOX poll period (default 50 ms)")
    args = ap.parse_args()

    print(f"[probe] programming {args.bitstream} onto CW305 ...")
    t = cw.target(None, cw.targets.CW305, fpga_id="35t",
                  bsfile=args.bitstream, force=True)
    t.pll.pll_enable_set(True)
    t.pll.pll_outenable_set(True, 1)
    t.pll.pll_outfreq_set(args.target_clock_mhz * 1e6, 1)
    time.sleep(0.5)
    m = int.from_bytes(t.fpga_read(0x06, 4), "little")
    print(f"[probe] magic = 0x{m:08X} ({'SP01' if m == 0x53503031 else 'BAD'})")

    print(f"[probe] polling MBOX[0..3] for {args.poll_s:.1f}s "
          f"(period {args.poll_period_s*1e3:.0f} ms) ...")
    prev = (None, None, None, None)
    t0 = time.time()
    transitions = 0
    last_seen = prev
    # Track every distinct MBOX(3) value ever seen (even if a marker fires
    # and is overwritten before the next poll, we want to know it was hit).
    m3_seen = {}  # value -> (first_seen_ms, count)
    while time.time() - t0 < args.poll_s:
        m0 = _mbox(t, 0)
        m1 = _mbox(t, 1)
        m2 = _mbox(t, 2)
        m3 = _mbox(t, 3)
        cur = (m0, m1, m2, m3)
        now_ms = (time.time() - t0) * 1e3
        if m3 not in m3_seen:
            m3_seen[m3] = [now_ms, 1]
        else:
            m3_seen[m3][1] += 1
        if cur != prev:
            print(f"  +{now_ms:7.1f} ms  "
                  f"M0=0x{m0:08X}  M1=0x{m1:08X}  M2=0x{m2:08X}  M3=0x{m3:08X}  "
                  f"-> {_decode_phase(m3)}")
            prev = cur
            transitions += 1
        last_seen = cur
        time.sleep(args.poll_period_s)

    m0, m1, m2, m3 = last_seen
    print(f"[probe] final state: MBOX[0..3] = "
          f"0x{m0:08X} 0x{m1:08X} 0x{m2:08X} 0x{m3:08X}")
    print(f"[probe] {transitions} transition(s) observed")
    print(f"[probe] {len(m3_seen)} distinct MBOX(3) value(s):")
    for v, (first_ms, cnt) in sorted(m3_seen.items(), key=lambda kv: kv[1][0]):
        print(f"   +{first_ms:7.1f} ms  M3=0x{v:08X}  seen {cnt:5d}x  "
              f"-> {_decode_phase(v)}")
    if m3 == 0xDEADBEEF:
        print(f"[probe] TRAP CAUGHT: mcause=0x{m0:08X}  mepc=0x{m1:08X}  "
              f"mtval=0x{m2:08X}")
        # Common mcause values for CV32E40X:
        cause_id = m0 & 0x7FFFFFFF
        intr = (m0 >> 31) & 1
        cause_names = {
            0x0: "instruction address misaligned",
            0x1: "instruction access fault",
            0x2: "illegal instruction",
            0x3: "ebreak",
            0x4: "load address misaligned",
            0x5: "load access fault",
            0x6: "store/AMO address misaligned",
            0x7: "store/AMO access fault",
            0x8: "ECALL from U-mode",
            0xB: "ECALL from M-mode",
        }
        nm = cause_names.get(cause_id, f"unknown ({cause_id})")
        print(f"[probe]   interrupt={intr}  exception code = 0x{cause_id:X}  ({nm})")

    t.dis()
    return 0


if __name__ == "__main__":
    sys.exit(main())
