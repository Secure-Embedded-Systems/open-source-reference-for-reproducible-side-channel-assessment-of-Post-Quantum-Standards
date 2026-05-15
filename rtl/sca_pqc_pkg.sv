// Package: 38-instruction opcode/typedef set for the SCA_PQC ISE (RISC-V Custom-3 0x7B).

`ifndef SCA_PQC_PKG_SV
`define SCA_PQC_PKG_SV

// ============================================================================
// Custom-3 Opcode
// ============================================================================
`define CUSTOM3_OPCODE  7'b1111011

// ============================================================================
// Group Select — funct3 (bits [14:12])
// ============================================================================
`define GRP_KECCAK     3'b000
`define GRP_MOD_ARITH  3'b001
`define GRP_BUTTERFLY  3'b010
`define GRP_CBD        3'b011
`define GRP_COMPRESS   3'b100
`define GRP_MLDSA      3'b101
`define GRP_MASK       3'b110
`define GRP_PRNG       3'b111

// ============================================================================
// Group 0: Keccak (funct3 = 3'b000) — 6 ops
// ============================================================================
`define OP_SET_C        6'd0    // acc_c <- rs1
`define OP_BCOP32_AB    6'd1    // rs1 ^ (~rs2 & acc_c)
`define OP_SET_HI       6'd2    // acc_hi <- rs1
`define OP_ROL32_L      6'd3    // ROL64 low
`define OP_ROL32_H      6'd4    // ROL64 high
`define OP_XOR3         6'd5    // rs1 ^ rs2 ^ acc_c

// ============================================================================
// Group 1: Modular Arithmetic (funct3 = 3'b001) — 5 ops
// ============================================================================
`define OP_BARRETT_K    6'd6    // mod 3329
`define OP_MONT_K       6'd7    // Montgomery Q=3329
`define OP_CADDQ        6'd8    // conditional add Q
`define OP_BARRETT_D    6'd9    // mod 8380417
`define OP_MONT_D       6'd10   // Montgomery Q=8380417

// ============================================================================
// Group 2: NTT Butterfly (funct3 = 3'b010) — 2 ops
// ============================================================================
`define OP_NTT_BFLY_CT     6'd11   // Cooley-Tukey, sum/diff only
`define OP_NTT_BFLY_GS     6'd12   // Gentleman-Sande, sum/diff only
`define OP_NTT_BFLY_CT_MUL 6'd39   // CT with Mont-mul twiddle
`define OP_NTT_BFLY_GS_MUL 6'd40   // GS with Mont-mul twiddle

// ============================================================================
// Group 3: CBD Sampling (funct3 = 3'b011) — 12 ops
// ============================================================================
`define OP_CBD2_1       6'd13   // CBD eta=2, step 1
`define OP_CBD2_2       6'd14   // CBD eta=2, step 2
`define OP_CBD2_3       6'd15   // CBD eta=2, step 3
`define OP_CBD2_4       6'd16   // CBD eta=2, step 4
`define OP_CBD2_5       6'd17   // CBD eta=2, step 5
`define OP_CBD2_6       6'd18   // CBD eta=2, step 6
`define OP_CBD2_7       6'd19   // CBD eta=2, step 7
`define OP_CBD2_8       6'd20   // CBD eta=2, step 8
`define OP_CBD3_1       6'd21   // CBD eta=3, step 1
`define OP_CBD3_2       6'd22   // CBD eta=3, step 2
`define OP_CBD3_3       6'd23   // CBD eta=3, step 3
`define OP_CBD3_4       6'd24   // CBD eta=3, step 4

// ============================================================================
// Group 4: Compress + Sampling (funct3 = 3'b100) — 5 ops
// ============================================================================
`define OP_COMPRESS_1   6'd25   // Compress step 1
`define OP_COMPRESS_2   6'd26   // Compress step 2
`define OP_COMPRESS_3   6'd27   // Compress step 3
`define OP_COMPRESS_4   6'd28   // Compress step 4 (d=10)
`define OP_REJ_UNIFORM  6'd29   // Rejection uniform sampling
`define OP_COMPRESS_5   6'd38   // Compress step 5 (d=11, ML-KEM-1024 du)

// ============================================================================
// Group 5: ML-DSA Operations (funct3 = 3'b101) — 2 ops
// ============================================================================
`define OP_POWER2ROUND  6'd30   // Power2Round
`define OP_DECOMPOSE    6'd31   // Decompose

// ============================================================================
// Group 6: Masking Primitives (funct3 = 3'b110) — 8 ops
// ============================================================================
// Slots 0-3: legacy single-blind primitives (kept for back-compat / micro-bench)
`define OP_SECMUL_STEP  6'd32   // (rs1*rs2+PRNG) mod q; funct7[4] selects Q
`define OP_MASK_REFRESH 6'd33   // re-randomize share
`define OP_MASKED_AND   6'd34   // AND_BLIND: (rs1&rs2)^PRNG, single building block
`define OP_MASKED_XOR   6'd35   // linear XOR
// Slots 4-7: paired DOM gadgets (Gross-Mangard "+r/-r" cancellation pattern).
// LATCH op draws fresh PRNG word, captures it in r_latched, returns blinded result.
// REUSE op consumes r_latched (no PRNG advance) so the +r and -r terms cancel
// exactly when shares are recombined. Use as { _LATCH ; _REUSE } pairs.
`define OP_SECMUL_LATCH 6'd41   // (rs1*rs2 + r_new) mod q; latch r_new
`define OP_SECMUL_REUSE 6'd42   // (rs1*rs2 - r_latched) mod q
`define OP_AND_LATCH    6'd43   // (rs1 & rs2) ^ r_new; latch r_new
`define OP_AND_REUSE    6'd44   // (rs1 & rs2) ^ r_latched

// ============================================================================
// Group 7: PRNG Control (funct3 = 3'b111) — 2 ops
// ============================================================================
`define OP_SEED_PRNG    6'd36   // Seed PRNG
`define OP_GET_RANDOM   6'd37   // Get random word

// ============================================================================
// Invalid / Reserved
// ============================================================================
`define OP_INVALID      6'd63

// ============================================================================
// FIPS-203 / FIPS-204 prime constants -- one source of truth.
// Used by ise_mod_reduce.sv, ise_mask_ops.sv, and any future ML-DSA module
// that needs the prime or the Montgomery negation constant.
// ============================================================================
`define MLKEM_Q             16'd3329
`define MLKEM_QINV_NEG_R16  16'd62209          // -Q^{-1} mod 2^16 (Q=3329)
`define MLDSA_Q             24'd8380417
`define MLDSA_QINV_NEG_R32  32'd4236238847     // -Q^{-1} mod 2^32 (Q=8380417)

`endif // SCA_PQC_PKG_SV
