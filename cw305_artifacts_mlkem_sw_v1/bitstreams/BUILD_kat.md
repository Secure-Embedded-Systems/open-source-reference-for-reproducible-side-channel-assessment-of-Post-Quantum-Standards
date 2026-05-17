# mlkem-sw KAT bitstream — built against artifact RTL

* Bitstream: `sca_pqc_cw305_mlkem_sw_kat_artifactrtl.bit` (2 192 123 B, md5 `7bab76e1f375b9b2fb19a280bc174730`)
* ELF: `firmware_mlkem_sw_kat_artifactrtl.elf` (31 112 B)
* Built: 2026-05-17 ~00:15 local
* Parent repo HEAD at build time: `46dbacf` (master, ahead of origin/master by 1 commit, working tree dirty per `git status`)

## Provenance

| Component | Source |
|---|---|
| RTL (SCA_PQC + ISE + OBI) | `../cw305_artifacts_2026-05-15/rtl/` (provenance commit `d3d21d9`, "Initial artefact snapshot 2026-05-15"), staged into `rtl/` via copy during build |
| Working-tree RTL with uncommitted edits | preserved at `rtl.workingtree/` for restoration |
| SoC top | `integration/sca_pqc_cw305_top.sv` (working-tree, uncommitted edits) |
| Firmware C | `integration/firmware/mlkem_sw_baseline/src/main_sw_cw305.c` |
| mlkem-native | `integration/firmware/mlkem_sw_baseline/upstream/mlkem` (portable-C SCU) |
| crt0 | `integration/firmware/mlkem1024/crt0.S` = diagnostic crt0 with 8 phase markers (copied from `tvla_mlkem_sw/firmware/src/crt0_mlkem_sw.S`) — generic crt0 backed up as `crt0.S.generic_bak` |
| Linker script | `integration/firmware/mlkem1024/link.ld` (unchanged) |

## ISE invariant

`riscv64-unknown-elf-objdump -d build/firmware.elf` reports **0** Custom-3 opcodes (`0x7B`). Makefile recipe asserts this and fails the build if non-zero. So the bitstream contains the SCA_PQC coprocessor in fabric, but the firmware never issues any custom instruction — exact same arrangement as the `mlkem-native` (no-ISE) row in `RUNBOOK_AREA.md`.

## ELF stats

```
text     data    bss    total
23774       0   6368   30142  bytes  (0x75be)
```

Key linker symbols:
```
_start            = 0x10000080
__bss_start       = 0x10005d40
__bss_end         = 0x10007620   (.bss span = 0x18e0 = 6368 B)
__global_pointer$ = 0x10006540
__stack_top       = 0x1000fff0
```

Largest BSS occupants (from `main_sw_cw305.c:134-138`):
```
pk[1568]   1568 B
sk[3168]   3168 B
ct[1568]   1568 B
ss_e[32], ss_d[32]   64 B
              ≈ 6368 B  ✓
```

## P&R timing closure (key result)

```
soc_clk                       30.92 MHz  PASS at 20.00 MHz
i_EF_UART_AHBL.clk            83.79 MHz  PASS at 20.00 MHz
usb_clk_buf                  163.11 MHz  PASS at 20.00 MHz
i_greyhound_soc.cv32e40x_core.clk
                              20.46 MHz  PASS at 20.00 MHz   <-- artifact RTL meets timing
```

Earlier working-tree-RTL builds in `tvla_mlkem_sw/bitstreams/` reported `cv32e40x_core.clk` at 14.9–18.5 MHz (timing FAIL). RTL drift on the working tree appears to be the cause.

## Toolchain (resolved at build time from $PATH or env)

* `yosys` (oss-cad-suite distribution recommended)
* `nextpnr-xilinx` (built from source against `prjxray-db`)
* `fasm2frames` (prjxray)
* `xc7frames2bit` (prjxray)
* `riscv64-unknown-elf-gcc` (GCC 10+, RV32IMC newlib)
* `prjxray-db` (artix7 / `xc7a35tftg256-2`)

## Mailbox protocol (firmware → host)

| MBOX | Address | Meaning |
|---|---|---|
| `[0]` | `0x50000200` | KeyGen cycles (mcycle delta) |
| `[1]` | `0x50000204` | Encap cycles |
| `[2]` | `0x50000208` | Decap cycles |
| `[3]` | `0x5000020C` | sentinel: `0xCAFE0001` PASS, `0xCAFE0002` FAIL, `0xCAFE00BA` running, `0xCAFE00B0..B3` phase markers from `main()`, `0xCAFE0001..0007` from diagnostic crt0, `0xDEADBEEF` trap (mcause/mepc/mtval in MBOX[0..2]) |

## Reproducing the probe

```sh
python3 tvla_mlkem_sw/scripts/probe_boot.py \
    --bitstream ../cw305_artifacts_2026-05-15/bitstreams/sca_pqc_cw305_mlkem_sw_kat_artifactrtl.bit \
    --target-clock-mhz 16 --poll-s 60
```

A healthy KAT run should walk through `0xCAFE0001 … 0007` (boot), then `0xCAFE00B0 → B1 → B2 → B3` (KeyGen → Encap → Decap), and end at `0xCAFE0001` (PASS). KeyGen / Encap / Decap cycle counts appear in MBOX[0..2].

## Build log

`build_mlkem_sw_kat_artifactrtl.log` (≈ 2.6 MB) in this same directory.
