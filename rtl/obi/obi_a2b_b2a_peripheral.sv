// OBI-bus peripheral wrapping the A2B and B2A masking cores.

module obi_a2b_b2a_peripheral (
  input  wire        clk_i,
  input  wire        rst_ni,

  // OBI slave interface
  input  wire        obi_req_i,
  output wire        obi_gnt_o,
  input  wire [31:0] obi_addr_i,
  input  wire        obi_we_i,
  input  wire [3:0]  obi_be_i,
  input  wire [31:0] obi_wdata_i,
  output reg         obi_rvalid_o,
  output reg  [31:0] obi_rdata_o
);

  // -------------------------------------------------------------------------
  // Address decode — use lower 8 bits for register offset
  // -------------------------------------------------------------------------
  wire [7:0] reg_addr = obi_addr_i[7:0];

  localparam [7:0] ADDR_CTRL        = 8'h00;
  localparam [7:0] ADDR_STATUS      = 8'h04;
  localparam [7:0] ADDR_SHARE0_IN   = 8'h08;
  localparam [7:0] ADDR_SHARE1_IN   = 8'h0C;
  localparam [7:0] ADDR_SHARE0_OUT  = 8'h10;
  localparam [7:0] ADDR_SHARE1_OUT  = 8'h14;
  localparam [7:0] ADDR_PRNG_SEED   = 8'h18;
  localparam [7:0] ADDR_PRNG_STATUS = 8'h1C;
  localparam [7:0] ADDR_VERSION     = 8'h20;

  localparam [31:0] VERSION_ID = 32'h58325832;  // "X2X2" — X2X architecture v2

  // -------------------------------------------------------------------------
  // OBI grant: always grant immediately (single-cycle address phase)
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
      if (obi_req_i & obi_gnt_o) begin
        req_addr_q  <= obi_addr_i[7:0];
        req_we_q    <= obi_we_i;
      end
    end
  end

  // -------------------------------------------------------------------------
  // Configuration and data registers
  // -------------------------------------------------------------------------
  reg [31:0] share0_in_r;
  reg [31:0] share1_in_r;
  reg        direction_r;    // 0=A2B, 1=B2A
  reg        wide_mode_r;    // 0=13-bit (ML-KEM), 1=24-bit (ML-DSA)
  reg        start_pulse;
  reg        busy_r;
  reg        done_r;
  reg        error_r;

  // Cycle counter for profiling / status reporting
  reg [7:0]  cycle_cnt_r;

  // -------------------------------------------------------------------------
  // Internal xoshiro128++ PRNG
  // -------------------------------------------------------------------------
  reg [31:0] prng_s [0:3];
  reg [3:0]  prng_seed_done;
  reg [1:0]  prng_seed_idx;
  wire       prng_ready;

  assign prng_ready = &prng_seed_done;

  // xoshiro128++ output function
  wire [31:0] prng_sum_03   = prng_s[0] + prng_s[3];
  wire [31:0] prng_rotl_sum = {prng_sum_03[24:0], prng_sum_03[31:25]};
  wire [31:0] prng_random   = prng_rotl_sum + prng_s[0];

  // xoshiro128++ state advance
  wire [31:0] prng_t     = {prng_s[1][22:0], 9'd0};
  wire [31:0] prng_s2new = prng_s[2] ^ prng_s[0];
  wire [31:0] prng_s3new = prng_s[3] ^ prng_s[1];
  wire [31:0] prng_s1new = prng_s[1] ^ prng_s2new;
  wire [31:0] prng_s0new = prng_s[0] ^ prng_s3new;
  wire [31:0] prng_s2fin = prng_s2new ^ prng_t;
  wire [31:0] prng_s3fin = {prng_s3new[20:0], prng_s3new[31:21]};

  // PRNG consume: advance state continuously while busy (cores need fresh
  // randomness every cycle for their internal rnd_pool fill)
  reg prng_consume;

  // -------------------------------------------------------------------------
  // A2B core instance (X2X architecture — SecAdd based)
  // -------------------------------------------------------------------------
  wire        a2b_start;
  wire [31:0] a2b_share0_o;
  wire [31:0] a2b_share1_o;
  wire        a2b_done;

  assign a2b_start = start_pulse & ~direction_r;

  a2b_core u_a2b (
    .clk_i      (clk_i),
    .rst_ni     (rst_ni),
    .start_i    (a2b_start),
    .share0_i   (share0_in_r),
    .share1_i   (share1_in_r),
    .random_i   (prng_random),
    .wide_mode_i(wide_mode_r),
    .share0_o   (a2b_share0_o),
    .share1_o   (a2b_share1_o),
    .done_o     (a2b_done)
  );

  // -------------------------------------------------------------------------
  // B2A core instance (X2X architecture — SecAdd based)
  // -------------------------------------------------------------------------
  wire        b2a_start;
  wire [31:0] b2a_share0_o;
  wire [31:0] b2a_share1_o;
  wire        b2a_done;

  assign b2a_start = start_pulse & direction_r;

  b2a_core u_b2a (
    .clk_i      (clk_i),
    .rst_ni     (rst_ni),
    .start_i    (b2a_start),
    .share0_i   (share0_in_r),
    .share1_i   (share1_in_r),
    .random_i   (prng_random),
    .wide_mode_i(wide_mode_r),
    .share0_o   (b2a_share0_o),
    .share1_o   (b2a_share1_o),
    .done_o     (b2a_done)
  );

  // -------------------------------------------------------------------------
  // Output share registers (latched when conversion completes)
  // -------------------------------------------------------------------------
  reg [31:0] share0_out_r;
  reg [31:0] share1_out_r;

  wire conv_done = direction_r ? b2a_done : a2b_done;

  // -------------------------------------------------------------------------
  // Register write logic
  // -------------------------------------------------------------------------
  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      share0_in_r    <= 32'd0;
      share1_in_r    <= 32'd0;
      direction_r    <= 1'b0;
      wide_mode_r    <= 1'b0;
      start_pulse    <= 1'b0;
      busy_r         <= 1'b0;
      done_r         <= 1'b0;
      error_r        <= 1'b0;
      cycle_cnt_r    <= 8'd0;
      share0_out_r   <= 32'd0;
      share1_out_r   <= 32'd0;
      prng_s[0]      <= 32'd0;
      prng_s[1]      <= 32'd0;
      prng_s[2]      <= 32'd0;
      prng_s[3]      <= 32'd0;
      prng_seed_done <= 4'b0000;
      prng_seed_idx  <= 2'd0;
      prng_consume   <= 1'b0;
    end else begin
      // Default: clear single-cycle pulses
      start_pulse  <= 1'b0;

      // Register writes (address phase — immediate)
      if (obi_req_i && obi_we_i) begin
        case (reg_addr)
          ADDR_CTRL: begin
            direction_r <= obi_wdata_i[1];
            wide_mode_r <= obi_wdata_i[2];
            if (obi_wdata_i[0] && !busy_r && prng_ready) begin
              start_pulse  <= 1'b1;
              busy_r       <= 1'b1;
              done_r       <= 1'b0;
              error_r      <= 1'b0;
              cycle_cnt_r  <= 8'd0;
              prng_consume <= 1'b1;
            end else if (obi_wdata_i[0] && !prng_ready) begin
              // Error: PRNG not seeded
              error_r <= 1'b1;
            end
          end

          ADDR_SHARE0_IN: begin
            share0_in_r <= obi_wdata_i;
          end

          ADDR_SHARE1_IN: begin
            share1_in_r <= obi_wdata_i;
          end

          ADDR_PRNG_SEED: begin
            prng_s[prng_seed_idx]         <= obi_wdata_i;
            prng_seed_done[prng_seed_idx] <= 1'b1;
            prng_seed_idx                 <= prng_seed_idx + 2'd1;
          end

          default: ; // ignore writes to read-only registers
        endcase
      end

      // PRNG state advance: continuously while busy (cores pull randomness
      // each cycle for their internal pool)
      if (prng_consume && prng_ready) begin
        prng_s[0] <= prng_s0new;
        prng_s[1] <= prng_s1new;
        prng_s[2] <= prng_s2fin;
        prng_s[3] <= prng_s3fin;
      end

      // Cycle counter while busy
      if (busy_r && !conv_done) begin
        if (cycle_cnt_r < 8'hFF)
          cycle_cnt_r <= cycle_cnt_r + 8'd1;
      end

      // Conversion completion
      if (conv_done && busy_r) begin
        busy_r       <= 1'b0;
        done_r       <= 1'b1;
        prng_consume <= 1'b0;
        if (direction_r) begin
          share0_out_r <= b2a_share0_o;
          share1_out_r <= b2a_share1_o;
        end else begin
          share0_out_r <= a2b_share0_o;
          share1_out_r <= a2b_share1_o;
        end
      end
    end
  end

  // -------------------------------------------------------------------------
  // Register read logic (response phase)
  // rvalid is registered (one cycle after address phase).
  // rdata is combinational from req_addr_q (same cycle as rvalid).
  // -------------------------------------------------------------------------
  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      obi_rvalid_o <= 1'b0;
    else
      obi_rvalid_o <= req_pending_q;
  end

  // Drive rdata based on latched req_addr_q so the bus value is still valid
  // on the cycle rvalid is asserted (one cycle AFTER req_pending_q falls).
  // req_we_q gate kept so writes don't echo stale reads.
  always @(*) begin
    obi_rdata_o = 32'd0;
    if (!req_we_q) begin
      case (req_addr_q)
        ADDR_STATUS:      obi_rdata_o = {16'd0, cycle_cnt_r, 5'd0, error_r, done_r, busy_r};
        ADDR_SHARE0_OUT:  obi_rdata_o = share0_out_r;
        ADDR_SHARE1_OUT:  obi_rdata_o = share1_out_r;
        ADDR_PRNG_STATUS: obi_rdata_o = {31'd0, prng_ready};
        ADDR_VERSION:     obi_rdata_o = VERSION_ID;
        default:          obi_rdata_o = 32'd0;
      endcase
    end
  end

endmodule
