// CV-X-IF top: 38-instruction coprocessor with Hamming-3 FSM and PRNG safety interlock.

`include "sca_pqc_pkg.sv"

module sca_pqc_coprocessor_top (
  input  wire        clk_i,
  input  wire        rst_ni,

  // CVXIF Issue interface
  input  wire        x_issue_valid_i,
  output reg         x_issue_ready_o,
  input  wire [31:0] x_issue_req_instr_i,
  input  wire [31:0] x_issue_req_rs1_i,
  input  wire [31:0] x_issue_req_rs2_i,
  output reg         x_issue_resp_accept_o,

  // CVXIF Result interface
  output reg         x_result_valid_o,
  input  wire        x_result_ready_i,
  output reg  [31:0] x_result_data_o,

  // CVXIF Commit interface (unused but wired)
  input  wire        x_commit_valid_i
);

  // =========================================================================
  // FSM states — Hamming-3 encoded for single-bit upset detection
  // =========================================================================
  localparam [2:0] ST_IDLE         = 3'b000;
  localparam [2:0] ST_ISE_RESULT   = 3'b011;
  localparam [2:0] ST_RESULT_READY = 3'b101;

  (* keep = "true" *) (* keep_hierarchy = "true" *) reg [2:0] state_q;
  reg [2:0] state_d;

  // =========================================================================
  // Instruction decode: opcode + funct3 + funct7[3:0] → 6-bit decoded_op
  // Also extract funct7[4] (bit 29) for dual-Q selection in mask ops
  // =========================================================================
  wire [6:0] instr_opcode    = x_issue_req_instr_i[6:0];
  wire [2:0] instr_funct3    = x_issue_req_instr_i[14:12];
  wire [3:0] instr_funct7_lo = x_issue_req_instr_i[28:25];
  wire       instr_funct7_4  = x_issue_req_instr_i[29]; // Q selector for mask ops

  reg [5:0] decoded_op;

  always @(*) begin
    decoded_op = `OP_INVALID;
    if (instr_opcode == `CUSTOM3_OPCODE) begin
      case (instr_funct3)
        `GRP_KECCAK: begin
          case (instr_funct7_lo)
            4'd0:  decoded_op = `OP_SET_C;
            4'd1:  decoded_op = `OP_BCOP32_AB;
            4'd2:  decoded_op = `OP_SET_HI;
            4'd3:  decoded_op = `OP_ROL32_L;
            4'd4:  decoded_op = `OP_ROL32_H;
            4'd5:  decoded_op = `OP_XOR3;
            default: decoded_op = `OP_INVALID;
          endcase
        end
        `GRP_MOD_ARITH: begin
          case (instr_funct7_lo)
            4'd0:  decoded_op = `OP_BARRETT_K;
            4'd1:  decoded_op = `OP_MONT_K;
            4'd2:  decoded_op = `OP_CADDQ;
            4'd3:  decoded_op = `OP_BARRETT_D;
            4'd4:  decoded_op = `OP_MONT_D;
            default: decoded_op = `OP_INVALID;
          endcase
        end
        `GRP_BUTTERFLY: begin
          case (instr_funct7_lo)
            4'd0:  decoded_op = `OP_NTT_BFLY_CT;
            4'd1:  decoded_op = `OP_NTT_BFLY_GS;
            4'd2:  decoded_op = `OP_NTT_BFLY_CT_MUL;
            4'd3:  decoded_op = `OP_NTT_BFLY_GS_MUL;
            default: decoded_op = `OP_INVALID;
          endcase
        end
        `GRP_CBD: begin
          case (instr_funct7_lo)
            4'd0:  decoded_op = `OP_CBD2_1;
            4'd1:  decoded_op = `OP_CBD2_2;
            4'd2:  decoded_op = `OP_CBD2_3;
            4'd3:  decoded_op = `OP_CBD2_4;
            4'd4:  decoded_op = `OP_CBD2_5;
            4'd5:  decoded_op = `OP_CBD2_6;
            4'd6:  decoded_op = `OP_CBD2_7;
            4'd7:  decoded_op = `OP_CBD2_8;
            4'd8:  decoded_op = `OP_CBD3_1;
            4'd9:  decoded_op = `OP_CBD3_2;
            4'd10: decoded_op = `OP_CBD3_3;
            4'd11: decoded_op = `OP_CBD3_4;
            default: decoded_op = `OP_INVALID;
          endcase
        end
        `GRP_COMPRESS: begin
          case (instr_funct7_lo)
            4'd0:  decoded_op = `OP_COMPRESS_1;
            4'd1:  decoded_op = `OP_COMPRESS_2;
            4'd2:  decoded_op = `OP_COMPRESS_3;
            4'd3:  decoded_op = `OP_COMPRESS_4;
            4'd4:  decoded_op = `OP_REJ_UNIFORM;
            4'd5:  decoded_op = `OP_COMPRESS_5;
            default: decoded_op = `OP_INVALID;
          endcase
        end
        `GRP_MLDSA: begin
          case (instr_funct7_lo)
            4'd0:  decoded_op = `OP_POWER2ROUND;
            4'd1:  decoded_op = `OP_DECOMPOSE;
            default: decoded_op = `OP_INVALID;
          endcase
        end
        `GRP_MASK: begin
          case (instr_funct7_lo)
            4'd0:  decoded_op = `OP_SECMUL_STEP;
            4'd1:  decoded_op = `OP_MASK_REFRESH;
            4'd2:  decoded_op = `OP_MASKED_AND;
            4'd3:  decoded_op = `OP_MASKED_XOR;
            4'd4:  decoded_op = `OP_SECMUL_LATCH;
            4'd5:  decoded_op = `OP_SECMUL_REUSE;
            4'd6:  decoded_op = `OP_AND_LATCH;
            4'd7:  decoded_op = `OP_AND_REUSE;
            default: decoded_op = `OP_INVALID;
          endcase
        end
        `GRP_PRNG: begin
          case (instr_funct7_lo)
            4'd0:  decoded_op = `OP_SEED_PRNG;
            4'd1:  decoded_op = `OP_GET_RANDOM;
            default: decoded_op = `OP_INVALID;
          endcase
        end
        default: decoded_op = `OP_INVALID;
      endcase
    end
  end

  // =========================================================================
  // Registered operands
  // =========================================================================
  (* keep = "true" *) (* keep_hierarchy = "true" *) reg [31:0] rs1_reg;
  (* keep = "true" *) (* keep_hierarchy = "true" *) reg [31:0] rs2_reg;
  (* keep = "true" *) (* keep_hierarchy = "true" *) reg [5:0]  op_reg;
  (* keep = "true" *) (* keep_hierarchy = "true" *) reg        funct7_4_reg; // Q selector for mask ops
  (* keep = "true" *) (* keep_hierarchy = "true" *) reg [31:0] result_reg;

  // =========================================================================
  // ISE module outputs
  // =========================================================================
  wire [31:0] keccak_result;
  wire [31:0] mod_result;
  wire [31:0] bfly_result;
  wire [31:0] cbd_result;
  wire [31:0] compress_result;
  wire [31:0] mldsa_result;
  wire [31:0] mask_result;
  wire [31:0] prng_random;
  wire        prng_ready;

  // PRNG consume signal — asserted when masked ops or GET_RANDOM need randomness
  // GET_RANDOM must also consume so consecutive calls return different values.
  wire prng_consume;
  // PRNG-consuming ops: every op that draws fresh randomness from the PRNG.
  // *_LATCH ops draw fresh r AND latch it; *_REUSE ops do NOT advance PRNG
  // (they consume the captured r), preserving the +r/-r cancellation invariant.
  wire is_mask_op = (op_reg == `OP_SECMUL_STEP)  ||
                    (op_reg == `OP_MASK_REFRESH) ||
                    (op_reg == `OP_MASKED_AND)   ||
                    (op_reg == `OP_SECMUL_LATCH) ||
                    (op_reg == `OP_AND_LATCH);
  wire is_prng_consumer = is_mask_op || (op_reg == `OP_GET_RANDOM);
  assign prng_consume = (state_q == ST_ISE_RESULT) && is_prng_consumer && prng_ready;

  // r_latched capture strobe: 1 cycle while a *_LATCH op is in result phase
  // and PRNG has presented its fresh word. Same condition as prng_consume,
  // but only for the LATCH variants.
  wire is_latch_op = (op_reg == `OP_SECMUL_LATCH) || (op_reg == `OP_AND_LATCH);
  wire latch_strobe = (state_q == ST_ISE_RESULT) && is_latch_op && prng_ready;

  // PRNG seed signals
  wire prng_seed_valid = (state_q == ST_ISE_RESULT) && (op_reg == `OP_SEED_PRNG);

  // =========================================================================
  // ISE module instantiations
  // =========================================================================
  ise_keccak_ops u_keccak (
    .clk_i    (clk_i),
    .rst_ni   (rst_ni),
    .valid_i  (state_q == ST_ISE_RESULT),
    .op_i     (op_reg),
    .rs1_i    (rs1_reg),
    .rs2_i    (rs2_reg),
    .result_o (keccak_result)
  );

  ise_mod_reduce u_mod (
    .clk_i    (clk_i),
    .rst_ni   (rst_ni),
    .op_i     (op_reg),
    .rs1_i    (rs1_reg),
    .rs2_i    (rs2_reg),
    .result_o (mod_result)
  );

  ise_ntt_butterfly u_bfly (
    .clk_i    (clk_i),
    .rst_ni   (rst_ni),
    .op_i     (op_reg),
    .rs1_i    (rs1_reg),
    .rs2_i    (rs2_reg),
    .result_o (bfly_result)
  );

  ise_cbd u_cbd (
    .clk_i    (clk_i),
    .rst_ni   (rst_ni),
    .op_i     (op_reg),
    .rs1_i    (rs1_reg),
    .result_o (cbd_result)
  );

  ise_compress u_compress (
    .clk_i    (clk_i),
    .rst_ni   (rst_ni),
    .op_i     (op_reg),
    .rs1_i    (rs1_reg),
    .result_o (compress_result)
  );

  ise_mldsa_ops u_mldsa (
    .clk_i    (clk_i),
    .rst_ni   (rst_ni),
    .op_i     (op_reg),
    .rs1_i    (rs1_reg),
    .rs2_i    (rs2_reg),
    .result_o (mldsa_result)
  );

  wire mask_trap;
  ise_mask_ops u_mask (
    .clk_i          (clk_i),
    .rst_ni         (rst_ni),
    .op_i           (op_reg),
    .rs1_i          (rs1_reg),
    .rs2_i          (rs2_reg),
    .q_sel_i        (funct7_4_reg),
    .prng_random_i  (prng_random),
    .prng_ready_i   (prng_ready),
    .latch_strobe_i (latch_strobe),
    .result_o       (mask_result),
    .trap_o         (mask_trap)
  );

  // Atomicity-violation trap: a *_REUSE op fired without a prior LATCH
  // (or the latched random was invalidated by an intervening PRNG consumer
  // such as GET_RANDOM, MASK_REFRESH, MASKED_AND, or legacy SECMUL_STEP).
  // Latch the trap so software / SystemVerilog testbench can observe it
  // even if the trapping op is a single cycle.  Wire to CV-X-IF illegal
  // instruction signalling once the host fault-injection harness needs it.
  (* keep = "true" *) (* keep_hierarchy = "true" *) reg mask_trap_latched_q;
  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)            mask_trap_latched_q <= 1'b0;
    else if (mask_trap)     mask_trap_latched_q <= 1'b1;
  end

  ise_prng u_prng (
    .clk_i       (clk_i),
    .rst_ni      (rst_ni),
    .seed_valid_i(prng_seed_valid),
    .seed_data_i (rs1_reg),
    .seed_idx_i  (rs2_reg[1:0]),
    .consume_i   (prng_consume),
    .random_o    (prng_random),
    .ready_o     (prng_ready)
  );

  // =========================================================================
  // Result MUX
  // =========================================================================
  reg [31:0] ise_result;

  always @(*) begin
    case (op_reg)
      `OP_SET_C, `OP_BCOP32_AB, `OP_SET_HI,
      `OP_ROL32_L, `OP_ROL32_H, `OP_XOR3:
        ise_result = keccak_result;

      `OP_BARRETT_K, `OP_MONT_K, `OP_CADDQ,
      `OP_BARRETT_D, `OP_MONT_D:
        ise_result = mod_result;

      `OP_NTT_BFLY_CT, `OP_NTT_BFLY_GS,
      `OP_NTT_BFLY_CT_MUL, `OP_NTT_BFLY_GS_MUL:
        ise_result = bfly_result;

      `OP_CBD2_1, `OP_CBD2_2, `OP_CBD2_3, `OP_CBD2_4,
      `OP_CBD2_5, `OP_CBD2_6, `OP_CBD2_7, `OP_CBD2_8,
      `OP_CBD3_1, `OP_CBD3_2, `OP_CBD3_3, `OP_CBD3_4:
        ise_result = cbd_result;

      `OP_COMPRESS_1, `OP_COMPRESS_2, `OP_COMPRESS_3,
      `OP_COMPRESS_4, `OP_COMPRESS_5, `OP_REJ_UNIFORM:
        ise_result = compress_result;

      `OP_POWER2ROUND, `OP_DECOMPOSE:
        ise_result = mldsa_result;

      `OP_SECMUL_STEP,  `OP_MASK_REFRESH,
      `OP_MASKED_AND,   `OP_MASKED_XOR,
      `OP_SECMUL_LATCH, `OP_SECMUL_REUSE,
      `OP_AND_LATCH,    `OP_AND_REUSE:
        ise_result = mask_result;

      `OP_SEED_PRNG:
        ise_result = rs1_reg; // echo back seed word as ack

      `OP_GET_RANDOM:
        ise_result = prng_random;

      default:
        ise_result = 32'd0;
    endcase
  end

  // =========================================================================
  // PRNG safety: stall in ISE_RESULT if masked op needs PRNG but not ready
  // =========================================================================
  wire prng_stall = is_mask_op && !prng_ready;

  // =========================================================================
  // FSM
  // =========================================================================
  wire accept_issue = (decoded_op != `OP_INVALID);

  always @(*) begin
    state_d               = state_q;
    x_issue_ready_o       = 1'b0;
    x_issue_resp_accept_o = 1'b0;
    x_result_valid_o      = 1'b0;
    x_result_data_o       = 32'd0;

    case (state_q)
      ST_IDLE: begin
        x_issue_ready_o       = 1'b1;
        x_issue_resp_accept_o = accept_issue;
        if (x_issue_valid_i && accept_issue)
          state_d = ST_ISE_RESULT;
      end

      ST_ISE_RESULT: begin
        // Stall if masked op and PRNG not seeded
        if (!prng_stall)
          state_d = ST_RESULT_READY;
        // else: remain in ST_ISE_RESULT until PRNG ready
      end

      ST_RESULT_READY: begin
        x_result_valid_o = 1'b1;
        x_result_data_o  = result_reg;
        if (x_result_ready_i)
          state_d = ST_IDLE;
      end

      default: state_d = ST_IDLE;
    endcase
  end

  // =========================================================================
  // Registered logic
  // =========================================================================
  always @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q      <= ST_IDLE;
      rs1_reg      <= 32'd0;
      rs2_reg      <= 32'd0;
      op_reg       <= `OP_INVALID;
      funct7_4_reg <= 1'b0;
      result_reg   <= 32'd0;
    end else begin
      state_q <= state_d;

      // Capture operands on issue accept
      if (state_q == ST_IDLE && x_issue_valid_i && accept_issue) begin
        rs1_reg      <= x_issue_req_rs1_i;
        rs2_reg      <= x_issue_req_rs2_i;
        op_reg       <= decoded_op;
        funct7_4_reg <= instr_funct7_4;
      end

      // Capture result (only when not stalled)
      if (state_q == ST_ISE_RESULT && !prng_stall) begin
        result_reg <= ise_result;
      end
    end
  end

endmodule
