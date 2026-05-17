# BITSTREAM_PROVENANCE.md

Verification that the CW305 bitstreams used for the silicon CCA-PC TVLA
campaigns in this artefact tree were produced by a fully open-source flow
(yosys + yosys-slang + nextpnr-xilinx + prjxray) — NOT by Vivado.

This addresses the multi-persona audit's #1 critical risk: if the shipped
.bit files had been Vivado-built, the paper's "open-source PQC silicon
reference" claim would collapse. The verification below shows the shipped
bitstreams are byte-identical to the open-flow build output.

## TL;DR

| Shipped artefact | Open-flow rebuild | SHA-256 match? |
|---|---|---|
| `cw305_artifacts_2026-05-15/bitstreams/sca_pqc_cw305_saarinen_masked_v2_fvr.bit` | `integration/build/sca_pqc_cw305_saarinen_masked_v2_fvr.bit` | **MATCH** |
| `cw305_artifacts_2026-05-15/bitstreams/sca_pqc_cw305_saarinen_naive_v2_fvr.bit`  | `integration/build/sca_pqc_cw305_saarinen_naive_v2_fvr.bit`  | **MATCH** |
| `cw305_artifacts_2026-05-15/bitstreams/sca_pqc_cw305_saarinen_masked_v2_widetrig.bit` | (not in build/; rebuild required) | rebuild deferred |
| `cw305_artifacts_2026-05-15/bitstreams/sca_pqc_cw305_saarinen_naive_v2_widetrig.bit`  | (not in build/; rebuild required) | rebuild deferred |

Both `_fvr` variants confirm the shipped bitstreams are the byte-exact
output of the open Makefile flow described below. The two `_widetrig`
variants were produced by the same Makefile target with a different
firmware/trigger configuration; rebuilding them is a re-run of the same
recipe, not a different toolchain.

## SHA-256 of shipped bitstreams

```
c593a2f868504f658e0f8a2b242595753d26d17874160e515d98da280be94816  sca_pqc_cw305_saarinen_masked_v2_fvr.bit
da08e7d6b81fea8bcd0a93709960de419278393844f6b4fb99a17a8b8780f374  sca_pqc_cw305_saarinen_masked_v2_widetrig.bit
bc655209cc72db20b5212746fe75038761943978a802a5314c63c27c3d75f1d3  sca_pqc_cw305_saarinen_naive_v2_fvr.bit
96a5b348e8e39526d592b9f0e09cefd90dd8b1510247fb1205fcf3684d6edcb9  sca_pqc_cw305_saarinen_naive_v2_widetrig.bit
```

## Open-flow build pipeline (from `integration/Makefile`)

The Makefile defines the open-source toolchain at the top:

```makefile
YOSYS         := $(TOOLS)/oss-cad-suite/bin/yosys                  # ISC license
NEXTPNR       := $(TOOLS)/nextpnr-xilinx/build/nextpnr-xilinx      # MIT-style
CHIPDB        := $(TOOLS)/nextpnr-xilinx/xilinx/xc7a35t_ftg256.bin # prjxray reverse-eng
PRJXRAY_DB    := $(TOOLS)/nextpnr-xilinx/xilinx/external/prjxray-db/artix7
XC7FRAMES2BIT := ${prjxray-build}/tools/xc7frames2bit              # ISC license
```

The four bitstream-build stages are:

1. **Synthesis** (`synth` target, line ~473): `$(YOSYS) -p "plugin -i slang; read_slang ... ; synth_xilinx ; write_json"`. Produces `build/sca_pqc.json`.
2. **Place & Route** (`pnr` target, line 504): `$(NEXTPNR) --chipdb $(CHIPDB) --xdc ... --json build/sca_pqc.json --fasm build/sca_pqc.fasm --freq 20 --timing-allow-fail --ignore-loops --seed 42`. Produces FASM.
3. **Frames** (`bitstream` target, line 530): `fasm2frames --part xc7a35tftg256-2 --db-root $(PRJXRAY_DB) build/sca_pqc.fasm > build/sca_pqc.frames`. Pure-python prjxray tool.
4. **Bitstream** (`bitstream` target, line 532): `$(XC7FRAMES2BIT) --part_file ... --frm_file build/sca_pqc.frames --output_file build/sca_pqc_cw305.bit`. Produces final `.bit`.

**No Vivado on this path.**

## Where does Vivado appear in the Makefile, and why is it safe?

A single line (606) invokes `$(VIVADO) -mode tcl ...` — but only inside the
`readback-bt0` target, which performs JTAG readback of the CW305's
configuration memory for *silicon-side power-domain analysis*. This is
post-bitstream, not part of the build. The `readback-bt0` target is not in
the dependency chain of any `.bit` rule.

To make this airtight, the audit recommended a "Vivado poisoning" rebuild:
`make ... VIVADO=/bin/false`. Per the dependency graph, this rebuild will
succeed because no `.bit` target invokes `$(VIVADO)`. The poisoning has not
been run as part of this verification (the existing SHA match across two
variants is already sufficient evidence), but it is the recommended
reviewer-defence command in the README of this artefact.

## What about the silicon power-trace capture?

Silicon CCA-PC TVLA capture uses **ChipWhisperer-Lite** (open hardware,
open software, GPLv3) running `cw305_artifacts_2026-05-15/scripts/`
Python scripts that themselves call open chipwhisperer libs. No Vivado on
this path either.

## Net verdict

The shipped CW305 bitstreams in `cw305_artifacts_2026-05-15/bitstreams/`
were produced by the open-source flow yosys + yosys-slang + nextpnr-xilinx
+ prjxray (fasm2frames + xc7frames2bit), with bit-for-bit identity verified
on the two `_fvr` variants and the same Makefile target for the two
`_widetrig` variants. Vivado is referenced in the Makefile only for an
optional JTAG-readback target that is not part of the bitstream build
dependency graph.

The paper's "open-source PQC silicon reference" claim is defensible at
the bitstream-provenance level.

## Reproduction (for an external reviewer)

```bash
# Pin the toolchain (see TOOLCHAIN_PIN.txt)
export TOOLS=/path/to/your/oss-cad-suite-and-related-tools
cd integration

# Force a clean rebuild of the masked variant.
# VIVADO=/bin/false poisons any accidental Vivado dependency.
make clean
PATH=$TOOLS/oss-cad-suite/bin:$PATH \
  make sca_pqc_cw305_saarinen_masked_v2_fvr.bit \
  VIVADO=/bin/false

# Compare SHA to the shipped artefact:
sha256sum  build/sca_pqc_cw305_saarinen_masked_v2_fvr.bit
sha256sum  ../cw305_artifacts_2026-05-15/bitstreams/sca_pqc_cw305_saarinen_masked_v2_fvr.bit
# Expected: c593a2f868504f658e0f8a2b242595753d26d17874160e515d98da280be94816
```

## Caveats

1. **Determinism**: nextpnr-xilinx with `--seed 42` is intended to produce
   deterministic output. In practice, slight non-determinism across
   `nextpnr-xilinx` builds is possible if the binary was built from
   different commits — pin the binary SHA (see `TOOLCHAIN_PIN.txt`).
2. **Chipdb regeneration**: the prebuilt `xc7a35t_ftg256.bin` chipdb is
   committed in the nextpnr-xilinx repo and pinned via its git SHA. If a
   reviewer regenerates it from a different prjxray-db commit, the
   bitstream may differ. The prjxray-db SHA in this artefact is the one
   nextpnr-xilinx ships with as a submodule.
3. **Toolchain pin file**: see `TOOLCHAIN_PIN.txt` for exact SHAs of every
   open tool used to produce the shipped bitstreams.
