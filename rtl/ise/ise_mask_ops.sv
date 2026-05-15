// ISE unit: DOM masking primitives (SECMUL_STEP/LATCH/REUSE, MASK_REFRESH, AND_BLIND/LATCH/REUSE, MASKED_XOR).

`include "sca_pqc_pkg.sv"

module ise_mask_ops (
  input  wire        clk_i,
  input  wire        rst_ni,
  input  wire [5:0]  op_i,
  input  wire [31:0] rs1_i,
  input  wire [31:0] rs2_i,
  input  wire        q_sel_i,       // 0=Q_KEM(3329), 1=Q_DSA(8380417)
  input  wire [31:0] prng_random_i, // fresh randomness from PRNG
  input  wire        prng_ready_i,  // PRNG is seeded and ready
  input  wire        latch_strobe_i,// 1-cycle strobe asserted by top on the
                                    // result cycle of any *_LATCH op
  output reg  [31:0] result_o,
  output wire        trap_o         // pulses high if REUSE op fires while
                                    // r_latched_valid_q is low (atomicity violation)
);

  // ML-DSA constants come from sca_pqc_pkg.sv (single source of truth).
  // Aliases here just for terser local naming.
  localparam [23:0] Q_D         = `MLDSA_Q;
  localparam [31:0] MONT_NINV_D = `MLDSA_QINV_NEG_R32;

  // -----------------------------------------------------------------------
  // Latched random + valid bit for paired DOM gadgets (Gross-Mangard +r/-r).
  // Width 32 covers both Q_KEM (use [11:0]) and Q_DSA (use [22:0]) needs,
  // and full 32-bit for boolean AND_LATCH / AND_REUSE. Same register is
  // shared across SECMUL and AND pairs because firmware never interleaves
  // them within an unfinished gadget.
  //
  //   r_latched_valid_q ← 1 on any *_LATCH op, 0 on *_REUSE op consuming it
  //                       AND on any other PRNG-consuming op that would
  //                       implicitly overwrite the latched random.
  //   trap_o            ← high if a *_REUSE op fires while the valid bit is
  //                       low (i.e. REUSE without prior LATCH, or LATCH
  //                       clobbered by an intervening PRNG consumer).
  //   The coprocessor top maps trap_o to a CV-X-IF illegal-instruction
  //   exception, so the +r/-r cancellation invariant is enforced in
  //   silicon -- not in a firmware comment.
  // -----------------------------------------------------------------------
  (* keep = "true" *) (* keep_hierarchy = "true" *) reg [31:0] r_latched_q;
  (* keep = "true" *) (* keep_hierarchy = "true" *) reg        r_latched_valid_q;

  // Decode the pair-related opcodes for clarity in the always-block.
  wire is_latch_op    = (op_i == `OP_SECMUL_LATCH) || (op_i == `OP_AND_LATCH);
  wire is_reuse_op    = (op_i == `OP_SECMUL_REUSE) || (op_i == `OP_AND_REUSE);
  // Any non-LATCH PRNG-consuming op invalidates r_latched_valid_q so that a
  // included here -- without it the host emulator and silicon disagreed on
  // the LATCH; GET_RANDOM; REUSE sequence (host trapped, silicon would have
  // silently produced unrelated arithmetic since r_latched_q is stale).
  wire is_prng_other  = (op_i == `OP_SECMUL_STEP)  || (op_i == `OP_MASK_REFRESH) ||
                        (op_i == `OP_MASKED_AND)   || (op_i == `OP_GET_RANDOM);

  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      r_latched_q       <= 32'd0;
      r_latched_valid_q <= 1'b0;
    end else if (latch_strobe_i) begin
      r_latched_q       <= prng_random_i;
      r_latched_valid_q <= 1'b1;
    end else if (is_reuse_op || is_prng_other) begin
      // Consume on REUSE (paired) or invalidate on any other PRNG consumer
      // that would overwrite the latched random in a future LATCH cycle.
      r_latched_valid_q <= 1'b0;
    end
  end

  // -----------------------------------------------------------------------
  // SECMUL paths: shared Montgomery / Barrett datapaths for STEP, LATCH, and
  // REUSE.  An input mux selects the additive blinding source:
  //   *_STEP, *_LATCH  ->  +prng_random   (fresh entropy)
  //   *_REUSE          ->  -r_latched_q   (cancels +r from the matching LATCH)
  // datapath for REUSE separately added ~150 LUT for no functional reason.
  // -----------------------------------------------------------------------
  wire is_reuse_op_decode = (op_i == `OP_SECMUL_REUSE);

  // --- Q_KEM = 3329 path: SIGNED 16-bit × 16-bit multiply + Montgomery reduce ---
  //   prod_k = rs1_i[15:0] * rs2_i[15:0]  (unsigned)
  // which produced wrong results for negative int16 share values that
  // ML-KEM Barrett-centred arithmetic legitimately produces.  Host
  // emulator did int16-signed multiply and matched KAT in simulation,
  // but silicon diverged.  Now matches MONT_K's signed multiply exactly.
  wire signed [15:0] sk_x = rs1_i[15:0];
  wire signed [15:0] sk_y = rs2_i[15:0];
  (* use_dsp = "yes" *) wire signed [31:0] prod_k_signed = sk_x * sk_y;

  wire signed [16:0] addend_k_eff =
      is_reuse_op_decode ? -$signed({1'b0, r_latched_q[15:0]})
                         :  $signed({1'b0, prng_random_i[15:0]});
  wire signed [32:0] blinded_k_signed = $signed({prod_k_signed[31], prod_k_signed}) +
                                         {{16{addend_k_eff[16]}}, addend_k_eff};
  wire [31:0] blinded_k    = blinded_k_signed[31:0];
  wire [31:0] mk_m_lo      = blinded_k[15:0] * 16'd62209;       // QINV_KEM (low 16)
  wire [31:0] mk_mq        = mk_m_lo[15:0] * 32'd3329;
  wire signed [31:0] mk_diff = $signed(blinded_k) - $signed(mk_mq);
  wire signed [15:0] mk_hi   = mk_diff[31:16];
  wire [31:0] secmul_k_result = {{16{mk_hi[15]}}, mk_hi};

  // --- Q_DSA = 8380417 path: signed 32-bit factors, Montgomery reduce ---
  // R^{-1} factor that MONT_D applies, so SECMUL outputs and MONT_D outputs
  // were at incompatible scaling levels and could not be summed to recover
  // a*b in the DSA paired-DOM.  This path now does the same Montgomery
  // reduction as ise_mod_reduce.sv MONT_D (R = 2^32, NINV = 4236238847),
  // so dom_mul_dsa = (u00 + v01) + (u11 + v10) = (a*b)/R mod Q matches a
  // single fqmul -- compatible with the canonical ML-DSA NTT.
  // Inputs: factors (rs1, rs2) signed 32-bit.  Internal multiply is full
  // signed 64-bit (caller pre-reduces shares into (-Q, Q) so the product
  // fits in 47 bits).  After +prng / -r_latched blinding, we Mont-reduce.
  wire signed [31:0] addend_d_signed =
      is_reuse_op_decode ? -$signed({9'd0, r_latched_q[22:0]})
                         :  $signed({9'd0, prng_random_i[22:0]});
  wire signed [63:0] prod_d_64;
  wire signed [63:0] blinded_d_64;
  (* use_dsp = "no" *) wire [63:0] md_m32;
  (* use_dsp = "no" *) wire [63:0] md_mq64;
  wire signed [63:0] md_diff;
  wire signed [31:0] md_d_result;
  wire [31:0] secmul_d_result;

  assign prod_d_64    = $signed(rs1_i) * $signed(rs2_i);
  assign blinded_d_64 = prod_d_64 + {{32{addend_d_signed[31]}}, addend_d_signed};
  assign md_m32       = blinded_d_64[31:0] * MONT_NINV_D;
  assign md_mq64      = md_m32[31:0] * {8'd0, Q_D};
  assign md_diff      = blinded_d_64 - $signed({1'b0, md_mq64});
  assign md_d_result  = md_diff[63:32];
  // Output range after Montgomery: (-Q, Q); CADDQ in software brings it
  // into [0, Q) when an unmasked centered representative is needed.
  assign secmul_d_result = $unsigned(md_d_result);

  // Q selector: funct7[4] chooses KEM vs DSA path
  wire [31:0] secmul_result = q_sel_i ? secmul_d_result : secmul_k_result;

  // SECMUL_LATCH/REUSE share the same datapaths as SECMUL_STEP via the
  // is_reuse_op_decode mux above; the only architectural distinction is the
  // r_latched_q capture/consume side-effect controlled by latch_strobe_i.

  // -----------------------------------------------------------------------
  // MASK_REFRESH: re-randomize a single share
  //   q_sel_i=0: Boolean refresh (XOR with PRNG)
  //   q_sel_i=1: Arithmetic refresh ((rs1 + PRNG[11:0]) mod Q_KEM = 3329)
  //
  // For arithmetic refresh, the random addend is masked to 12 bits
  // (range 0..4095 > Q, so modular reduction is needed).
  // -----------------------------------------------------------------------
  wire [31:0] refresh_bool = rs1_i ^ prng_random_i;

  // Arithmetic refresh: (rs1 + random[11:0]) mod 3329
  wire [12:0] arith_sum;
  wire [31:0] refresh_arith;
  assign arith_sum    = rs1_i[11:0] + prng_random_i[11:0];
  assign refresh_arith = (arith_sum >= 13'd3329) ?
                         {19'd0, arith_sum - 13'd3329} : {19'd0, arith_sum};

  wire [31:0] refresh_result = q_sel_i ? refresh_arith : refresh_bool;

  // -----------------------------------------------------------------------
  // AND_BLIND (opcode: MASKED_AND): single share-pair AND with blinding
  //   result = (rs1 & rs2) ^ prng_random
  //
  // This is one building block of the DOM-AND protocol. Software must
  // invoke this 4 times (a0&b0, a0&b1, a1&b0, a1&b1) with fresh PRNG
  // each time, then recombine using MASKED_XOR. The PRNG is consumed
  // in the coprocessor top (prng_consume) to ensure independent randomness
  // for each invocation.
  // -----------------------------------------------------------------------
  wire [31:0] masked_and_result = (rs1_i & rs2_i) ^ prng_random_i;

  // AND_LATCH: same datapath as MASKED_AND; r_latched_q captures prng_random
  // when the top asserts latch_strobe_i. AND_REUSE XORs with the latched r,
  // no PRNG advance, so the cross-term randoms cancel on share recombination.
  wire [31:0] and_reuse_result = (rs1_i & rs2_i) ^ r_latched_q;

  // -----------------------------------------------------------------------
  // MASKED_XOR: linear XOR combining / share manipulation (no randomness)
  //   result = rs1 ^ rs2
  // Used to combine partial products in DOM recombination steps.
  // -----------------------------------------------------------------------
  wire [31:0] masked_xor_result = rs1_i ^ rs2_i;

  // -----------------------------------------------------------------------
  // Output MUX
  // -----------------------------------------------------------------------
  // Atomicity-violation trap: REUSE without a valid latched random.
  assign trap_o = is_reuse_op && !r_latched_valid_q;

  always @(*) begin
    case (op_i)
      `OP_SECMUL_STEP,
      `OP_SECMUL_LATCH,
      `OP_SECMUL_REUSE: result_o = secmul_result;        // shared mod-arith path
      `OP_MASK_REFRESH: result_o = refresh_result;
      `OP_MASKED_AND,
      `OP_AND_LATCH:    result_o = masked_and_result;    // (rs1 & rs2) ^ prng
      `OP_AND_REUSE:    result_o = and_reuse_result;     // (rs1 & rs2) ^ r_latched
      `OP_MASKED_XOR:   result_o = masked_xor_result;
      default:          result_o = 32'd0;
    endcase
  end

endmodule
