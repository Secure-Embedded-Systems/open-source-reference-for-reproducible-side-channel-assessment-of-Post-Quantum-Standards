// ISE unit: ML-DSA-specific helpers (power2round, decompose, makehint, usehint).

`include "sca_pqc_pkg.sv"

module ise_mldsa_ops (
  input  wire        clk_i,
  input  wire        rst_ni,
  input  wire [5:0]  op_i,
  input  wire [31:0] rs1_i,   // coefficient value (in [0, Q-1])
  input  wire [31:0] rs2_i,   // parameter selector
  output reg  [31:0] result_o
);

  // --------------------------------------------------------------------------
  // ML-DSA constants
  // --------------------------------------------------------------------------
  localparam Q         = 23'd8380417;  // ML-DSA prime
  localparam D         = 13;           // Power2Round parameter
  localparam HALF_D    = 23'd4096;     // 1 << (D-1) = 4096
  localparam TWO_D     = 23'd8192;     // 1 << D     = 8192

  // Gamma2 variants
  localparam GAMMA2_44 = 23'd95232;    // (Q-1)/88  for ML-DSA-44/65
  localparam GAMMA2_87 = 23'd261888;   // (Q-1)/32  for ML-DSA-87
  localparam ALPHA_44  = 23'd190464;   // 2 * gamma2 for ML-DSA-44/65
  localparam ALPHA_87  = 23'd523776;   // 2 * gamma2 for ML-DSA-87

  // --------------------------------------------------------------------------
  // Power2Round
  //   r_plus = rs1_i + 4096
  //   r1     = r_plus >> 13       (high bits)
  //   r0     = rs1_i - r1 * 8192  (low bits, centered)
  // --------------------------------------------------------------------------
  wire [22:0] p2r_input;
  wire [22:0] p2r_r_plus;
  wire [22:0] p2r_r1;
  wire signed [22:0] p2r_r0;

  assign p2r_input  = rs1_i[22:0];
  assign p2r_r_plus = p2r_input + HALF_D;
  assign p2r_r1     = p2r_r_plus >> D;
  assign p2r_r0     = $signed(p2r_input) - $signed({p2r_r1[9:0], 13'd0}); // r1 * 8192

  wire [31:0] p2r_result;
  assign p2r_result = {p2r_r1[15:0], p2r_r0[15:0]};

  // --------------------------------------------------------------------------
  // Decompose — Barrett-like reduction for mod alpha
  //
  // For alpha = 190464 = 2^7 * 3 * 496 (ML-DSA-44/65):
  //   Reciprocal approximation: floor(2^41 / 190464) = 11544725
  //   q_hat = (rs1_i * 11544725) >> 41
  //   r0    = rs1_i - q_hat * 190464
  //
  // For alpha = 523776 = 2^9 * 1023 (ML-DSA-87):
  //   Reciprocal approximation: floor(2^41 / 523776) = 4198404
  //   q_hat = (rs1_i * 4198404) >> 41
  //   r0    = rs1_i - q_hat * 523776
  //
  // Then center r0 into [-alpha/2, alpha/2) and compute r1.
  // --------------------------------------------------------------------------
  wire        sel_87;
  assign sel_87 = rs2_i[0]; // 0 = ML-DSA-44/65, 1 = ML-DSA-87

  wire [22:0] dec_input;
  assign dec_input = rs1_i[22:0];

  // --- Barrett constants ---
  wire [23:0] alpha;
  wire [23:0] half_alpha;
  wire [23:0] recip;

  assign alpha      = sel_87 ? 24'd523776  : 24'd190464;
  assign half_alpha = sel_87 ? 24'd261888  : 24'd95232;
  assign recip      = sel_87 ? 24'd4198405 : 24'd11545612;  // ceil(2^41 / alpha)

  // --- Barrett quotient: q_hat = (input * recip) >> 41 ---
  // Use 47-bit product (23-bit input * 24-bit recip)
  wire [46:0] dec_prod;
  assign dec_prod = dec_input * recip;

  wire [22:0] q_hat;
  assign q_hat = dec_prod[46:41]; // upper bits after >> 41

  // --- Remainder: r0 = input - q_hat * alpha ---
  wire [46:0] q_alpha;
  assign q_alpha = q_hat * alpha;

  wire signed [23:0] r0_raw;
  assign r0_raw = $signed({1'b0, dec_input}) - $signed(q_alpha[23:0]);

  // --- Correction: if r0_raw >= alpha, subtract alpha ---
  wire signed [23:0] r0_corr;
  assign r0_corr = (r0_raw >= $signed({1'b0, alpha})) ? r0_raw - $signed({1'b0, alpha}) : r0_raw;

  // --- Center: if r0_corr > half_alpha, r0_centered = r0_corr - alpha ---
  wire signed [23:0] r0_centered;
  assign r0_centered = (r0_corr > $signed({1'b0, half_alpha}))
                      ? r0_corr - $signed({1'b0, alpha})
                      : r0_corr;

  // --- Special case: if (input - r0_centered) == Q-1, set r1=0, r0=r0_centered-1 ---
  wire [23:0] a_minus_r0;
  assign a_minus_r0 = {1'b0, dec_input} - r0_centered[23:0];

  wire        special_case;
  assign special_case = (a_minus_r0 == (Q - 1));

  wire signed [23:0] r0_final;
  wire [22:0]        r1_final;

  assign r0_final = special_case ? (r0_centered - 24'sd1) : r0_centered;

  // r1 = (input - r0_centered) / alpha  (exact division when not special case)
  // For the special case, r1 = 0
  wire [46:0] r1_div_prod;
  wire [22:0] a_minus_r0_trunc;
  assign a_minus_r0_trunc = a_minus_r0[22:0];

  // Reuse Barrett approach for division: r1 = (a - r0) * recip >> 41
  wire [46:0] r1_prod;
  assign r1_prod = a_minus_r0_trunc * recip;
  assign r1_final = special_case ? 23'd0 : r1_prod[46:41];

  wire [31:0] dec_result;
  assign dec_result = {r1_final[15:0], r0_final[15:0]};

  // --------------------------------------------------------------------------
  // Output mux — combinational (coprocessor top registers the result)
  // --------------------------------------------------------------------------
  always @(*) begin
    case (op_i)
      `OP_POWER2ROUND: result_o = p2r_result;
      `OP_DECOMPOSE:   result_o = dec_result;
      default:         result_o = 32'd0;
    endcase
  end

endmodule
