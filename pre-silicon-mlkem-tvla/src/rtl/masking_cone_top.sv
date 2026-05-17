// =========================================================================
// rtl_level_tsim/scripts/masking_cone_top.sv
//
// Synthetic wrapper module that instantiates ise_mask_ops + ise_prng and
// exposes their combined I/O as primary ports of a tiny top module.  This
// is the scope-cut artefact for the camera-ready per the senior researcher's
// roadmap M1: a small acyclic feed-forward cone that we can synthesize
// without dragging in cv32e40x's controller-bypass SCC, the OBI demux, or
// any of the full-SoC bring-up scaffolding.
//
// Target post-flatten: ≤200 FFs, 0 non-trivial SCCs, <60 layers.
// Abort: >250 FFs (signals we picked the wrong cut).
// =========================================================================

module masking_cone_top (
    input  wire        clk_i,
    input  wire        rst_ni,

    // PRNG seed interface (driven by the trace-replay harness; in the full SoC
    // these would be driven by SEED_PRNG firmware instructions)
    input  wire        seed_valid_i,
    input  wire [31:0] seed_data_i,
    input  wire [1:0]  seed_idx_i,
    input  wire        consume_i,

    // Mask-op stimulus (the cone's primary input — these mirror the operand
    // bundle that the cv32e40x dispatches to ise_mask_ops via CV-X-IF)
    input  wire [5:0]  op_i,
    input  wire [31:0] rs1_i,
    input  wire [31:0] rs2_i,
    input  wire        q_sel_i,         // 0=Q_KEM(3329), 1=Q_DSA(8380417)
    input  wire        latch_strobe_i,  // pulsed on *_LATCH ops by top-level

    // Cone outputs
    output wire [31:0] result_o,
    output wire        trap_o,
    output wire        prng_ready_o
);

  // -----------------------------------------------------------------------
  // PRNG instance
  // -----------------------------------------------------------------------
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

  // -----------------------------------------------------------------------
  // Masking-ops instance
  // -----------------------------------------------------------------------
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

endmodule
