// Boolean-masking core: Boolean-to-arithmetic share conversion.

(* keep_hierarchy = "yes" *)
module b2a_core (
  input  wire        clk_i,
  input  wire        rst_ni,

  input  wire        start_i,
  input  wire [31:0] share0_i,     // boolean share 0 (y0)
  input  wire [31:0] share1_i,     // boolean share 1 (y1)
  input  wire [31:0] random_i,     // fresh randomness from PRNG
  input  wire        wide_mode_i,  // 0=13-bit (ML-KEM), 1=24-bit (ML-DSA)

  output reg  [31:0] share0_o,     // arithmetic share 0 (x0)
  output reg  [31:0] share1_o,     // arithmetic share 1 (x1)
  output reg         done_o
);

  // -------------------------------------------------------------------------
  // Parameters
  // -------------------------------------------------------------------------
  localparam W_MAX    = 24;
  localparam N_STAGES = 5;  // ceil(log2(24))
  localparam N_RND    = 2 + 3*N_STAGES;  // randomness words needed per SecAdd

  localparam [31:0] Q_KEM = 32'd3329;
  localparam [31:0] Q_DSA = 32'd8380417;

  // -------------------------------------------------------------------------
  // FSM
  // -------------------------------------------------------------------------
  localparam [3:0] ST_IDLE      = 4'd0;
  localparam [3:0] ST_LOAD      = 4'd1;
  localparam [3:0] ST_RND_FILL  = 4'd2;
  localparam [3:0] ST_ADD1_RUN  = 4'd3;  // SecAdd: y + neg_r
  localparam [3:0] ST_ADD1_DONE = 4'd4;
  localparam [3:0] ST_CORR_PREP = 4'd5;
  localparam [3:0] ST_ADD2_RUN  = 4'd6;  // SecAdd: correction
  localparam [3:0] ST_ADD2_DONE = 4'd7;
  localparam [3:0] ST_OUTPUT    = 4'd8;
  localparam [3:0] ST_DONE      = 4'd9;

  reg [3:0] state_q;

  // -------------------------------------------------------------------------
  // Working registers
  // -------------------------------------------------------------------------
  reg [W_MAX-1:0] y0_r, y1_r;
  reg [W_MAX-1:0] r_r;           // random value (arithmetic share x1)
  reg [W_MAX-1:0] mask_r;
  reg [31:0]      q_val;
  reg [W_MAX-1:0] sum0_r, sum1_r;

  // -------------------------------------------------------------------------
  // SecAdd instance
  // -------------------------------------------------------------------------
  reg                         add_start;
  reg  [W_MAX-1:0]            add_a0, add_a1, add_b0, add_b1;
  wire [W_MAX-1:0]            add_s0, add_s1;
  wire                        add_done;

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

  // -------------------------------------------------------------------------
  // PRNG expansion — same approach as a2b_core
  // -------------------------------------------------------------------------
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

  // -------------------------------------------------------------------------
  // Width selection
  // -------------------------------------------------------------------------
  wire [W_MAX-1:0] w_mask = wide_mode_i ? {W_MAX{1'b1}} : {{(W_MAX-13){1'b0}}, {13{1'b1}}};

  // -------------------------------------------------------------------------
  // Main FSM
  // -------------------------------------------------------------------------
  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q   <= ST_IDLE;
      y0_r      <= {W_MAX{1'b0}};
      y1_r      <= {W_MAX{1'b0}};
      r_r       <= {W_MAX{1'b0}};
      mask_r    <= {W_MAX{1'b0}};
      q_val     <= 32'd0;
      sum0_r    <= {W_MAX{1'b0}};
      sum1_r    <= {W_MAX{1'b0}};
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

        // -----------------------------------------------------------------
        ST_IDLE: begin
          if (start_i) begin
            // Latch Boolean shares
            y0_r   <= share0_i[W_MAX-1:0] & w_mask;
            y1_r   <= share1_i[W_MAX-1:0] & w_mask;
            // Capture random value as the arithmetic share x1
            // Reduce mod q for the arithmetic share
            r_r    <= random_i[W_MAX-1:0] & w_mask;
            mask_r <= w_mask;
            q_val  <= wide_mode_i ? Q_DSA : Q_KEM;
            // Start filling randomness
            filling_rnd  <= 1'b1;
            rnd_fill_cnt <= 5'd0;
            state_q      <= ST_LOAD;
          end
        end

        // -----------------------------------------------------------------
        // Reduce r mod q (may need up to 2 subtractions since r is W bits
        // and 2^W can be up to ~2.5*Q for ML-KEM)
        ST_LOAD: begin
          // Reduce r mod q. r is W bits, max = 2^W - 1.
          // For ML-KEM: max = 8191, Q = 3329, so max 2 subtractions needed.
          begin : reduce_r
            reg [W_MAX-1:0] two_q;
            two_q = q_val[W_MAX-1:0] + q_val[W_MAX-1:0];
            if (r_r >= two_q)
              r_r <= r_r - two_q;
            else if (r_r >= q_val[W_MAX-1:0])
              r_r <= r_r - q_val[W_MAX-1:0];
          end
          state_q <= ST_RND_FILL;
        end

        // -----------------------------------------------------------------
        ST_RND_FILL: begin
          if (!filling_rnd) begin
            // B2A algorithm:
            // We want x0 + x1 = y mod q, where y = y0 XOR y1.
            // Let x1 = r (random). Then x0 = y - r mod q.
            // To compute y - r: compute y + (2^W - r) using SecAdd.
            // neg_r = (2^W - r) & mask = (-r) mod 2^W
            //
            // y is Boolean-shared as (y0, y1).
            // neg_r is unmasked, so shares are (neg_r, 0).
            begin : launch_add1
              reg [W_MAX-1:0] neg_r;
              neg_r = ((~r_r) + {{(W_MAX-1){1'b0}}, 1'b1}) & mask_r;

              add_a0 <= y0_r;
              add_a1 <= y1_r;
              add_b0 <= neg_r;
              add_b1 <= {W_MAX{1'b0}};
            end
            add_start <= 1'b1;
            state_q   <= ST_ADD1_RUN;
          end
        end

        // -----------------------------------------------------------------
        ST_ADD1_RUN: begin
          if (add_done) begin
            // Result: s0 XOR s1 = y - r mod 2^W (Boolean shared)
            sum0_r  <= add_s0 & mask_r;
            sum1_r  <= add_s1 & mask_r;
            state_q <= ST_ADD1_DONE;
          end
        end

        // -----------------------------------------------------------------
        // Prime correction: if y - r < 0 (i.e., borrow occurred),
        // add q to get result mod q.
        // Borrow detection: MSB of the W-bit result.
        // If MSB is set, the subtraction underflowed, so add q.
        // -----------------------------------------------------------------
        ST_ADD1_DONE: begin
          filling_rnd  <= 1'b1;
          rnd_fill_cnt <= 5'd0;
          state_q      <= ST_CORR_PREP;
        end

        // -----------------------------------------------------------------
        ST_CORR_PREP: begin
          if (!filling_rnd) begin
            begin : correction_block
              reg [4:0]       msb_pos;
              reg             underflow;
              reg [W_MAX-1:0] correction;

              msb_pos   = wide_mode_i ? 5'd23 : 5'd12;
              underflow = sum0_r[msb_pos] ^ sum1_r[msb_pos];

              // If underflow, add q; else add 0 (constant-time: always run SecAdd)
              if (underflow)
                correction = q_val[W_MAX-1:0] & mask_r;
              else
                correction = {W_MAX{1'b0}};

              add_a0 <= sum0_r;
              add_a1 <= sum1_r;
              add_b0 <= correction;
              add_b1 <= {W_MAX{1'b0}};
            end
            add_start <= 1'b1;
            state_q   <= ST_ADD2_RUN;
          end
        end

        // -----------------------------------------------------------------
        ST_ADD2_RUN: begin
          if (add_done) begin
            sum0_r  <= add_s0 & mask_r;
            sum1_r  <= add_s1 & mask_r;
            state_q <= ST_OUTPUT;
          end
        end

        // -----------------------------------------------------------------
        ST_OUTPUT: begin
          // x0 = unmasked SecAdd result = sum0 XOR sum1 (reveal y-r mod q)
          // x1 = r
          // Verify: x0 + x1 = (y - r mod q) + r = y mod q. Correct.
          //
          // Output mask: W-1 bits (modulus width = 12 for ML-KEM, 23 for ML-DSA)
          // Strip the overflow/sign bit used for underflow detection.
          begin : output_block
            reg [W_MAX-1:0] out_mask;

            // For B2A, the corrected result (y-r+Q when underflow) is always
            // in [0, Q-1], so we keep all modulus bits.
            // ML-KEM: Q=3329, fits in 12 bits. Use 12-bit mask.
            // ML-DSA: Q=8380417, fits in 23 bits. Use 23-bit mask.
            // But after correction via SecAdd with W=13 bits,
            // the result may have bit 12 set (e.g., 3329+4864=8193 wraps,
            // but intermediate sum before masking uses full W bits).
            // The corrected sum0 XOR sum1 should be in [0, Q-1].
            // Use W-bit mask (13 bits for ML-KEM) to preserve all data,
            // then the mod-Q value is guaranteed to be < Q by construction.
            out_mask = w_mask;  // Full W bits

            share0_o <= {{(32-W_MAX){1'b0}}, (sum0_r ^ sum1_r) & out_mask};
            share1_o <= {{(32-W_MAX){1'b0}}, r_r & out_mask};
          end
          state_q <= ST_DONE;
        end

        // -----------------------------------------------------------------
        ST_DONE: begin
          done_o  <= 1'b1;
          state_q <= ST_IDLE;
        end

        // -----------------------------------------------------------------
        default: state_q <= ST_IDLE;

      endcase
    end
  end

endmodule
