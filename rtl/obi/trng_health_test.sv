// TRNG NIST SP 800-90B health tests (repetition-count, adaptive-proportion).

module trng_health_test #(
  parameter REP_WINDOW   = 28,
  parameter ADAPT_WINDOW = 1024,
  parameter ADAPT_CUTOFF = 589,
  parameter FAIL_THRESH  = 11
)(
  input  wire clk_i,
  input  wire rst_ni,
  input  wire enable_i,
  input  wire rnd_bit_i,

  output wire error_o,
  output wire total_failure_o
);

  // -------------------------------------------------------------------------
  // Repetition count test
  // Shift register of REP_WINDOW bits; error if all 0 or all 1
  // -------------------------------------------------------------------------
  reg [REP_WINDOW-1:0] rep_sr;
  wire rep_all_zero;
  wire rep_all_one;
  wire rep_error;

  assign rep_all_zero = (rep_sr == {REP_WINDOW{1'b0}});
  assign rep_all_one  = (rep_sr == {REP_WINDOW{1'b1}});
  assign rep_error    = rep_all_zero | rep_all_one;

  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      rep_sr <= {REP_WINDOW{1'b0}};
    else if (enable_i)
      rep_sr <= {rep_sr[REP_WINDOW-2:0], rnd_bit_i};
  end

  // -------------------------------------------------------------------------
  // Adaptive proportion test
  // Count 1s in a window of ADAPT_WINDOW samples.
  // Error if count > ADAPT_CUTOFF or count < (ADAPT_WINDOW - ADAPT_CUTOFF).
  // -------------------------------------------------------------------------
  // Window sample counter width
  localparam CNT_W = $clog2(ADAPT_WINDOW + 1);

  reg [CNT_W-1:0]  ones_count;
  reg [CNT_W-1:0]  sample_count;
  reg               adapt_error_r;
  reg               adapt_window_valid;

  // Lower cutoff: if fewer than (WINDOW - CUTOFF) ones, bias towards 0
  localparam [CNT_W-1:0] ADAPT_LOWER = ADAPT_WINDOW - ADAPT_CUTOFF;

  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      ones_count         <= {CNT_W{1'b0}};
      sample_count       <= {CNT_W{1'b0}};
      adapt_error_r      <= 1'b0;
      adapt_window_valid <= 1'b0;
    end else if (enable_i) begin
      if (sample_count == ADAPT_WINDOW[CNT_W-1:0] - 1) begin
        // End of window: evaluate and reset
        adapt_window_valid <= 1'b1;
        if ((ones_count + rnd_bit_i) > ADAPT_CUTOFF[CNT_W-1:0] ||
            (ones_count + rnd_bit_i) < ADAPT_LOWER)
          adapt_error_r <= 1'b1;
        else
          adapt_error_r <= 1'b0;
        ones_count   <= {CNT_W{1'b0}};
        sample_count <= {CNT_W{1'b0}};
      end else begin
        sample_count <= sample_count + 1;
        ones_count   <= ones_count + {{(CNT_W-1){1'b0}}, rnd_bit_i};
      end
    end
  end

  // -------------------------------------------------------------------------
  // Combined error signal
  // Repetition error is immediate; adaptive is valid only after first window
  // -------------------------------------------------------------------------
  wire current_error;
  assign current_error = rep_error | (adapt_window_valid & adapt_error_r);
  assign error_o = current_error;

  // -------------------------------------------------------------------------
  // Total failure: consecutive error counter
  // -------------------------------------------------------------------------
  localparam FAIL_W = $clog2(FAIL_THRESH + 1);

  reg [FAIL_W-1:0] fail_count;
  reg               total_fail_r;

  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      fail_count   <= {FAIL_W{1'b0}};
      total_fail_r <= 1'b0;
    end else if (enable_i) begin
      // Only evaluate at window boundaries for consecutive counting
      if (sample_count == {CNT_W{1'b0}} && adapt_window_valid) begin
        if (current_error) begin
          if (fail_count >= FAIL_THRESH[FAIL_W-1:0] - 1)
            total_fail_r <= 1'b1;
          else
            fail_count <= fail_count + 1;
        end else begin
          fail_count <= {FAIL_W{1'b0}};
        end
      end
    end
  end

  assign total_failure_o = total_fail_r;

endmodule
