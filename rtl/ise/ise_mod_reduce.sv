// ISE unit: modular reduction (Barrett for q=3329, Montgomery for q=8380417).

`include "sca_pqc_pkg.sv"

module ise_mod_reduce (
  input  wire        clk_i,
  input  wire        rst_ni,
  input  wire [5:0]  op_i,
  input  wire [31:0] rs1_i, rs2_i,
  output reg  [31:0] result_o
);

  // ========================================================================
  // Constants
  // ========================================================================
  // Prime + Montgomery constants come from sca_pqc_pkg.sv (single source).
  localparam [15:0] Q_K         = `MLKEM_Q;
  localparam [23:0] Q_D         = `MLDSA_Q;
  localparam [15:0] BARRETT_M_K = 16'd5039;           // ceil(2^24 / 3329)
  localparam [31:0] BARRETT_M_D = 32'd262400;         // ceil(2^41 / 8380417)
  localparam [15:0] MONT_NINV_K = `MLKEM_QINV_NEG_R16;
  localparam [31:0] MONT_NINV_D = `MLDSA_QINV_NEG_R32;

  // ========================================================================
  // Barrett Q = 3329  (Kyber)
  // prod = rs1 * 5039,  t = prod >>> 24,  r = rs1 - t*3329,  normalize
  // ========================================================================
  (* use_dsp = "no" *) wire signed [31:0] s_rs1_k    = $signed(rs1_i);
  (* use_dsp = "no" *) wire signed [47:0] bk_prod    = s_rs1_k * $signed({1'b0, BARRETT_M_K});
  wire signed [31:0] bk_t    = bk_prod >>> 24;
  (* use_dsp = "no" *) wire signed [31:0] bk_r_raw  = s_rs1_k - bk_t * $signed({1'b0, Q_K});
  wire signed [31:0] bk_r     = (bk_r_raw >= $signed({1'b0, Q_K})) ? bk_r_raw - $signed({1'b0, Q_K}) :
                                 (bk_r_raw < 0)                     ? bk_r_raw + $signed({1'b0, Q_K}) :
                                                                      bk_r_raw;

  // ========================================================================
  // Barrett Q = 8380417  (Dilithium)
  // prod = rs1 * 262400,  t = prod >>> 41,  r = rs1 - t*Q,  normalize
  // ========================================================================
  (* use_dsp = "no" *) wire signed [31:0] s_rs1_d    = $signed(rs1_i);
  (* use_dsp = "no" *) wire signed [63:0] bd_prod    = s_rs1_d * $signed({1'b0, BARRETT_M_D});
  wire signed [31:0] bd_t    = bd_prod >>> 41;
  (* use_dsp = "no" *) wire signed [31:0] bd_r_raw  = s_rs1_d - bd_t * $signed({1'b0, Q_D});
  wire signed [31:0] bd_r     = (bd_r_raw >= $signed({1'b0, Q_D})) ? bd_r_raw - $signed({1'b0, Q_D}) :
                                 (bd_r_raw < 0)                     ? bd_r_raw + $signed({1'b0, Q_D}) :
                                                                      bd_r_raw;

  // ========================================================================
  // Montgomery Q = 3329  (Kyber): rd = (rs1 * rs2) * R^{-1} mod 3329
  //
  // Both rs1 and rs2 are interpreted as signed 16-bit operands in their
  // low halves. Computes the 32-bit signed product, then performs the
  // canonical 2-multiply Montgomery reduction:
  //
  //   prod = (int16)rs1 * (int16)rs2
  //   t    = (uint16)prod * QINV   (low 16 bits)
  //   diff = prod - t * Q
  //   res  = diff >> 16            (sign-extended to 32 bits)
  //
  // Result is in (-Q, Q) — canonical Kyber `montgomery_reduce()` output.
  // ========================================================================
  wire signed [15:0] mk_x    = rs1_i[15:0];
  wire signed [15:0] mk_y    = rs2_i[15:0];
  (* use_dsp = "yes" *) wire signed [31:0] mk_prod = mk_x * mk_y;
  wire        [15:0] mk_a16  = mk_prod[15:0];
  (* use_dsp = "no"  *) wire [31:0] mk_m_lo = mk_a16 * MONT_NINV_K;
  (* use_dsp = "no"  *) wire [31:0] mk_mq   = mk_m_lo[15:0] * Q_K;
  wire signed [31:0] mk_diff = mk_prod - $signed(mk_mq);
  wire signed [15:0] mk_hi   = mk_diff[31:16];
  wire        [31:0] mk_result = {{16{mk_hi[15]}}, mk_hi};

  // ========================================================================
  // Montgomery Q = 8380417  (Dilithium)
  // a64 = {rs2, rs1},  m32 = a64[31:0] * 4236238847,
  // mq64 = m32[31:0] * Q,  diff = a64 - mq64,  result = diff[63:32]
  // ========================================================================
  wire [63:0] md_a64  = {rs2_i, rs1_i};
  (* use_dsp = "no" *) wire [63:0] md_m32  = md_a64[31:0] * MONT_NINV_D;
  (* use_dsp = "no" *) wire [63:0] md_mq64 = md_m32[31:0] * {8'd0, Q_D};
  wire [63:0] md_diff = md_a64 - md_mq64;
  wire [31:0] md_result = md_diff[63:32];

  // ========================================================================
  // CADDQ  (Kyber):  (rs1 < 0) ? rs1 + 3329 : rs1
  // ========================================================================
  wire signed [31:0] caddq_in = $signed(rs1_i);
  wire [31:0] caddq_result = (caddq_in < 0) ? rs1_i + {16'd0, Q_K} : rs1_i;

  // ========================================================================
  // Output mux
  // ========================================================================
  always @(*) begin
    result_o = 32'd0;
    case (op_i)
      `OP_BARRETT_K: result_o = bk_r[31:0];
      `OP_MONT_K:    result_o = mk_result;
      `OP_CADDQ:     result_o = caddq_result;
      `OP_BARRETT_D: result_o = bd_r[31:0];
      `OP_MONT_D:    result_o = md_result;
      default:       result_o = 32'd0;
    endcase
  end

endmodule
