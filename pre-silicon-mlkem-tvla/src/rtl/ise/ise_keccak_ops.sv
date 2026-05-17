// ============================================================================
// SCA_Pro_PQC — SCA-Resilient PQC Accelerator for ML-KEM + ML-DSA
// ============================================================================
//
// File        : ise_keccak_ops.sv
// Description : Keccak-permutation ISE helpers (Group 0).
//               Implements SET_C, BCOP32_AB, SET_HI, ROL32_L, ROL32_H, XOR3.
//
// ============================================================================

`include "sca_pqc_pkg.sv"

module ise_keccak_ops (
  input  wire        clk_i, rst_ni, valid_i,
  input  wire [5:0]  op_i,
  input  wire [31:0] rs1_i, rs2_i,
  output reg  [31:0] result_o
);

  reg [31:0] acc_c, acc_hi;

  wire [5:0]  shamt   = rs2_i[5:0];
  wire [63:0] concat  = {acc_hi, rs1_i};
  wire [6:0]  rshamt  = 7'd64 - {1'b0, shamt};
  wire [63:0] rotated = (concat << shamt) | (concat >> rshamt[5:0]);

  // ------------------------------------------------------------
  // Combinational result mux
  // ------------------------------------------------------------
  always @(*) begin
    result_o = 32'd0;
    case (op_i)
      `OP_SET_C:     result_o = rs1_i;
      `OP_BCOP32_AB: result_o = rs1_i ^ (~rs2_i & acc_c);
      `OP_SET_HI:    result_o = rs1_i;
      `OP_ROL32_L:   result_o = rotated[31:0];
      `OP_ROL32_H:   result_o = rotated[63:32];
      `OP_XOR3:      result_o = rs1_i ^ rs2_i ^ acc_c;
      default:       result_o = 32'd0;
    endcase
  end

  // ------------------------------------------------------------
  // Accumulator registers
  // ------------------------------------------------------------
  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      acc_c  <= 32'd0;
      acc_hi <= 32'd0;
    end else if (valid_i) begin
      if (op_i == `OP_SET_C)  acc_c  <= rs1_i;
      if (op_i == `OP_SET_HI) acc_hi <= rs1_i;
    end
  end

endmodule
