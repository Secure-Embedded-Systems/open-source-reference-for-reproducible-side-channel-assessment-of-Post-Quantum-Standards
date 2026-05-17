// =========================================================================
// rtl_level_tsim/scripts/masking_cone_top_leak.sv
//
// Positive-control variant of masking_cone_top.sv.  Identical to the
// production cone EXCEPT that three extra debug FFs are wired in as
// deliberate operand-leak observers, per audit §2C:
//
//   leakA_q  — trivial leak: captures rs1_i every cycle (no mask)
//   leakB_q  — first-order leak: captures rs1_i ^ rs2_i every cycle
//              (no PRNG mask; share-recombine-style XOR with no decorrelation)
//   leakC_q  — conditional leak: captures rs1_i only when op_i == SECMUL_LATCH
//              (tests cycle-resolution sensitivity at N=10k)
//
// Per audit §2C, these three FFs are always-on in this build (no runtime
// flag).  The production cone (masking_cone_top.sv) is the "no-leak"
// configuration, in a SEPARATE file.  This avoids the runtime-flag
// pitfall where the gating logic itself could subtly perturb FF state.
//
// Expected TVLA outcome:
//   leakA_q max|t|  >> 4.5  (every bit of rs1 bleeds)
//   leakB_q max|t|  >> 4.5  (every bit of rs1^rs2 bleeds)
//   leakC_q max|t|  >> 4.5  but only at cycle 6 (the LATCH cycle)
// =========================================================================

`include "sca_pqc_pkg.sv"

module masking_cone_top (
    input  wire        clk_i,
    input  wire        rst_ni,

    input  wire        seed_valid_i,
    input  wire [31:0] seed_data_i,
    input  wire [1:0]  seed_idx_i,
    input  wire        consume_i,

    input  wire [5:0]  op_i,
    input  wire [31:0] rs1_i,
    input  wire [31:0] rs2_i,
    input  wire        q_sel_i,
    input  wire        latch_strobe_i,

    output wire [31:0] result_o,
    output wire        trap_o,
    output wire        prng_ready_o
);

  wire [31:0] prng_random;
  wire        prng_ready;

  ise_prng u_prng (
      .clk_i        (clk_i),
      .rst_ni       (rst_ni),
      .seed_valid_i (seed_valid_i),
      .seed_data_i  (seed_data_i),
      .seed_idx_i   (seed_idx_i),
      .consume_i    (consume_i),
      .random_o     (prng_random),
      .ready_o      (prng_ready)
  );

  ise_mask_ops u_mask (
      .clk_i          (clk_i),
      .rst_ni         (rst_ni),
      .op_i           (op_i),
      .rs1_i          (rs1_i),
      .rs2_i          (rs2_i),
      .q_sel_i        (q_sel_i),
      .prng_random_i  (prng_random),
      .prng_ready_i   (prng_ready),
      .latch_strobe_i (latch_strobe_i),
      .result_o       (result_o),
      .trap_o         (trap_o)
  );

  assign prng_ready_o = prng_ready;

  // ----------------------------------------------------------------------
  // POSITIVE-CONTROL DEBUG FFs (audit §2C falsifiability suite)
  // ----------------------------------------------------------------------
  // SECMUL_LATCH opcode = 6'd41 (internal) — see sca_pqc_pkg.sv
  localparam logic [5:0] OP_SECMUL_LATCH = 6'd41;

  logic [31:0] leakA_q;       // trivial leak
  logic [31:0] leakB_q;       // first-order leak (no mask)
  logic [31:0] leakC_q;       // conditional leak (only on LATCH cycle)

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      leakA_q <= 32'd0;
      leakB_q <= 32'd0;
      leakC_q <= 32'd0;
    end else begin
      leakA_q <= rs1_i;
      leakB_q <= rs1_i ^ rs2_i;
      if (op_i == OP_SECMUL_LATCH) begin
        leakC_q <= rs1_i;
      end
    end
  end

  // Keep the leak FFs from being optimised away by giving them a
  // dummy fan-out into a `(* keep *)` wire.
  (* keep = "true" *) wire [31:0] leak_observe;
  assign leak_observe = leakA_q ^ leakB_q ^ leakC_q;

endmodule
