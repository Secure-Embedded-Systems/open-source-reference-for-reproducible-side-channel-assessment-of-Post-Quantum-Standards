# mlkem-sw TVLA widetrig bitstream — built against artifact RTL

* Bitstream: `sca_pqc_cw305_mlkem_sw_widetrig_artifactrtl.bit` (2 192 123 B, md5 `6b831e4e045a9e16d51157883f905215`)
* ELF: `firmware_mlkem_sw_widetrig_artifactrtl.elf` (39 316 B, md5 `ede1b6f5883f7874a77535e7313a8948`)
* Built: 2026-05-17 ~00:20 local (clean build with `make clean` first — earlier incremental `make all FW=mlkem_sw_widetrig` was a no-op because the KAT firmware.elf already existed and make didn't see the FW change as a dependency)

## Provenance

| Component | Source |
|---|---|
| RTL | `../cw305_artifacts_2026-05-15/rtl/` (commit `d3d21d9`), staged into `rtl/` by copy |
| SoC top | `integration/sca_pqc_cw305_top.sv` (working-tree) |
| Firmware C | `integration/firmware/mlkem_sw_baseline/src/main_sw_cw305_widetrig.c` (note: this is the *integration* mirror, not the one in `tvla_mlkem_sw/firmware/src/` — the two differ; the integration variant declares `s_pk` in addition to `s_sk` / `s_ss`) |
| mlkem-native | `integration/firmware/mlkem_sw_baseline/upstream/mlkem` (portable-C SCU) |
| Saarinen CCA-PC pool | `integration/firmware/mlkem1024_fips203/include/saarinen_ct_pool_1024_v2.h` |
| crt0 | `integration/firmware/mlkem1024/crt0.S` = diagnostic crt0 (8 phase markers) |

## ISE invariant

`riscv64-unknown-elf-objdump -d build/firmware.elf` → **0** Custom-3 opcodes. Same arrangement as KAT: ISE present in fabric, never issued by firmware.

## ELF stats

```
text     data    bss    total
32222       0   4768   36990  bytes  (0x907e)
```

Larger `.text` than KAT (32 222 vs 23 774) because the widetrig firmware pulls in `saarinen_ct_pool_1024_v2.h` — 10 × 1568-byte CT_B vectors + CT_A — as `const` rodata that ends up in `.text`.

Key linker symbols:
```
_start            = 0x10000080
__bss_start       = 0x10007e40
__bss_end         = 0x100090e0   (.bss span = 0x12a0 = 4768 B)
__global_pointer$ = 0x10008640
__stack_top       = 0x1000fff0
```

BSS occupants:
```
s_sk  3168 B   (precomputed FIPS-203 sk = TVEC_OUT_SK, copied at boot)
s_pk  1568 B   (declared but unused by decap loop — held for parity)
s_ss    32 B   (decap output buffer)
              ≈ 4768 B  ✓
```

## P&R timing

```
soc_clk                           30.92 MHz  PASS at 20.00 MHz
i_greyhound_soc.cv32e40x_core.clk 20.46 MHz  PASS at 20.00 MHz
```

## Mailbox protocol (widetrig)

| MBOX | Meaning |
|---|---|
| `[0]` | `0xCAFE0010` sticky boot marker (set after sk load, before decap loop) |
| `[1]` | TIO4 trigger bit — pulsed inside `mlk_poly_tomsg` (the secret-bit-extraction operation, share-recombine window) |
| `[2]` | running trace_idx counter |
| `[3]` | `[31:24]` B-pool index (1..10 when pool=B, 0 when pool=A); `[16]` pool A/B coin (0=A, 1=B); `[15:0]` = `0x0021` trace-done sentinel; `0xCAFE0001..0007` boot phase markers; `0xCAFE00B0`/`0x00000020` early-main / decap-loop-armed; `0xDEADBEEF` trap |

## Reproducing probe + capture

Probe (smoke test, 30 s):
```sh
python3 tvla_mlkem_sw/scripts/probe_boot.py \
    --bitstream ../cw305_artifacts_2026-05-15/bitstreams/sca_pqc_cw305_mlkem_sw_widetrig_artifactrtl.bit \
    --target-clock-mhz 16 --poll-s 30
```

A healthy widetrig run walks `0xCAFE0001..0007` → `0xCAFE00B0` → `0xCAFE0010` (boot-ready, decap loop armed) and then settles into trace-done sentinels with `0x..0021` in the low 16 bits and an A/B pool label rotating in the upper bits.

TVLA capture (ChipWhisperer-Lite + CW305):
```sh
python3 sca/host/capture_cw305_saarinen.py \
    --bitstream ../cw305_artifacts_2026-05-15/bitstreams/sca_pqc_cw305_mlkem_sw_widetrig_artifactrtl.bit \
    --target-clock-mhz 16 --adc-mhz 64 --presamples 250 --samples 6000 --gain 25 \
    --n-traces 5000 --out traces/mlkem_sw_widetrig_set
```
(Argument names are illustrative — adapt to the actual `capture_cw305_saarinen.py` CLI in your tree.)

## Build log

`build_mlkem_sw_widetrig_artifactrtl.log` (≈ 2.6 MB) in this same directory.
