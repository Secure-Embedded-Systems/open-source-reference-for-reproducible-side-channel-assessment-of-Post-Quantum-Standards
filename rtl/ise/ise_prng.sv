// ISE unit: on-die xoshiro128++ PRNG with seed-time safety interlock.

`include "sca_pqc_pkg.sv"

module ise_prng (
  input  wire        clk_i,
  input  wire        rst_ni,

  // Seed interface (from SEED_PRNG instruction)
  input  wire        seed_valid_i,
  input  wire [31:0] seed_data_i,
  input  wire [1:0]  seed_idx_i,     // which state word (0-3)

  // Consume interface (from masked ops)
  input  wire        consume_i,       // advance PRNG state

  // Output
  output wire [31:0] random_o,        // current PRNG output
  output wire        ready_o          // 1 if seeded (all 4 words loaded)
);

  // --------------------------------------------------------------------------
  // Internal state: 4 x 32-bit words
  // --------------------------------------------------------------------------
  (* keep = "true" *) (* keep_hierarchy = "true" *) reg [31:0] s [0:3];
  (* keep = "true" *) (* keep_hierarchy = "true" *) reg [3:0]  seed_done;

  assign ready_o = &seed_done;

  // --------------------------------------------------------------------------
  // xoshiro128++ output function
  //   result = rotl(s[0] + s[3], 7) + s[0]
  // --------------------------------------------------------------------------
  wire [31:0] sum_03;
  wire [31:0] rotl_sum;

  assign sum_03   = s[0] + s[3];
  assign rotl_sum = {sum_03[24:0], sum_03[31:25]};  // rotl(sum_03, 7)
  assign random_o = rotl_sum + s[0];

  // --------------------------------------------------------------------------
  // State advance (xoshiro128++ scramble)
  //
  //   t     = s[1] << 9
  //   s[2] ^= s[0]
  //   s[3] ^= s[1]
  //   s[1] ^= s[2]
  //   s[0] ^= s[3]
  //   s[2] ^= t
  //   s[3]  = rotl(s[3], 11)
  //
  // Computed as next-state values to avoid ordering issues.
  // --------------------------------------------------------------------------
  wire [31:0] t;
  assign t = {s[1][22:0], 9'd0};  // s[1] << 9

  // After the XOR cascade, the new state values are:
  //   s2_new = s[2] ^ s[0]
  //   s3_new = s[3] ^ s[1]
  //   s1_new = s[1] ^ s2_new = s[1] ^ s[2] ^ s[0]
  //   s0_new = s[0] ^ s3_new = s[0] ^ s[3] ^ s[1]
  //   s2_fin = s2_new ^ t     = s[2] ^ s[0] ^ (s[1] << 9)
  //   s3_fin = rotl(s3_new, 11) = rotl(s[3] ^ s[1], 11)

  wire [31:0] s2_new;
  wire [31:0] s3_new;
  wire [31:0] s1_new;
  wire [31:0] s0_new;
  wire [31:0] s2_fin;
  wire [31:0] s3_fin;

  assign s2_new = s[2] ^ s[0];
  assign s3_new = s[3] ^ s[1];
  assign s1_new = s[1] ^ s2_new;          // s[1] ^ s[2] ^ s[0]
  assign s0_new = s[0] ^ s3_new;          // s[0] ^ s[3] ^ s[1]
  assign s2_fin = s2_new ^ t;             // s[2] ^ s[0] ^ (s[1] << 9)
  assign s3_fin = {s3_new[20:0], s3_new[31:21]};  // rotl(s3_new, 11)

  // --------------------------------------------------------------------------
  // Sequential logic: seed and advance
  // --------------------------------------------------------------------------
  integer i;

  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      s[0]      <= 32'd0;
      s[1]      <= 32'd0;
      s[2]      <= 32'd0;
      s[3]      <= 32'd0;
      seed_done <= 4'b0000;
    end else begin
      // Seed: write one state word at a time
      if (seed_valid_i) begin
        s[seed_idx_i]            <= seed_data_i;
        seed_done[seed_idx_i]    <= 1'b1;
      end

      // Advance state when consumed and ready
      if (consume_i && ready_o) begin
        s[0] <= s0_new;
        s[1] <= s1_new;
        s[2] <= s2_fin;
        s[3] <= s3_fin;
      end
    end
  end

endmodule
