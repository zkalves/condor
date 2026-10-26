//
// This file is part of Condor.
//
// Condor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Copyright (C) 2026 Ezequiel Alves. All rights reserved.
//

// SystemVerilog ALU Design
// Compliant with IEEE 1800-2017 & Project HDL Guidelines

module alu #(
  parameter int unsigned DATA_WIDTH = 32,
  parameter int unsigned OP_WIDTH   = 4
) (
  input  logic                  clk_i,
  input  logic                  rst_ni,
  input  logic                  valid_i,
  input  logic [OP_WIDTH-1:0]   op_i,
  input  logic [DATA_WIDTH-1:0] a_i,
  input  logic [DATA_WIDTH-1:0] b_i,
  output logic                  valid_o,
  output logic [DATA_WIDTH-1:0] result_o,
  output logic                  zero_o,
  output logic                  overflow_o
);

  // Operation typedef
  typedef enum logic [OP_WIDTH-1:0] {
    ALU_ADD = 4'h0,
    ALU_SUB = 4'h1,
    ALU_AND = 4'h2,
    ALU_OR  = 4'h3,
    ALU_XOR = 4'h4,
    ALU_SLL = 4'h5,
    ALU_SRL = 4'h6,
    ALU_SRA = 4'h7,
    ALU_MUL = 4'h8
  } alu_op_t;

  // Intermediate combinational signals
  logic [DATA_WIDTH:0]   sum_ext_c;
  logic [DATA_WIDTH:0]   diff_ext_c;
  logic [DATA_WIDTH-1:0] result_d;
  logic                  valid_d;
  logic                  zero_d;
  logic                  overflow_d;

  // Sequential registers
  logic [DATA_WIDTH-1:0] result_q;
  logic                  valid_q;
  logic                  zero_q;
  logic                  overflow_q;

  // Combinational arithmetic helpers
  assign sum_ext_c  = {1'b0, a_i} + {1'b0, b_i};
  assign diff_ext_c = {1'b0, a_i} - {1'b0, b_i};

  // Combinational ALU Core Logic
  always_comb begin
    valid_d    = valid_i;
    result_d   = '0;
    overflow_d = 1'b0;

    if (valid_i) begin
      case (op_i)
        ALU_ADD: begin
          result_d   = sum_ext_c[DATA_WIDTH-1:0];
          overflow_d = (a_i[DATA_WIDTH-1] == b_i[DATA_WIDTH-1]) &&
                       (result_d[DATA_WIDTH-1] != a_i[DATA_WIDTH-1]);
        end

        ALU_SUB: begin
          result_d   = diff_ext_c[DATA_WIDTH-1:0];
          overflow_d = (a_i[DATA_WIDTH-1] != b_i[DATA_WIDTH-1]) &&
                       (result_d[DATA_WIDTH-1] != a_i[DATA_WIDTH-1]);
        end

        ALU_AND: begin
          result_d = a_i & b_i;
        end

        ALU_OR: begin
          result_d = a_i | b_i;
        end

        ALU_XOR: begin
          result_d = a_i ^ b_i;
        end

        ALU_SLL: begin
          result_d = a_i << b_i[4:0];
        end

        ALU_SRL: begin
          result_d = a_i >> b_i[4:0];
        end

        ALU_SRA: begin
          result_d = $signed(a_i) >>> b_i[4:0];
        end

        ALU_MUL: begin
          result_d = a_i * b_i;
        end

        default: begin
          result_d   = '0;
          overflow_d = 1'b0;
        end
      endcase
    end

    zero_d = (result_d == '0);
  end

  // Sequential register stage
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_q    <= 1'b0;
      result_q   <= '0;
      zero_q     <= 1'b0;
      overflow_q <= 1'b0;
    end else begin
      valid_q    <= valid_d;
      result_q   <= result_d;
      zero_q     <= zero_d;
      overflow_q <= overflow_d;
    end
  end

  // Output assignments
  assign valid_o    = valid_q;
  assign result_o   = result_q;
  assign zero_o     = zero_q;
  assign overflow_o = overflow_q;

endmodule
