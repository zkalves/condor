/*
 * This file is part of Condor.
 *
 * Condor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Condor is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Condor.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2026 Ezequiel Alves. All rights reserved.
 */

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cassert>
#include <memory>
#include <fstream>
#include <chrono>
#include <random>
#include <iomanip>
#include <unistd.h>
#include "Valu.h"
#include "verilated.h"

#ifndef VM_COVERAGE
#define VM_COVERAGE 0
#endif

#if VM_COVERAGE
#include "verilated_cov.h"
#endif

enum AluOp : uint8_t {
    OP_ADD = 0x0,
    OP_SUB = 0x1,
    OP_AND = 0x2,
    OP_OR  = 0x3,
    OP_XOR = 0x4,
    OP_SLL = 0x5,
    OP_SRL = 0x6,
    OP_SRA = 0x7,
    OP_MUL = 0x8
};

static uint64_t g_sim_time = 0;

inline bool add_overflow(uint32_t a, uint32_t b) {
    uint32_t r = a + b;
    bool a_sign = (a >> 31) & 1;
    bool b_sign = (b >> 31) & 1;
    bool r_sign = (r >> 31) & 1;
    return (a_sign == b_sign) && (r_sign != a_sign);
}

inline bool sub_overflow(uint32_t a, uint32_t b) {
    uint32_t r = a - b;
    bool a_sign = (a >> 31) & 1;
    bool b_sign = (b >> 31) & 1;
    bool r_sign = (r >> 31) & 1;
    return (a_sign != b_sign) && (r_sign != a_sign);
}

void tick(Valu* top) {
    top->clk_i = 0;
    top->eval();
    g_sim_time += 5;

    top->clk_i = 1;
    top->eval();
    g_sim_time += 5;
}

void reset_dut(Valu* top) {
    top->rst_ni = 0;
    top->valid_i = 0;
    top->op_i = 0;
    top->a_i = 0;
    top->b_i = 0;
    for (int i = 0; i < 5; ++i) {
        tick(top);
    }
    top->rst_ni = 1;
    tick(top);
}

void execute_op(Valu* top, uint8_t op, uint32_t a, uint32_t b,
                uint32_t expected_result, bool expected_zero = false,
                bool check_overflow = false, bool expected_overflow = false) {
    top->valid_i = 1;
    top->op_i = op;
    top->a_i = a;
    top->b_i = b;

    tick(top);
    top->valid_i = 0;

    if (top->result_o != expected_result) {
        std::cerr << "Assertion FAILED: op=" << (int)op
                  << " a=0x" << std::hex << a << " b=0x" << b
                  << " Expected=0x" << expected_result
                  << " Got=0x" << top->result_o << std::dec << std::endl;
        std::exit(1);
    }
    if (top->zero_o != (expected_zero ? 1 : 0)) {
        std::cerr << "Assertion FAILED: Zero flag mismatch. Expected="
                  << expected_zero << " Got=" << (int)top->zero_o << std::endl;
        std::exit(1);
    }
    if (check_overflow && top->overflow_o != (expected_overflow ? 1 : 0)) {
        std::cerr << "Assertion FAILED: Overflow flag mismatch. Expected="
                  << expected_overflow << " Got=" << (int)top->overflow_o << std::endl;
        std::exit(1);
    }
}

// Stress test loops to exercise randomized coverage and assertions at full native speed
bool run_stress_test(Valu* top, std::string_view suite, uint64_t target_cycles, double target_duration_sec,
                     uint32_t seed, uint64_t& cycles_checked, uint64_t& assertions_checked) {
    reset_dut(top);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> dist32(0, 0xFFFFFFFF);
    std::uniform_int_distribution<uint32_t> dist_shift(0, 31);
    std::uniform_int_distribution<uint32_t> dist_op(0, 8);

    auto start_tp = std::chrono::steady_clock::now();
    cycles_checked = 0;
    assertions_checked = 0;

    std::cout << ">>> Launching verification suite: [" << suite << "] (seed: " << seed
              << ", target cycles: " << target_cycles;
    if (target_duration_sec > 0.0) {
        std::cout << ", target duration: " << target_duration_sec << "s";
    }
    std::cout << ")..." << std::endl;

    static const uint32_t boundaries[] = {
        0x00000000, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000,
        0x00000001, 0xFFFFFFFE, 0x55555555, 0xAAAAAAAA,
        0x0000FFFF, 0xFFFF0000, 0x00FF00FF, 0xFF00FF00
    };
    constexpr size_t num_boundaries = sizeof(boundaries) / sizeof(boundaries[0]);

    while (true) {
        // Run a batch of operations
        for (int i = 0; i < 1000; ++i) {
            uint32_t a = dist32(rng);
            uint32_t b = dist32(rng);

            if (suite == "arithmetic") {
                execute_op(top, OP_ADD, a, b, a + b, (a + b) == 0, true, add_overflow(a, b));
                execute_op(top, OP_SUB, a, b, a - b, (a - b) == 0, true, sub_overflow(a, b));
                assertions_checked += 2;
            } else if (suite == "logic") {
                execute_op(top, OP_AND, a, b, a & b, (a & b) == 0);
                execute_op(top, OP_OR,  a, b, a | b, (a | b) == 0);
                execute_op(top, OP_XOR, a, b, a ^ b, (a ^ b) == 0);
                assertions_checked += 3;
            } else if (suite == "shift") {
                uint32_t shamt = dist_shift(rng);
                execute_op(top, OP_SLL, a, shamt, a << shamt, (a << shamt) == 0);
                execute_op(top, OP_SRL, a, shamt, a >> shamt, (a >> shamt) == 0);
                int32_t s_a = static_cast<int32_t>(a);
                int32_t s_res = s_a >> shamt;
                execute_op(top, OP_SRA, a, shamt, static_cast<uint32_t>(s_res), s_res == 0);
                assertions_checked += 3;
            } else if (suite == "mult") {
                execute_op(top, OP_MUL, a, b, a * b, (a * b) == 0);
                assertions_checked += 1;
            } else if (suite == "overflow") {
                // Test positive overflow
                uint32_t p1 = 0x40000000 | (dist32(rng) & 0x3FFFFFFF);
                uint32_t p2 = 0x40000000 | (dist32(rng) & 0x3FFFFFFF);
                execute_op(top, OP_ADD, p1, p2, p1 + p2, false, true, true);

                // Test negative overflow
                uint32_t n1 = 0x80000000 | (dist32(rng) & 0x3FFFFFFF);
                uint32_t n2 = 0x80000000 | (dist32(rng) & 0x3FFFFFFF);
                execute_op(top, OP_ADD, n1, n2, n1 + n2, false, true, true);

                // Test positive - negative overflow
                execute_op(top, OP_SUB, p1, n1, p1 - n1, false, true, true);

                // Normal addition
                uint32_t s1 = dist32(rng) & 0x0000FFFF;
                uint32_t s2 = dist32(rng) & 0x0000FFFF;
                execute_op(top, OP_ADD, s1, s2, s1 + s2, (s1 + s2) == 0, true, false);
                assertions_checked += 4;
            } else if (suite == "zero_flag") {
                execute_op(top, OP_SUB, a, a, 0, true);
                execute_op(top, OP_XOR, a, a, 0, true);
                execute_op(top, OP_AND, a, 0, 0, true);
                execute_op(top, OP_MUL, a, 0, 0, true);
                execute_op(top, OP_OR,  a | 1, 0, a | 1, false);
                assertions_checked += 5;
            } else if (suite == "boundary") {
                size_t idx_a = (cycles_checked + i) % num_boundaries;
                size_t idx_b = (cycles_checked + i + 1) % num_boundaries;
                uint32_t ba = boundaries[idx_a];
                uint32_t bb = boundaries[idx_b];
                uint8_t op = (cycles_checked + i) % 9;
                uint32_t exp = 0;
                bool ovf = false;
                switch (op) {
                    case OP_ADD: exp = ba + bb; ovf = add_overflow(ba, bb); break;
                    case OP_SUB: exp = ba - bb; ovf = sub_overflow(ba, bb); break;
                    case OP_AND: exp = ba & bb; break;
                    case OP_OR:  exp = ba | bb; break;
                    case OP_XOR: exp = ba ^ bb; break;
                    case OP_SLL: exp = ba << (bb & 0x1F); break;
                    case OP_SRL: exp = ba >> (bb & 0x1F); break;
                    case OP_SRA: exp = static_cast<uint32_t>(static_cast<int32_t>(ba) >> (bb & 0x1F)); break;
                    case OP_MUL: exp = ba * bb; break;
                }
                execute_op(top, op, ba, bb, exp, exp == 0, (op == OP_ADD || op == OP_SUB), ovf);
                assertions_checked += 1;
            } else if (suite == "fuzz") {
                uint8_t op = dist_op(rng);
                uint32_t exp = 0;
                bool ovf = false;
                switch (op) {
                    case OP_ADD: exp = a + b; ovf = add_overflow(a, b); break;
                    case OP_SUB: exp = a - b; ovf = sub_overflow(a, b); break;
                    case OP_AND: exp = a & b; break;
                    case OP_OR:  exp = a | b; break;
                    case OP_XOR: exp = a ^ b; break;
                    case OP_SLL: exp = a << (b & 0x1F); break;
                    case OP_SRL: exp = a >> (b & 0x1F); break;
                    case OP_SRA: exp = static_cast<uint32_t>(static_cast<int32_t>(a) >> (b & 0x1F)); break;
                    case OP_MUL: exp = a * b; break;
                }
                execute_op(top, op, a, b, exp, exp == 0, (op == OP_ADD || op == OP_SUB), ovf);
                assertions_checked += 1;
            } else if (suite == "comparison") {
                execute_op(top, OP_SUB, a, b, a - b, a == b, true, sub_overflow(a, b));
                bool expected_slt = (static_cast<int32_t>(a) < static_cast<int32_t>(b));
                bool hw_slt = (static_cast<int32_t>(top->result_o) < 0) ^ top->overflow_o;
                if (expected_slt != hw_slt) {
                    std::cerr << "Assertion FAILED: Signed SLT check failed! a=" << (int32_t)a
                              << " b=" << (int32_t)b << std::endl;
                    std::exit(1);
                }
                assertions_checked += 2;
            } else if (suite == "pipeline_stress") {
                uint8_t op = static_cast<uint8_t>(i % 9);
                uint32_t exp = 0;
                switch (op) {
                    case OP_ADD: exp = a + b; break;
                    case OP_SUB: exp = a - b; break;
                    case OP_AND: exp = a & b; break;
                    case OP_OR:  exp = a | b; break;
                    case OP_XOR: exp = a ^ b; break;
                    case OP_SLL: exp = a << (b & 0x1F); break;
                    case OP_SRL: exp = a >> (b & 0x1F); break;
                    case OP_SRA: exp = static_cast<uint32_t>(static_cast<int32_t>(a) >> (b & 0x1F)); break;
                    case OP_MUL: exp = a * b; break;
                }
                execute_op(top, op, a, b, exp, exp == 0);
                assertions_checked += 1;
            } else {
                // Default fallback
                execute_op(top, OP_ADD, a, b, a + b, (a + b) == 0);
                assertions_checked += 1;
            }
            cycles_checked++;
            if (target_cycles > 0 && cycles_checked >= target_cycles) {
                break;
            }
        }

        if (target_cycles > 0 && cycles_checked >= target_cycles) {
            break;
        }

        if (target_duration_sec > 0.0) {
            auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_tp).count();
            if (elapsed >= target_duration_sec) {
                break;
            }
        }
    }

    std::cout << ">>> Suite [" << suite << "] PASSED: " << cycles_checked << " cycles, "
              << assertions_checked << " assertions verified." << std::endl;
    return true;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    std::string test_name = "arithmetic";
    std::string coverage_file;
    uint64_t target_cycles = 100000; // Default 100,000 cycles per test suite
    double duration_sec = 0.0;       // By default purely cycle-bounded, zero sleep
    uint32_t seed = 1337;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--test=", 0) == 0) {
            test_name = arg.substr(7);
        } else if (arg.rfind("--coverage-file=", 0) == 0) {
            coverage_file = arg.substr(16);
        } else if (arg.rfind("--cycles=", 0) == 0) {
            target_cycles = std::stoull(arg.substr(9));
        } else if (arg.rfind("--duration-sec=", 0) == 0) {
            duration_sec = std::stod(arg.substr(15));
            if (arg.find("--cycles=") == std::string::npos) {
                target_cycles = 0; // If explicit duration is passed without cycles, use duration
            }
        } else if (arg.rfind("--seed=", 0) == 0) {
            seed = static_cast<uint32_t>(std::stoul(arg.substr(7)));
        }
    }

    auto start_time = std::chrono::system_clock::now();
    auto top = std::make_unique<Valu>();

    uint64_t cycles = 0;
    uint64_t assertions = 0;
    bool passed = run_stress_test(top.get(), test_name, target_cycles, duration_sec, seed, cycles, assertions);

    top->final();

#if VM_COVERAGE
    if (!coverage_file.empty()) {
        std::cout << ">>> Writing coverage data to: " << coverage_file << std::endl;
        VerilatedCov::write(coverage_file.c_str());
    }
#endif

    // Generate machine-readable YAML status in current working directory
    {
        std::ofstream yfs("test_metrics.yaml", std::ios::trunc);
        if (yfs) {
            yfs << "test_name: \"" << test_name << "\"\n"
                << "status: \"" << (passed ? "PASSED" : "FAILED") << "\"\n"
                << "seed: " << seed << "\n"
                << "cycles_simulated: " << cycles << "\n"
                << "assertions_verified: " << assertions << "\n"
                << "simulation_time_ns: " << g_sim_time << "\n"
                << "coverage_enabled: " << (VM_COVERAGE ? "true" : "false") << "\n";
#if VM_COVERAGE
            if (!coverage_file.empty()) {
                yfs << "coverage_file: \"" << coverage_file << "\"\n";
            }
#endif
        }
    }

    return passed ? 0 : 1;
}
