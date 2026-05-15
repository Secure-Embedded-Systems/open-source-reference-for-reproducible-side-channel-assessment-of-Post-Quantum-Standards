// Boolean-domain secure adder (Kogge-Stone style over shared inputs).

(* keep_hierarchy = "yes" *)
module sec_add #(
  parameter WIDTH    = 13,
  parameter N_STAGES = 4    // ceil(log2(WIDTH))
) (
  input  wire                          clk_i,
  input  wire                          rst_ni,
  input  wire                          start_i,

  input  wire [WIDTH-1:0]              a0_i, a1_i,   // shares of a
  input  wire [WIDTH-1:0]              b0_i, b1_i,   // shares of b

  // Randomness: 1 word for triangle AND, 1 for triangle OR,
  // 3 words per box stage (AND for P*G_lo, AND for P*P_lo, OR for G|PG)
  // Total: 2 + 3*N_STAGES words
  input  wire [(2+3*N_STAGES)*WIDTH-1:0] rnd_i,

  output reg  [WIDTH-1:0]             s0_o, s1_o,    // shares of sum
  output reg                          done_o
);

  // -------------------------------------------------------------------------
  // Randomness slicing helper
  // -------------------------------------------------------------------------
  function automatic [WIDTH-1:0] rnd_slice;
    input integer idx;
    integer base;
    begin
      base = idx * WIDTH;
      rnd_slice = rnd_i[base +: WIDTH];
    end
  endfunction

  // -------------------------------------------------------------------------
  // FSM
  // -------------------------------------------------------------------------
  localparam ST_IDLE     = 4'd0;
  localparam ST_TRIANGLE = 4'd1;  // compute initial (g, p) — launch SecAnd+SecOR
  localparam ST_TRI_WAIT = 4'd2;  // wait for triangle SecAnd/SecOR done
  localparam ST_BOX_AND1 = 4'd3;  // box stage: compute P_hi AND G_lo
  localparam ST_BOX_W1   = 4'd4;  // wait for first AND
  localparam ST_BOX_AND2 = 4'd5;  // box stage: compute P_hi AND P_lo, OR for G
  localparam ST_BOX_W2   = 4'd6;  // wait for second AND/OR
  localparam ST_FINAL    = 4'd7;  // compute sum and register output
  localparam ST_DONE     = 4'd8;

  reg [3:0]  state_q;
  reg [2:0]  stage_cnt;  // which prefix stage (0..N_STAGES-1)

  // -------------------------------------------------------------------------
  // Working registers for generate/propagate shares
  // -------------------------------------------------------------------------
  reg [WIDTH-1:0] g0_r, g1_r;   // generate shares
  reg [WIDTH-1:0] p0_r, p1_r;   // propagate shares
  reg [WIDTH-1:0] axb0_r, axb1_r; // a XOR b shares (needed for final sum)

  // -------------------------------------------------------------------------
  // SecAnd instance (shared, time-multiplexed)
  // -------------------------------------------------------------------------
  reg              and_en;
  reg  [WIDTH-1:0] and_a0, and_a1, and_b0, and_b1, and_rnd;
  wire [WIDTH-1:0] and_c0, and_c1;
  wire             and_done;

  sec_and #(.WIDTH(WIDTH)) u_and (
    .clk_i  (clk_i),
    .rst_ni (rst_ni),
    .en_i   (and_en),
    .a0_i   (and_a0),
    .a1_i   (and_a1),
    .b0_i   (and_b0),
    .b1_i   (and_b1),
    .rnd_i  (and_rnd),
    .c0_o   (and_c0),
    .c1_o   (and_c1),
    .done_o (and_done)
  );

  // -------------------------------------------------------------------------
  // SecOR instance (shared, time-multiplexed)
  // Uses its own sec_and internally — 1 cycle latency
  // -------------------------------------------------------------------------
  reg              or_en;
  reg  [WIDTH-1:0] or_a0, or_a1, or_b0, or_b1, or_rnd;
  wire [WIDTH-1:0] or_c0, or_c1;
  wire             or_done;

  sec_or #(.WIDTH(WIDTH)) u_or (
    .clk_i  (clk_i),
    .rst_ni (rst_ni),
    .en_i   (or_en),
    .a0_i   (or_a0),
    .a1_i   (or_a1),
    .b0_i   (or_b0),
    .b1_i   (or_b1),
    .rnd_i  (or_rnd),
    .c0_o   (or_c0),
    .c1_o   (or_c1),
    .done_o (or_done)
  );

  // -------------------------------------------------------------------------
  // Kogge-Stone prefix routing: shift g/p by 2^stage positions
  // For stage j, bit i gets prefix from bit i - 2^j (if i >= 2^j).
  // Bits below the threshold pass through unchanged.
  // -------------------------------------------------------------------------
  reg [WIDTH-1:0] g_lo0, g_lo1;  // g shifted down by 2^stage
  reg [WIDTH-1:0] p_lo0, p_lo1;  // p shifted down by 2^stage
  reg [WIDTH-1:0] active_mask;   // which bits participate in this stage

  // Temporary registers for box intermediate results
  reg [WIDTH-1:0] pg_and0, pg_and1;  // P_hi AND G_lo result

  // Randomness index counter (max = 2 + 3*N_STAGES - 1)
  reg [4:0] rnd_idx;

  always @(*) begin : prefix_routing
    integer shift_amt;
    integer i;

    shift_amt = (1 << stage_cnt);

    g_lo0      = {WIDTH{1'b0}};
    g_lo1      = {WIDTH{1'b0}};
    p_lo0      = {WIDTH{1'b0}};
    p_lo1      = {WIDTH{1'b0}};
    active_mask = {WIDTH{1'b0}};

    for (i = 0; i < WIDTH; i = i + 1) begin
      if (i >= shift_amt) begin
        g_lo0[i]       = g0_r[i - shift_amt];
        g_lo1[i]       = g1_r[i - shift_amt];
        p_lo0[i]       = p0_r[i - shift_amt];
        p_lo1[i]       = p1_r[i - shift_amt];
        active_mask[i] = 1'b1;
      end
    end
  end

  // -------------------------------------------------------------------------
  // FSM
  // -------------------------------------------------------------------------
  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q   <= ST_IDLE;
      stage_cnt <= 3'd0;
      g0_r      <= {WIDTH{1'b0}};
      g1_r      <= {WIDTH{1'b0}};
      p0_r      <= {WIDTH{1'b0}};
      p1_r      <= {WIDTH{1'b0}};
      axb0_r    <= {WIDTH{1'b0}};
      axb1_r    <= {WIDTH{1'b0}};
      pg_and0   <= {WIDTH{1'b0}};
      pg_and1   <= {WIDTH{1'b0}};
      s0_o      <= {WIDTH{1'b0}};
      s1_o      <= {WIDTH{1'b0}};
      done_o    <= 1'b0;
      and_en    <= 1'b0;
      and_a0    <= {WIDTH{1'b0}};
      and_a1    <= {WIDTH{1'b0}};
      and_b0    <= {WIDTH{1'b0}};
      and_b1    <= {WIDTH{1'b0}};
      and_rnd   <= {WIDTH{1'b0}};
      or_en     <= 1'b0;
      or_a0     <= {WIDTH{1'b0}};
      or_a1     <= {WIDTH{1'b0}};
      or_b0     <= {WIDTH{1'b0}};
      or_b1     <= {WIDTH{1'b0}};
      or_rnd    <= {WIDTH{1'b0}};
      rnd_idx   <= 5'd0;
    end else begin
      // Defaults
      and_en <= 1'b0;
      or_en  <= 1'b0;
      done_o <= 1'b0;

      case (state_q)

        // -----------------------------------------------------------------
        ST_IDLE: begin
          if (start_i) begin
            // Latch a XOR b for final sum computation
            axb0_r  <= a0_i ^ b0_i;
            axb1_r  <= a1_i ^ b1_i;
            rnd_idx <= 4'd0;
            state_q <= ST_TRIANGLE;
          end
        end

        // -----------------------------------------------------------------
        // Triangle: g = AND(a, b), p = OR(a, b) — both launch in parallel
        // SecAnd computes g, SecOR computes p — both take 1 cycle
        // -----------------------------------------------------------------
        ST_TRIANGLE: begin
          // Launch SecAnd for generate: g = a AND b
          and_en  <= 1'b1;
          and_a0  <= a0_i;
          and_a1  <= a1_i;
          and_b0  <= b0_i;
          and_b1  <= b1_i;
          and_rnd <= rnd_slice(0);

          // Launch SecOR for propagate: p = a OR b
          or_en  <= 1'b1;
          or_a0  <= a0_i;
          or_a1  <= a1_i;
          or_b0  <= b0_i;
          or_b1  <= b1_i;
          or_rnd <= rnd_slice(1);

          rnd_idx   <= 5'd2;
          state_q   <= ST_TRI_WAIT;
        end

        // -----------------------------------------------------------------
        ST_TRI_WAIT: begin
          if (and_done) begin
            // Capture triangle results
            g0_r      <= and_c0;
            g1_r      <= and_c1;
            p0_r      <= or_c0;
            p1_r      <= or_c1;
            stage_cnt <= 3'd0;
            state_q   <= ST_BOX_AND1;
          end
        end

        // -----------------------------------------------------------------
        // Box stage — part 1: compute P_hi AND G_lo
        // Kogge-Stone: G_new[i] = G[i] OR (P[i] AND G[i-2^j])
        //              P_new[i] = P[i] AND P[i-2^j]
        // First, compute temp = P[i] AND G[i-2^j]
        // -----------------------------------------------------------------
        ST_BOX_AND1: begin
          if (stage_cnt < N_STAGES) begin
            // P_hi AND G_lo
            and_en  <= 1'b1;
            and_a0  <= p0_r;
            and_a1  <= p1_r;
            and_b0  <= g_lo0;
            and_b1  <= g_lo1;
            and_rnd <= rnd_slice(rnd_idx);
            state_q <= ST_BOX_W1;
          end else begin
            // All stages done — compute final sum
            state_q <= ST_FINAL;
          end
        end

        // -----------------------------------------------------------------
        ST_BOX_W1: begin
          if (and_done) begin
            // Save P AND G_lo result, masked by active_mask
            // Inactive bits keep their original g value
            pg_and0 <= (and_c0 & active_mask) | (g0_r & ~active_mask);
            pg_and1 <= (and_c1 & active_mask) | (g1_r & ~active_mask);

            // Now launch: P_hi AND P_lo for propagate update
            // AND SecOR(pg_and, g) for generate update runs in parallel
            // Actually, G_new = G[i] OR (P[i] AND G_lo[i]) = OR(g, pg_and)
            // So we need SecOR for the g update, and SecAnd for p update

            // Launch SecAnd for P_new = P_hi AND P_lo
            and_en  <= 1'b1;
            and_a0  <= p0_r;
            and_a1  <= p1_r;
            and_b0  <= p_lo0;
            and_b1  <= p_lo1;
            and_rnd <= rnd_slice(rnd_idx + 1);

            // Launch SecOR for G_new = G OR (P AND G_lo) = OR(g, pg_and)
            or_en  <= 1'b1;
            or_a0  <= g0_r;
            or_a1  <= g1_r;
            or_b0  <= (and_c0 & active_mask) | (g0_r & ~active_mask);
            or_b1  <= (and_c1 & active_mask) | (g1_r & ~active_mask);
            or_rnd <= rnd_slice(rnd_idx + 2);

            rnd_idx <= rnd_idx + 5'd3;
            state_q <= ST_BOX_W2;
          end
        end

        // -----------------------------------------------------------------
        ST_BOX_W2: begin
          if (and_done) begin
            // Update propagate: active bits get AND result, others pass through
            p0_r <= (and_c0 & active_mask) | (p0_r & ~active_mask);
            p1_r <= (and_c1 & active_mask) | (p1_r & ~active_mask);

            // Update generate: active bits get OR result, others pass through
            g0_r <= (or_c0 & active_mask) | (g0_r & ~active_mask);
            g1_r <= (or_c1 & active_mask) | (g1_r & ~active_mask);

            stage_cnt <= stage_cnt + 3'd1;
            state_q   <= ST_BOX_AND1;
          end
        end

        // -----------------------------------------------------------------
        // Final: sum[i] = (a[i] XOR b[i]) XOR carry[i-1]
        // carry = generate (after all prefix stages)
        // carry for bit i is g[i-1], carry for bit 0 is 0
        // -----------------------------------------------------------------
        ST_FINAL: begin
          // Carry shares: shift g right by 1 (carry into bit i from bit i-1)
          // carry[0] = 0, carry[i] = g[i-1] for i > 0
          s0_o <= axb0_r ^ {g0_r[WIDTH-2:0], 1'b0};
          s1_o <= axb1_r ^ {g1_r[WIDTH-2:0], 1'b0};
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
