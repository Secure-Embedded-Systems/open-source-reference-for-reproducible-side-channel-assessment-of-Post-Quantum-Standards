// ============================================================================
// SCA_Pro_PQC — SCA-Resilient PQC Accelerator for ML-KEM + ML-DSA
// ============================================================================
//
// File        : ise_cbd.sv
// Description : Centered Binomial Distribution (CBD) sampling ISE (Group 3).
//               Implements CBD eta=2 (8 steps) and CBD eta=3 (4 steps).
//               Each step extracts one signed coefficient from a packed
//               bitstream in {rs2, rs1}.
//
// CBD eta=2: 4 bits per coefficient (2-bit popcount minus 2-bit popcount).
//            8 coefficients from 32 bits => 8 steps.
// CBD eta=3: 6 bits per coefficient (3-bit popcount minus 3-bit popcount).
//            4 coefficients from 24 bits => 4 steps.
//
// ============================================================================

`include "sca_pqc_pkg.sv"

module ise_cbd (
  input  wire        clk_i,
  input  wire        rst_ni,
  input  wire [5:0]  op_i,
  input  wire [31:0] rs1_i,
  output reg  [31:0] result_o
);

  // ========================================================================
  // Helpers: population count of 2-bit and 3-bit fields
  // ========================================================================
  function automatic [1:0] popcount2(input [1:0] v);
    popcount2 = {1'b0, v[0]} + {1'b0, v[1]};
  endfunction

  function automatic [1:0] popcount3(input [2:0] v);
    popcount3 = {1'b0, v[0]} + {1'b0, v[1]} + {1'b0, v[2]};
  endfunction

  // ========================================================================
  // CBD eta=2: Extract coefficient from 4-bit slice at given offset.
  //   a = popcount(bits[1:0]),  b = popcount(bits[3:2]),  coeff = a - b
  // ========================================================================
  wire [63:0] cbd_bits = {32'd0, rs1_i};

  // --- eta=2 coefficients (8 steps, 4 bits each) ---
  wire signed [31:0] cbd2 [0:7];
  genvar gi;
  generate
    for (gi = 0; gi < 8; gi = gi + 1) begin : gen_cbd2
      wire [3:0] slice = cbd_bits[gi*4 +: 4];
      wire [1:0] a     = popcount2(slice[1:0]);
      wire [1:0] b     = popcount2(slice[3:2]);
      wire signed [2:0] diff = $signed({1'b0, a}) - $signed({1'b0, b});
      assign cbd2[gi] = {{29{diff[2]}}, diff};
    end
  endgenerate

  // --- eta=3 coefficients (4 steps, 6 bits each) ---
  wire signed [31:0] cbd3 [0:3];
  generate
    for (gi = 0; gi < 4; gi = gi + 1) begin : gen_cbd3
      wire [5:0] slice = cbd_bits[gi*6 +: 6];
      wire [1:0] a     = popcount3(slice[2:0]);
      wire [1:0] b     = popcount3(slice[5:3]);
      wire signed [2:0] diff = $signed({1'b0, a}) - $signed({1'b0, b});
      assign cbd3[gi] = {{29{diff[2]}}, diff};
    end
  endgenerate

  // ========================================================================
  // Output mux
  // ========================================================================
  always @(*) begin
    result_o = 32'd0;
    case (op_i)
      // CBD eta=2 steps 1..8
      `OP_CBD2_1: result_o = cbd2[0];
      `OP_CBD2_2: result_o = cbd2[1];
      `OP_CBD2_3: result_o = cbd2[2];
      `OP_CBD2_4: result_o = cbd2[3];
      `OP_CBD2_5: result_o = cbd2[4];
      `OP_CBD2_6: result_o = cbd2[5];
      `OP_CBD2_7: result_o = cbd2[6];
      `OP_CBD2_8: result_o = cbd2[7];
      // CBD eta=3 steps 1..4
      `OP_CBD3_1: result_o = cbd3[0];
      `OP_CBD3_2: result_o = cbd3[1];
      `OP_CBD3_3: result_o = cbd3[2];
      `OP_CBD3_4: result_o = cbd3[3];
      default:    result_o = 32'd0;
    endcase
  end

endmodule
