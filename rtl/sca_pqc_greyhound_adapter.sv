// Adapter binding the SCA_PQC coprocessor to the Greyhound SoC CV-X-IF socket.

`include "sca_pqc_pkg.sv"

module sca_pqc_greyhound_adapter (
    input  wire        clk_i,
    input  wire        rst_ni,

    // Greyhound fabric custom instruction interface (from greyhound_soc)
    output wire        fabric_issue_ready_o,
    output wire        fabric_issue_accept_o,
    input  wire        fabric_issue_valid_i,
    input  wire [31:0] fabric_issue_instr_i,
    input  wire [31:0] fabric_issue_op0_i,    // rs1
    input  wire [31:0] fabric_issue_op1_i,    // rs2
    input  wire [3:0]  fabric_issue_id_i,

    output wire        fabric_result_valid_o,
    output wire [3:0]  fabric_result_id_o,
    output wire [4:0]  fabric_result_rd_o,
    output wire [31:0] fabric_result_o
);

    // Coprocessor signals
    wire        copro_issue_ready;
    wire        copro_accept;
    wire        copro_result_valid;
    wire [31:0] copro_result_data;

    // Track inflight instruction ID and rd for result writeback
    reg [3:0] inflight_id;
    reg [4:0] inflight_rd;

    always @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            inflight_id <= 4'd0;
            inflight_rd <= 5'd0;
        end else if (fabric_issue_valid_i && copro_issue_ready && copro_accept) begin
            inflight_id <= fabric_issue_id_i;
            inflight_rd <= fabric_issue_instr_i[11:7]; // rd field from RISC-V encoding
        end
    end

    // Wire mapping: Greyhound fabric ↔ coprocessor
    assign fabric_issue_ready_o  = copro_issue_ready;
    assign fabric_issue_accept_o = copro_accept;
    assign fabric_result_valid_o = copro_result_valid;
    assign fabric_result_id_o    = inflight_id;
    assign fabric_result_rd_o    = inflight_rd;
    assign fabric_result_o       = copro_result_data;

    // SCA_PQC coprocessor instance
    sca_pqc_coprocessor_top i_sca_pqc (
        .clk_i                (clk_i),
        .rst_ni               (rst_ni),
        .x_issue_valid_i      (fabric_issue_valid_i),
        .x_issue_ready_o      (copro_issue_ready),
        .x_issue_req_instr_i  (fabric_issue_instr_i),
        .x_issue_req_rs1_i    (fabric_issue_op0_i),
        .x_issue_req_rs2_i    (fabric_issue_op1_i),
        .x_issue_resp_accept_o(copro_accept),
        .x_result_valid_o     (copro_result_valid),
        .x_result_ready_i     (1'b1),  // Always ready to accept results
        .x_result_data_o      (copro_result_data),
        .x_commit_valid_i     (1'b0)   // No speculative execution
    );

endmodule
