// Ring-oscillator entropy source array feeding the TRNG.

module trng_ro_array #(
  parameter N_RO   = 32,
  parameter RO_LEN = 13
)(
  input  wire clk_i,
  input  wire rst_ni,
  input  wire enable_i,
  output wire random_bit_o
);

  // -------------------------------------------------------------------------
  // Ring oscillator array
  // Each RO is a chain of RO_LEN inverters with the output fed back to
  // the input, forming a free-running oscillator. The (* keep = "true" *)
  // attribute prevents synthesis tools from optimizing the chain away.
  // -------------------------------------------------------------------------
  wire [N_RO-1:0] ro_out;

  genvar g_ro, g_inv;
  generate
    for (g_ro = 0; g_ro < N_RO; g_ro = g_ro + 1) begin : gen_ro

      // Inverter chain wires: inv[0] is input, inv[RO_LEN] is output
      (* keep = "true" *) wire [RO_LEN:0] inv;

      // Feed output back to input (oscillation)
      assign inv[0] = enable_i ? inv[RO_LEN] : 1'b0;

      // Inverter chain
      for (g_inv = 0; g_inv < RO_LEN; g_inv = g_inv + 1) begin : gen_inv
        (* keep = "true" *) wire inv_out;
        assign inv_out = ~inv[g_inv];
        assign inv[g_inv + 1] = inv_out;
      end

      assign ro_out[g_ro] = inv[RO_LEN];

    end
  endgenerate

  // -------------------------------------------------------------------------
  // Sampling register: capture RO outputs on system clock edge
  // This creates metastability (intentional for entropy extraction).
  // -------------------------------------------------------------------------
  reg [N_RO-1:0] ro_sampled_q;

  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      ro_sampled_q <= {N_RO{1'b0}};
    else if (enable_i)
      ro_sampled_q <= ro_out;
    else
      ro_sampled_q <= {N_RO{1'b0}};
  end

  // -------------------------------------------------------------------------
  // XOR tree reduction: reduce N_RO sampled bits to 1 entropy bit
  // -------------------------------------------------------------------------
  reg xor_reduced;

  integer i;
  always @(*) begin
    xor_reduced = 1'b0;
    for (i = 0; i < N_RO; i = i + 1)
      xor_reduced = xor_reduced ^ ro_sampled_q[i];
  end

  // -------------------------------------------------------------------------
  // Output register for clean timing
  // -------------------------------------------------------------------------
  reg random_bit_q;

  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      random_bit_q <= 1'b0;
    else if (enable_i)
      random_bit_q <= xor_reduced;
    else
      random_bit_q <= 1'b0;
  end

  assign random_bit_o = random_bit_q;

endmodule
