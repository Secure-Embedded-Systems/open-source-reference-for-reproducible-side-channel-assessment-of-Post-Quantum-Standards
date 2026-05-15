// Boolean-domain secure AND gadget on shared inputs.

(* keep_hierarchy = "yes" *)
module sec_and #(
  parameter WIDTH = 13
) (
  input  wire              clk_i,
  input  wire              rst_ni,
  input  wire              en_i,

  input  wire [WIDTH-1:0]  a0_i,    // share 0 of a
  input  wire [WIDTH-1:0]  a1_i,    // share 1 of a
  input  wire [WIDTH-1:0]  b0_i,    // share 0 of b
  input  wire [WIDTH-1:0]  b1_i,    // share 1 of b
  input  wire [WIDTH-1:0]  rnd_i,   // fresh randomness (one word)

  output wire [WIDTH-1:0]  c0_o,    // share 0 of c = a AND b
  output wire [WIDTH-1:0]  c1_o,    // share 1 of c = a AND b
  output reg               done_o
);

  // -------------------------------------------------------------------------
  // Combinational: inner-domain and cross-domain products
  // -------------------------------------------------------------------------
  wire [WIDTH-1:0] inner0  = a0_i & b0_i;
  wire [WIDTH-1:0] inner1  = a1_i & b1_i;
  wire [WIDTH-1:0] cross01 = (a0_i & b1_i) ^ rnd_i;
  wire [WIDTH-1:0] cross10 = (a1_i & b0_i) ^ rnd_i;

  // -------------------------------------------------------------------------
  // REGISTER BARRIER — CRITICAL for probing security / glitch resistance
  // All four terms must be registered before recombination to prevent
  // intermediate values from leaking via glitches.
  // -------------------------------------------------------------------------
  (* keep_hierarchy = "yes" *) reg [WIDTH-1:0] reg_inner0;
  (* keep_hierarchy = "yes" *) reg [WIDTH-1:0] reg_inner1;
  (* keep_hierarchy = "yes" *) reg [WIDTH-1:0] reg_cross01;
  (* keep_hierarchy = "yes" *) reg [WIDTH-1:0] reg_cross10;
  reg en_d;

  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      reg_inner0  <= {WIDTH{1'b0}};
      reg_inner1  <= {WIDTH{1'b0}};
      reg_cross01 <= {WIDTH{1'b0}};
      reg_cross10 <= {WIDTH{1'b0}};
      en_d        <= 1'b0;
      done_o      <= 1'b0;
    end else begin
      en_d   <= en_i;
      done_o <= en_d;
      if (en_i) begin
        reg_inner0  <= inner0;
        reg_inner1  <= inner1;
        reg_cross01 <= cross01;
        reg_cross10 <= cross10;
      end
    end
  end

  // -------------------------------------------------------------------------
  // Recombination (combinational, after register barrier)
  // -------------------------------------------------------------------------
  assign c0_o = reg_inner0 ^ reg_cross01;
  assign c1_o = reg_inner1 ^ reg_cross10;

endmodule
