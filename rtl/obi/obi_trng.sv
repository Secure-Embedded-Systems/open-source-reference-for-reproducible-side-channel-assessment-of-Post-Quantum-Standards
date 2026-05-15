// OBI-bus peripheral wrapping the ring-oscillator TRNG with health-test gating.

module obi_trng (
  input  wire        clk_i,
  input  wire        rst_ni,

  // OBI slave interface
  input  wire        obi_req_i,
  output wire        obi_gnt_o,
  input  wire [31:0] obi_addr_i,
  input  wire        obi_we_i,
  input  wire [31:0] obi_wdata_i,
  output reg         obi_rvalid_o,
  output reg  [31:0] obi_rdata_o,

  // Interrupt output
  output wire        trng_intr_o
);

  // -------------------------------------------------------------------------
  // Address decode — use bits [7:0] for register offset
  // -------------------------------------------------------------------------
  wire [7:0] reg_addr = obi_addr_i[7:0];

  localparam [7:0] ADDR_TRNG_DATA   = 8'h00;  // relative to TRNG base (0x100 in SoC)
  localparam [7:0] ADDR_TRNG_STATUS = 8'h04;
  localparam [7:0] ADDR_TRNG_CTRL   = 8'h08;

  // -------------------------------------------------------------------------
  // OBI grant: always grant immediately
  // -------------------------------------------------------------------------
  assign obi_gnt_o = obi_req_i;

  // -------------------------------------------------------------------------
  // Latched request for response phase
  // -------------------------------------------------------------------------
  reg        req_pending_q;
  reg [7:0]  req_addr_q;
  reg        req_we_q;

  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      req_pending_q <= 1'b0;
      req_addr_q    <= 8'd0;
      req_we_q      <= 1'b0;
    end else begin
      req_pending_q <= obi_req_i & obi_gnt_o;
      req_addr_q    <= reg_addr;
      req_we_q      <= obi_we_i;
    end
  end

  // -------------------------------------------------------------------------
  // Control register
  // -------------------------------------------------------------------------
  reg trng_enable_r;

  // -------------------------------------------------------------------------
  // TRNG entropy source
  // -------------------------------------------------------------------------
  wire rng_bit;

  trng_ro_array #(
    .N_RO   (32),
    .RO_LEN (13)
  ) u_ro_array (
    .clk_i       (clk_i),
    .rst_ni      (rst_ni),
    .enable_i    (trng_enable_r),
    .random_bit_o(rng_bit)
  );

  // -------------------------------------------------------------------------
  // Health testing
  // -------------------------------------------------------------------------
  wire health_error;
  wire health_total_failure;

  trng_health_test #(
    .REP_WINDOW   (28),
    .ADAPT_WINDOW (1024),
    .ADAPT_CUTOFF (589),
    .FAIL_THRESH  (11)
  ) u_health (
    .clk_i          (clk_i),
    .rst_ni         (rst_ni),
    .enable_i       (trng_enable_r),
    .rnd_bit_i      (rng_bit),
    .error_o        (health_error),
    .total_failure_o(health_total_failure)
  );

  wire health_ok = ~health_error & ~health_total_failure;

  // -------------------------------------------------------------------------
  // FSM states
  // -------------------------------------------------------------------------
  localparam [2:0] ST_IDLE       = 3'd0;
  localparam [2:0] ST_BIST       = 3'd1;
  localparam [2:0] ST_ACCUMULATE = 3'd2;
  localparam [2:0] ST_READY      = 3'd3;
  localparam [2:0] ST_WAIT_ACK   = 3'd4;

  reg [2:0] state_q;

  // BIST: run 10 health windows (10 * 1024 = 10240 cycles)
  localparam BIST_CYCLES = 10 * 1024;
  localparam BIST_W = $clog2(BIST_CYCLES + 1);

  reg [BIST_W-1:0] bist_cnt;

  // Accumulator: 32-bit shift register + bit counter
  reg [31:0] accum_sr;
  reg [5:0]  accum_cnt;  // count 0..31 -> 32 bits

  // Output data register
  reg [31:0] trng_data_r;
  reg        trng_valid_r;

  // Clear-on-read flag
  reg        read_ack;

  // Interrupt
  assign trng_intr_o = trng_valid_r;

  // -------------------------------------------------------------------------
  // FSM and accumulation logic
  // -------------------------------------------------------------------------
  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q      <= ST_IDLE;
      bist_cnt     <= {BIST_W{1'b0}};
      accum_sr     <= 32'd0;
      accum_cnt    <= 6'd0;
      trng_data_r  <= 32'd0;
      trng_valid_r <= 1'b0;
      trng_enable_r<= 1'b0;
      read_ack     <= 1'b0;
    end else begin
      // Clear-on-read: detect DATA register read in response phase
      if (req_pending_q && !req_we_q && req_addr_q == ADDR_TRNG_DATA && trng_valid_r)
        read_ack <= 1'b1;
      else
        read_ack <= 1'b0;

      // CTRL register write
      if (obi_req_i && obi_we_i && reg_addr == ADDR_TRNG_CTRL) begin
        trng_enable_r <= obi_wdata_i[0];
      end

      case (state_q)

        ST_IDLE: begin
          trng_valid_r <= 1'b0;
          if (trng_enable_r && !health_total_failure) begin
            state_q  <= ST_BIST;
            bist_cnt <= {BIST_W{1'b0}};
          end
        end

        ST_BIST: begin
          // Run health tests for BIST_CYCLES before producing output
          if (!trng_enable_r || health_total_failure) begin
            state_q <= ST_IDLE;
          end else if (bist_cnt >= BIST_CYCLES[BIST_W-1:0] - 1) begin
            state_q   <= ST_ACCUMULATE;
            accum_cnt <= 6'd0;
            accum_sr  <= 32'd0;
          end else begin
            bist_cnt <= bist_cnt + 1;
          end
        end

        ST_ACCUMULATE: begin
          if (!trng_enable_r || health_total_failure) begin
            state_q <= ST_IDLE;
          end else if (!health_error) begin
            // Only accumulate healthy bits
            accum_sr  <= {accum_sr[30:0], rng_bit};
            accum_cnt <= accum_cnt + 1;
            if (accum_cnt == 6'd31) begin
              // All 32 bits collected
              trng_data_r  <= {accum_sr[30:0], rng_bit};
              trng_valid_r <= 1'b1;
              state_q      <= ST_READY;
            end
          end
          // If health_error (but not total), skip this bit (don't accumulate)
        end

        ST_READY: begin
          if (!trng_enable_r) begin
            state_q      <= ST_IDLE;
            trng_valid_r <= 1'b0;
          end else if (read_ack) begin
            state_q      <= ST_WAIT_ACK;
            trng_valid_r <= 1'b0;
          end
        end

        ST_WAIT_ACK: begin
          // Transition back to accumulate for next word
          accum_cnt <= 6'd0;
          accum_sr  <= 32'd0;
          state_q   <= ST_ACCUMULATE;
        end

        default: begin
          state_q <= ST_IDLE;
        end

      endcase
    end
  end

  // -------------------------------------------------------------------------
  // Register read logic (response phase)
  // -------------------------------------------------------------------------
  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      obi_rvalid_o <= 1'b0;
      obi_rdata_o  <= 32'd0;
    end else begin
      obi_rvalid_o <= req_pending_q;

      if (req_pending_q && !req_we_q) begin
        case (req_addr_q)
          ADDR_TRNG_DATA: begin
            obi_rdata_o <= trng_data_r;
            // Signal clear-on-read to FSM
          end
          ADDR_TRNG_STATUS: begin
            obi_rdata_o <= {29'd0, health_total_failure, health_ok, trng_valid_r};
          end
          default: begin
            obi_rdata_o <= 32'd0;
          end
        endcase
      end else begin
        obi_rdata_o <= 32'd0;
      end
    end
  end

endmodule
