// Higher-level secure operations built on sec_add and sec_and.

(* keep_hierarchy = "yes" *)
module sec_xor #(
  parameter WIDTH = 13
) (
  input  wire [WIDTH-1:0] a0_i, a1_i,
  input  wire [WIDTH-1:0] b0_i, b1_i,
  output wire [WIDTH-1:0] c0_o, c1_o
);

  assign c0_o = a0_i ^ b0_i;
  assign c1_o = a1_i ^ b1_i;

endmodule

// ============================================================================
// SecNOT — Combinational secure NOT (flip share 0 only)
// For 2-share: ~x = ~x0 XOR x1 = (~x0, x1) since x = x0 XOR x1
// In 2's complement: ~x = -x - 1, which is correct for bitwise NOT.
// ============================================================================
(* keep_hierarchy = "yes" *)
module sec_not #(
  parameter WIDTH = 13
) (
  input  wire [WIDTH-1:0] a0_i, a1_i,
  output wire [WIDTH-1:0] c0_o, c1_o
);

  assign c0_o = ~a0_i;
  assign c1_o = a1_i;

endmodule

// ============================================================================
// SecOR — Secure masked OR via De Morgan's law
//   OR(a, b) = NOT(AND(NOT(a), NOT(b)))
// Latency: 1 cycle (from the internal sec_and)
// ============================================================================
(* keep_hierarchy = "yes" *)
module sec_or #(
  parameter WIDTH = 13
) (
  input  wire              clk_i,
  input  wire              rst_ni,
  input  wire              en_i,

  input  wire [WIDTH-1:0]  a0_i, a1_i,
  input  wire [WIDTH-1:0]  b0_i, b1_i,
  input  wire [WIDTH-1:0]  rnd_i,

  output wire [WIDTH-1:0]  c0_o, c1_o,
  output wire              done_o
);

  // NOT(a): flip share 0 only
  wire [WIDTH-1:0] na0 = ~a0_i;
  wire [WIDTH-1:0] na1 =  a1_i;

  // NOT(b): flip share 0 only
  wire [WIDTH-1:0] nb0 = ~b0_i;
  wire [WIDTH-1:0] nb1 =  b1_i;

  // AND(NOT(a), NOT(b)) — 1 cycle through register barrier
  wire [WIDTH-1:0] and_c0, and_c1;

  sec_and #(.WIDTH(WIDTH)) u_and (
    .clk_i  (clk_i),
    .rst_ni (rst_ni),
    .en_i   (en_i),
    .a0_i   (na0),
    .a1_i   (na1),
    .b0_i   (nb0),
    .b1_i   (nb1),
    .rnd_i  (rnd_i),
    .c0_o   (and_c0),
    .c1_o   (and_c1),
    .done_o (done_o)
  );

  // NOT(AND result): flip share 0 only
  assign c0_o = ~and_c0;
  assign c1_o =  and_c1;

endmodule
