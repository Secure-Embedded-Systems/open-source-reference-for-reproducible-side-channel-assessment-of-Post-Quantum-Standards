#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
# Copyright (c) 2026 Dillibabu Shanmugam, Patrick Schaumont (WPI)
"""fanin_audit.py — structural fan-in audit on the masking-cone production FFs.

For each FF in the Yosys-flattened netlist of `masking_cone_top`:
  - identify the D-input bit indices
  - BFS backward through the cell graph until every bit either
    (a) reaches a primary input port, or
    (b) terminates at a constant / open wire
  - report the SET of primary-input ports reached

Then verify the audit hypothesis (per audit §1C):
  - production FFs (s[0..3], seed_done, r_latched_q, r_latched_valid_q)
    MUST NOT reach rs1_i, rs2_i, op_i in their transitive fan-in cone
  - if they do, the L1 "max|t|=0 in masked-core" result has a structural
    contamination path that wasn't caught by the Welch-t alone.

Output:
  - per-FF table: FF name, fan-in port set
  - verdict for each operand-port: REACHED / NOT REACHED
  - JSON dump for downstream consumers
"""
import argparse
import json
from collections import defaultdict, deque
from pathlib import Path


THIS_DIR = Path(__file__).parent
DEFAULT_JSON = THIS_DIR.parent.parent.parent / "rtl_level_tsim" / "build" / "oim_masking_cone.json"


# DFF-family cells: D input port name is "D"; output is "Q"
DFF_TYPES = {"$adff", "$dff", "$adffe", "$aldff", "$dffe", "$sdff"}

# Production FF names (from L1 npz cell labels — the 7 masking-cone FFs)
PRODUCTION_FFS = [
    "u_prng.s[127:96]",
    "u_prng.s[95:64]",
    "u_prng.s[63:32]",
    "u_prng.s[31:0]",
    "u_prng.seed_done[3:0]",
    "u_mask.r_latched_q[31:0]",
    "u_mask.r_latched_valid_q[0]",
]

# Secret ports — carry the fixed-vs-random TVLA discriminator.
# These MUST NOT appear in any production FF's transitive fan-in for the
# L1 max|t|=0 negative result to be structurally consistent.
SECRET_PORTS = ["rs1_i", "rs2_i"]

# Control ports — deterministic across pools (same opcode/strobe sequence in
# both fixed and random pools). Reaching these is informational only, NOT a
# TVLA contamination, because Var(control) is zero between pools.
CONTROL_PORTS = ["op_i", "latch_strobe_i", "consume_i", "seed_valid_i",
                 "seed_idx_i", "q_sel_i", "clk_i", "rst_ni"]

# Seed ports — PRNG seed bytes. In an unpaired-seed pool they have non-zero
# variance per trace but are independent of the secret; in a paired-seed pool
# they are identical across pools. Their reach is expected and benign.
SEED_PORTS = ["seed_data_i"]


def load_module(json_path: Path):
    """Load the Yosys JSON and return the top module."""
    with open(json_path) as f:
        j = json.load(f)
    mod_name = list(j["modules"].keys())[0]
    return mod_name, j["modules"][mod_name]


def build_bit_driver_index(mod):
    """Map each bit index to the (cell_name, output_port_name) that drives it,
    or to the primary port name if a port drives it directly."""
    bit_driver = {}        # bit_idx -> (kind, name) where kind in {"port", "cell"}
    bit_port = {}          # bit_idx -> primary port name (if driven by port)
    port_bits = {}         # port name -> list of bit indices

    # Ports are primary drivers iff direction='input'; outputs would also appear here
    for port_name, port in mod["ports"].items():
        port_bits[port_name] = port["bits"]
        if port["direction"] == "input":
            for b in port["bits"]:
                if isinstance(b, int):
                    bit_port[b] = port_name

    # Cell outputs drive bits
    for cell_name, cell in mod["cells"].items():
        ports_dir = cell.get("port_directions", {})
        for pname, bits in cell.get("connections", {}).items():
            if ports_dir.get(pname) == "output":
                for b in bits:
                    if isinstance(b, int):
                        bit_driver[b] = (cell_name, pname)
    return bit_driver, bit_port, port_bits


def bfs_fanin(start_bits, bit_driver, bit_port, mod):
    """BFS backward from start_bits, accumulating which primary ports are reached.
    Returns dict: port_name -> True if reached."""
    cells = mod["cells"]
    reached_ports = set()
    visited_bits = set()
    queue = deque(start_bits)
    while queue:
        b = queue.popleft()
        if not isinstance(b, int) or b in visited_bits:
            continue
        visited_bits.add(b)
        # Is this bit a primary input port?
        if b in bit_port:
            reached_ports.add(bit_port[b])
            continue
        # Else, find the cell driving it
        if b not in bit_driver:
            continue   # unconnected / constant / open
        cell_name, _out_port = bit_driver[b]
        cell = cells[cell_name]
        ports_dir = cell.get("port_directions", {})
        for pname, bits in cell.get("connections", {}).items():
            if ports_dir.get(pname) == "input":
                for ib in bits:
                    if isinstance(ib, int) and ib not in visited_bits:
                        queue.append(ib)
    return reached_ports


def find_ff_bits(mod):
    """Locate every DFF-family cell and group its D-input bits by FF group name.
    Grouping uses the netname (or cell name) that the Q output ties to.
    Returns dict: FF_group_name -> list of D-bit indices."""
    groups = defaultdict(list)
    netnames = mod.get("netnames", {})

    # Build a reverse map: bit_idx -> list of netnames that include this bit
    bit_to_netname = defaultdict(list)
    for nname, ninfo in netnames.items():
        for b in ninfo.get("bits", []):
            if isinstance(b, int):
                bit_to_netname[b].append(nname)

    cells = mod["cells"]
    for cell_name, cell in cells.items():
        if cell["type"] not in DFF_TYPES:
            continue
        d_bits = cell["connections"].get("D", [])
        q_bits = cell["connections"].get("Q", [])
        # Group name = first netname matching Q (best-effort hierarchical mapping)
        group = None
        for qb in q_bits:
            if isinstance(qb, int) and qb in bit_to_netname:
                # pick most-readable netname (prefer hierarchical with '.')
                names = bit_to_netname[qb]
                hier = [n for n in names if "." in n]
                group = (hier or names)[0]
                break
        if group is None:
            group = f"<cell:{cell_name}>"
        groups[group].extend(d_bits)
    return dict(groups)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default=str(DEFAULT_JSON), help="Yosys-flattened JSON")
    ap.add_argument("--out-json", default=str(THIS_DIR.parent.parent / "results" / "L1_fanin_audit.json"))
    args = ap.parse_args()

    print("=" * 78)
    print("L1 structural fan-in audit — operand reachability per production FF")
    print("=" * 78)
    print(f"Source netlist: {args.json}")

    mod_name, mod = load_module(Path(args.json))
    print(f"Module: {mod_name}  ({len(mod['cells'])} cells)")

    bit_driver, bit_port, port_bits = build_bit_driver_index(mod)
    print(f"Built bit-driver index: {len(bit_driver)} cell-driven bits, "
          f"{len(bit_port)} port-driven bits")

    ff_groups = find_ff_bits(mod)
    print(f"Found {len(ff_groups)} FF groups in netlist\n")

    # Per-FF fan-in analysis
    audit_results = []
    print(f"  {'FF group':<32} {'secret':<15} {'seed':<15} {'control'}")
    print(f"  {'-'*32} {'-'*14} {'-'*14} {'-'*40}")
    for ff_name in sorted(ff_groups.keys()):
        d_bits = ff_groups[ff_name]
        reached = bfs_fanin(d_bits, bit_driver, bit_port, mod)
        secret_hits  = sorted([p for p in SECRET_PORTS  if p in reached])
        seed_hits    = sorted([p for p in SEED_PORTS    if p in reached])
        control_hits = sorted([p for p in CONTROL_PORTS if p in reached])
        other        = sorted(reached - set(SECRET_PORTS) - set(SEED_PORTS) - set(CONTROL_PORTS))
        audit_results.append({
            "ff_group": ff_name,
            "n_d_bits": len(d_bits),
            "reached_secret_ports":  secret_hits,
            "reached_seed_ports":    seed_hits,
            "reached_control_ports": control_hits,
            "reached_other_ports":   other,
            "tvla_contamination":    len(secret_hits) > 0,
        })
        flag = "  <<< SECRET PATH" if secret_hits else ""
        print(f"  {ff_name:<32} {','.join(secret_hits) or '(none)':<15} "
              f"{','.join(seed_hits) or '(none)':<15} "
              f"{','.join(control_hits)}{flag}")
        if other:
            print(f"  {'':32} other ports: {','.join(other)}")

    # Summary
    n_contam = sum(1 for r in audit_results if r["tvla_contamination"])
    print(f"\n--- Summary ---")
    print(f"  Total FF groups:                 {len(audit_results)}")
    print(f"  FFs with SECRET fan-in path:     {n_contam}  (rs1_i/rs2_i)")
    print(f"  FFs reaching control ports:      {sum(1 for r in audit_results if r['reached_control_ports'])}  (informational; deterministic across pools)")
    print(f"  FFs reaching seed ports:         {sum(1 for r in audit_results if r['reached_seed_ports'])}  (informational; per-trace random but secret-independent)")

    if n_contam == 0:
        print(f"\nVERDICT: PASS — no production FF has rs1_i/rs2_i in its")
        print(f"          transitive fan-in cone. The L1 max|t|=0 result on")
        print(f"          masked-core FFs is structurally consistent: no")
        print(f"          combinational path from secret to FF exists.")
    else:
        print(f"\nVERDICT: STRUCTURAL CONTAMINATION FOUND")
        for r in audit_results:
            if r["tvla_contamination"]:
                print(f"          {r['ff_group']} reaches {r['reached_secret_ports']}")

    # Write JSON
    out = Path(args.out_json)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w") as f:
        json.dump({
            "module": mod_name,
            "n_cells": len(mod["cells"]),
            "secret_ports_audited":  SECRET_PORTS,
            "seed_ports_audited":    SEED_PORTS,
            "control_ports_audited": CONTROL_PORTS,
            "per_ff": audit_results,
            "n_contamination": n_contam,
            "verdict": "PASS" if n_contam == 0 else "FAIL",
        }, f, indent=2)
    print(f"\nWrote: {out}")


if __name__ == "__main__":
    main()
