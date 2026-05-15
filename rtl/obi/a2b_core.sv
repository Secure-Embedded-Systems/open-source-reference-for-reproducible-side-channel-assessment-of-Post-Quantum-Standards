// Boolean-masking core: arithmetic-to-Boolean share conversion.

(* keep_hierarchy = "yes" *)
module a2b_core (
  input  wire        clk_i,
  input  wire        rst_ni,

  input  wire        start_i,
  input  wire [31:0] share0_i,     // arithmetic share 0 (x0)
  input  wire [31:0] share1_i,     // arithmetic share 1 (x1)
  input  wire [31:0] random_i,     // fresh randomness from PRNG
  input  wire        wide_mode_i,  // 0=13-bit (ML-KEM), 1=24-bit (ML-DSA)

  output reg  [31:0] share0_o,     // boolean share 0 (y0)
  output reg  [31:0] share1_o,     // boolean share 1 (y1)
  output reg         done_o
);

  localparam W_MAX    = 24;
  localparam N_STAGES = 5;
  localparam N_RND    = 2 + 3*N_STAGES;

  localparam [23:0] Q_KEM = 24'd3329;
  localparam [23:0] Q_DSA = 24'd8380417;

  // 2^W - Q values for correction
  // ML-KEM: 2^13 - 3329 = 4863
  // ML-DSA: 2^24 - 8380417 = 8396799
  localparam [23:0] NEG_Q_KEM = 24'd4863;   // 2^13 - 3329
  localparam [23:0] NEG_Q_DSA = 24'd8396799; // 2^24 - 8380417

  // FSM
  localparam [3:0] ST_IDLE       = 4'd0;
  localparam [3:0] ST_FILL_RND1  = 4'd1;
  localparam [3:0] ST_ADD1_START = 4'd2;
  localparam [3:0] ST_ADD1_RUN   = 4'd3;
  localparam [3:0] ST_FILL_RND2  = 4'd4;
  localparam [3:0] ST_ADD2_START = 4'd5;
  localparam [3:0] ST_ADD2_RUN   = 4'd6;
  localparam [3:0] ST_SELECT     = 4'd7;
  localparam [3:0] ST_DONE       = 4'd8;

  reg [3:0] state_q;

  // Working registers
  reg [W_MAX-1:0] x0_r, x1_r;
  reg [W_MAX-1:0] mask_r;
  reg [W_MAX-1:0] raw0_r, raw1_r;     // pass 1 result (raw sum)
  reg [W_MAX-1:0] trial0_r, trial1_r; // pass 2 result (sum + (2^W-Q))

  // SecAdd instance
  reg                         add_start;
  reg  [W_MAX-1:0]            add_a0, add_a1, add_b0, add_b1;
  wire [W_MAX-1:0]            add_s0, add_s1;
  wire                        add_done;

  // Randomness pool
  reg [N_RND*W_MAX-1:0] rnd_pool;
  reg [4:0]             rnd_fill_cnt;
  reg                   filling_rnd;

  sec_add #(.WIDTH(W_MAX), .N_STAGES(N_STAGES)) u_secadd (
    .clk_i   (clk_i),
    .rst_ni  (rst_ni),
    .start_i (add_start),
    .a0_i    (add_a0),
    .a1_i    (add_a1),
    .b0_i    (add_b0),
    .b1_i    (add_b1),
    .rnd_i   (rnd_pool),
    .s0_o    (add_s0),
    .s1_o    (add_s1),
    .done_o  (add_done)
  );

  // PRNG expansion: fill rnd_pool from sequential random_i values
  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      rnd_pool     <= {(N_RND*W_MAX){1'b0}};
      rnd_fill_cnt <= 5'd0;
      filling_rnd  <= 1'b0;
    end else if (filling_rnd) begin
      rnd_pool[rnd_fill_cnt*W_MAX +: W_MAX] <= random_i[W_MAX-1:0] ^ {rnd_fill_cnt, {(W_MAX-5){1'b0}}};
      if (rnd_fill_cnt == N_RND - 1) begin
        rnd_fill_cnt <= 5'd0;
        filling_rnd  <= 1'b0;
      end else begin
        rnd_fill_cnt <= rnd_fill_cnt + 5'd1;
      end
    end
  end

  // Width selection
  // w_mask: masks to W bits (W = ceil(log2(Q))+1 = 13 or 24)
  wire [W_MAX-1:0] w_mask = wide_mode_i ? {W_MAX{1'b1}} : {{(W_MAX-13){1'b0}}, {13{1'b1}}};
  // w_mask_ext: masks to W+1 bits for carry-out detection in pass 2
  wire [W_MAX-1:0] w_mask_ext = wide_mode_i ? {W_MAX{1'b1}} : {{(W_MAX-14){1'b0}}, {14{1'b1}}};
  // carry_pos: bit position of the carry-out (= W, one above the modulus MSB)
  wire [4:0]       carry_pos = wide_mode_i ? 5'd23 : 5'd13;  // NOTE: bit 13 for ML-KEM, not 12
  // result_mask: mask for the final output (W-1 bits = modulus width)
  wire [4:0]       mod_msb = wide_mode_i ? 5'd22 : 5'd11;  // actual modulus MSB position
  wire [W_MAX-1:0] neg_q   = wide_mode_i ? NEG_Q_DSA : NEG_Q_KEM;

  // Main FSM
  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q   <= ST_IDLE;
      x0_r      <= {W_MAX{1'b0}};
      x1_r      <= {W_MAX{1'b0}};
      mask_r    <= {W_MAX{1'b0}};
      raw0_r    <= {W_MAX{1'b0}};
      raw1_r    <= {W_MAX{1'b0}};
      trial0_r  <= {W_MAX{1'b0}};
      trial1_r  <= {W_MAX{1'b0}};
      add_start <= 1'b0;
      add_a0    <= {W_MAX{1'b0}};
      add_a1    <= {W_MAX{1'b0}};
      add_b0    <= {W_MAX{1'b0}};
      add_b1    <= {W_MAX{1'b0}};
      share0_o  <= 32'd0;
      share1_o  <= 32'd0;
      done_o    <= 1'b0;
    end else begin
      add_start <= 1'b0;
      done_o    <= 1'b0;

      case (state_q)

        ST_IDLE: begin
          if (start_i) begin
            x0_r   <= share0_i[W_MAX-1:0] & w_mask;
            x1_r   <= share1_i[W_MAX-1:0] & w_mask;
            mask_r <= w_mask;
            filling_rnd  <= 1'b1;
            rnd_fill_cnt <= 5'd0;
            state_q      <= ST_FILL_RND1;
          end
        end

        // ---- Pass 1: compute raw = x0 + x1 mod 2^W ----
        ST_FILL_RND1: begin
          if (!filling_rnd) begin
            add_a0    <= x0_r;
            add_a1    <= {W_MAX{1'b0}};  // x0 as trivial Boolean share (x0, 0)
            add_b0    <= x1_r;
            add_b1    <= {W_MAX{1'b0}};  // x1 as trivial Boolean share (x1, 0)
            add_start <= 1'b1;
            state_q   <= ST_ADD1_RUN;
          end
        end

        ST_ADD1_RUN: begin
          if (add_done) begin
            raw0_r  <= add_s0 & mask_r;
            raw1_r  <= add_s1 & mask_r;
            // Start filling randomness for pass 2
            filling_rnd  <= 1'b1;
            rnd_fill_cnt <= 5'd0;
            state_q      <= ST_FILL_RND2;
          end
        end

        // ---- Pass 2: compute trial = raw + (2^W - Q) mod 2^W ----
        // If result MSB is set, then raw >= Q and we use trial[W-2:0]
        // If result MSB is clear, raw < Q and we use raw[W-2:0]
        ST_FILL_RND2: begin
          if (!filling_rnd) begin
            // Pass 2 uses W+1 bits so the carry-out is visible
            add_a0    <= raw0_r & w_mask;       // original sum (W bits)
            add_a1    <= raw1_r & w_mask;
            add_b0    <= neg_q & w_mask;        // 2^W - Q (W bits, fits in W+1)
            add_b1    <= {W_MAX{1'b0}};
            add_start <= 1'b1;
            state_q   <= ST_ADD2_RUN;
          end
        end

        ST_ADD2_RUN: begin
          if (add_done) begin
            // Keep W+1 bits so we can see the carry-out
            trial0_r <= add_s0 & w_mask_ext;
            trial1_r <= add_s1 & w_mask_ext;
            state_q  <= ST_SELECT;
          end
        end

        // ---- Select correct result ----
        // overflow = trial_msb = trial0[msb_pos] XOR trial1[msb_pos]
        // if overflow: result = trial (reduced), else: result = raw (already < Q)
        //
        // Masked MUX: select between raw and trial using the overflow bit.
        // For constant-time: result = raw XOR (overflow_mask AND (raw XOR trial))
        // where overflow_mask = {W{overflow_bit}} (replicated)
        //
        // Since we hold both shares internally (this module owns both),
        // we can compute the unmasked overflow and branch.
        // The overflow bit only reveals whether x >= Q, which is public
        // information (all valid coefficients are in [0, Q-1]).
        // ---- Select based on carry-out of trial = raw + (2^W - Q) ----
        // carry_out = trial[carry_pos] share0 XOR trial[carry_pos] share1
        // If carry_out = 1: raw >= Q, use trial's lower bits (= raw - Q)
        // If carry_out = 0: raw < Q, use raw directly
        ST_SELECT: begin
          begin : select_block
            reg carry_out;
            reg [W_MAX-1:0] out_mask;

            carry_out = trial0_r[carry_pos] ^ trial1_r[carry_pos];
            // Output mask: only the modulus-width bits (W-1 bits = log2(Q) bits)
            out_mask = w_mask >> 1;  // W-1 bits (strip the overflow bit from W)

            if (carry_out) begin
              // raw >= Q: use trial lower bits (= raw - Q, already reduced)
              share0_o <= {{(32-W_MAX){1'b0}}, trial0_r & out_mask};
              share1_o <= {{(32-W_MAX){1'b0}}, trial1_r & out_mask};
            end else begin
              // raw < Q: use raw directly (already in [0, Q-1])
              share0_o <= {{(32-W_MAX){1'b0}}, raw0_r & out_mask};
              share1_o <= {{(32-W_MAX){1'b0}}, raw1_r & out_mask};
            end
          end
          state_q <= ST_DONE;
        end

        ST_DONE: begin
          done_o  <= 1'b1;
          state_q <= ST_IDLE;
        end

        default: state_q <= ST_IDLE;

      endcase
    end
  end

endmodule
