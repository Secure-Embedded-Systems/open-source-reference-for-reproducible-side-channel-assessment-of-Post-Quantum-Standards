// ISE unit: ciphertext compress/decompress (1, 4, 5, 10, 11-bit variants).

`include "sca_pqc_pkg.sv"

module ise_compress (
  input  wire        clk_i,
  input  wire        rst_ni,
  input  wire [5:0]  op_i,
  input  wire [31:0] rs1_i,
  output reg  [31:0] result_o
);

  // ========================================================================
  // Constants
  // ========================================================================
  localparam [15:0] Q_K      = 16'd3329;
  localparam [31:0] RECIP_Q  = 32'd80635;  // ceil(2^28 / 3329)
  localparam [15:0] Q_HALF   = 16'd1664;   // (Q - 1) / 2

  // ========================================================================
  // Compress helper: multiply-shift approximation
  //
  //   u = x + ((x >> 15) & Q)     — conditional add Q (handles signed input)
  //   y = (u << d) + Q_HALF       — scale by 2^d, add rounding bias
  //   p = y * RECIP_Q             — multiply by reciprocal of Q
  //   result = (p >> 28) & mask   — extract compressed value
  //
  // The (x >> 15) & Q trick: if x has bit 15 set (i.e. x is in the upper
  // half of the signed 16-bit range, treated as negative), this adds Q to
  // bring it back into [0, Q-1]. For properly reduced x in [0, Q-1] this
  // is a no-op since bit 15 is never set.
  // ========================================================================
  wire [31:0] x = rs1_i;

  // Conditional add Q for signed input normalization
  wire [31:0] u = x + ((x >> 15) & {16'd0, Q_K});

  // d=1: compress to 1 bit
  wire [31:0] y_d1  = (u << 1)  + {16'd0, Q_HALF};
  wire [63:0] p_d1  = y_d1 * RECIP_Q;
  wire [31:0] comp_d1 = (p_d1[59:28]) & 32'h0000_0001;

  // d=4: compress to 4 bits
  wire [31:0] y_d4  = (u << 4)  + {16'd0, Q_HALF};
  wire [63:0] p_d4  = y_d4 * RECIP_Q;
  wire [31:0] comp_d4 = (p_d4[59:28]) & 32'h0000_000F;

  // d=5: compress to 5 bits
  wire [31:0] y_d5  = (u << 5)  + {16'd0, Q_HALF};
  wire [63:0] p_d5  = y_d5 * RECIP_Q;
  wire [31:0] comp_d5 = (p_d5[59:28]) & 32'h0000_001F;

  // d=10: compress to 10 bits
  wire [31:0] y_d10 = (u << 10) + {16'd0, Q_HALF};
  wire [63:0] p_d10 = y_d10 * RECIP_Q;
  wire [31:0] comp_d10 = (p_d10[59:28]) & 32'h0000_03FF;

  // d=11: compress to 11 bits (ML-KEM-1024 du)
  wire [31:0] y_d11 = (u << 11) + {16'd0, Q_HALF};
  wire [63:0] p_d11 = y_d11 * RECIP_Q;
  wire [31:0] comp_d11 = (p_d11[59:28]) & 32'h0000_07FF;

  // ========================================================================
  // REJ_UNIFORM: candidate = rs1[11:0];  accept if < 3329
  // ========================================================================
  wire [11:0] rej_cand = rs1_i[11:0];
  wire        rej_ok   = (rej_cand < Q_K[11:0]);
  wire [31:0] rej_result = rej_ok ? {20'd0, rej_cand} : 32'hFFFFFFFF;

  // ========================================================================
  // Output mux
  // ========================================================================
  always @(*) begin
    result_o = 32'd0;
    case (op_i)
      `OP_COMPRESS_1: result_o = comp_d1;
      `OP_COMPRESS_2: result_o = comp_d4;
      `OP_COMPRESS_3: result_o = comp_d5;
      `OP_COMPRESS_4: result_o = comp_d10;
      `OP_COMPRESS_5: result_o = comp_d11;
      `OP_REJ_UNIFORM:result_o = rej_result;
      default:        result_o = 32'd0;
    endcase
  end

endmodule
