// ============================================================================
// SCA_Pro_PQC — NTT butterfly with optional twiddle Mont-multiply
// ============================================================================
//
// Legacy ops (regression-preserving):
//   OP_NTT_BFLY_CT, OP_NTT_BFLY_GS : sum/diff packing only
//
// New ops (twiddle multiply folded in):
//   OP_NTT_BFLY_CT_MUL : forward Cooley-Tukey
//     rs1 = a (low 16 bits, signed)
//     rs2 = packed { zeta[15:0], b[15:0] }
//     t   = mont_reduce(b * zeta)
//     out = { (a-t)[15:0], (a+t)[15:0] }
//
//   OP_NTT_BFLY_GS_MUL : inverse Gentleman-Sande
//     rs1 = a, rs2 = packed { zeta, b }
//     d   = a - b   (signed 16-bit, may wrap mod 2^16; the Mont reduction
//                    handles the modular reduction afterwards)
//     t   = mont_reduce(d * zeta)
//     out = { t[15:0], (a+b)[15:0] }
// ============================================================================

`include "sca_pqc_pkg.sv"

module ise_ntt_butterfly (
  input  wire        clk_i,
  input  wire        rst_ni,
  input  wire [5:0]  op_i,
  input  wire [31:0] rs1_i,
  input  wire [31:0] rs2_i,
  output reg  [31:0] result_o
);

  localparam [15:0] Q_K       = 16'd3329;
  localparam [15:0] MONT_QINV = 16'd62209;

  // ---- Legacy sum/diff path ----------------------------------------------
  wire signed [16:0] a17_legacy = {rs1_i[15], rs1_i[15:0]};
  wire signed [16:0] t17_legacy = {rs2_i[15], rs2_i[15:0]};
  wire signed [16:0] sum_legacy  = a17_legacy + t17_legacy;
  wire signed [16:0] diff_legacy = a17_legacy - t17_legacy;
  wire        [31:0] bfly_legacy = {diff_legacy[15:0], sum_legacy[15:0]};

  // ---- Decoded operands for the mult path --------------------------------
  wire signed [15:0] a_in    = rs1_i[15:0];
  wire signed [15:0] b_in    = rs2_i[15:0];
  wire signed [15:0] z_in    = rs2_i[31:16];

  // ---- CT path: t = mont(b*zeta); out = a±t ------------------------------
  (* use_dsp = "yes" *) wire signed [31:0] ct_prod  = b_in * z_in;
  wire        [15:0] ct_prod_lo = ct_prod[15:0];
  wire        [31:0] ct_m_lo  = ct_prod_lo * MONT_QINV;
  wire        [31:0] ct_mq    = ct_m_lo[15:0] * Q_K;
  wire signed [31:0] ct_diff32 = ct_prod - $signed(ct_mq);
  wire signed [15:0] ct_t     = ct_diff32[31:16];

  wire signed [16:0] ct_a17  = {a_in[15], a_in};
  wire signed [16:0] ct_t17  = {ct_t[15], ct_t};
  wire signed [16:0] ct_sum  = ct_a17 + ct_t17;
  wire signed [16:0] ct_dif  = ct_a17 - ct_t17;
  wire        [31:0] bfly_ct_mul = { ct_dif[15:0], ct_sum[15:0] };

  // ---- GS path: s = a+b; d = a-b; t = mont(d*zeta); out = {t, s} --------
  // Bisect: explicit $signed() casts on concatenation
  wire signed [16:0] gs_sum17 = $signed({a_in[15], a_in}) + $signed({b_in[15], b_in});
  wire signed [16:0] gs_dif17 = $signed({a_in[15], a_in}) - $signed({b_in[15], b_in});
  wire signed [15:0] gs_d     = gs_dif17[15:0];
  (* use_dsp = "yes" *) wire signed [31:0] gs_prod  = gs_d * z_in;
  wire        [15:0] gs_prod_lo = gs_prod[15:0];
  wire        [31:0] gs_m_lo  = gs_prod_lo * MONT_QINV;
  wire        [31:0] gs_mq    = gs_m_lo[15:0] * Q_K;
  wire signed [31:0] gs_diff32 = gs_prod - $signed(gs_mq);
  wire signed [15:0] gs_t     = gs_diff32[31:16];

  wire        [31:0] bfly_gs_mul = { gs_t[15:0], gs_sum17[15:0] };

  // ---- Output mux --------------------------------------------------------
  always @(*) begin
    case (op_i)
      `OP_NTT_BFLY_CT:     result_o = bfly_legacy;
      `OP_NTT_BFLY_GS:     result_o = bfly_legacy;
      `OP_NTT_BFLY_CT_MUL: result_o = bfly_ct_mul;
      `OP_NTT_BFLY_GS_MUL: result_o = bfly_gs_mul;
      default:             result_o = 32'd0;
    endcase
  end

endmodule
