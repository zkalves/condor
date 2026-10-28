#!/usr/bin/env bash
#
# This file is part of Condor.
#
# Condor is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Copyright (C) 2026 Ezequiel Alves. All rights reserved.
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERILATOR_EXAMPLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RTL_FILE="$VERILATOR_EXAMPLE_DIR/rtl/alu.sv"
TB_FILE="$VERILATOR_EXAMPLE_DIR/tb/tb_alu.cpp"

# If VERILATOR_ROOT is set in environment to a path that triggers inconsistency warning, unset it
if [ -n "$VERILATOR_ROOT" ]; then
    if ! verilator --version >/dev/null 2>&1; then
        unset VERILATOR_ROOT
    fi
fi

VERILATOR_BIN="${CONDOR_VAR_VERILATOR_BIN:-verilator}"

if ! command -v "$VERILATOR_BIN" >/dev/null 2>&1; then
    for candidate in /opt/verilator/v*/bin/verilator /opt/verilator/current/bin/verilator /opt/verilator/default/bin/verilator; do
        if [ -x "$candidate" ]; then
            VERILATOR_BIN="$candidate"
            export PATH="$(dirname "$candidate"):$PATH"
            break
        fi
    done
fi

if ! command -v "$VERILATOR_BIN" >/dev/null 2>&1; then
    echo "ERROR: '$VERILATOR_BIN' was not found in PATH." >&2
    echo "Please ensure Verilator is installed and available in PATH." >&2
    exit 127
fi

JOB_DIR="${CONDOR_JOB_DIR:-$PWD}"
JOB_DIR_ABS="$(cd "$JOB_DIR" && pwd)"
BIN_DIR="${JOB_DIR_ABS}/bin"
mkdir -p "$BIN_DIR"

echo "=================================================================="
echo " [BUILD] Verilator Cycle-Accurate SystemVerilog C++ Compilation"
echo "=================================================================="

echo "[BUILD Phase 1/4] Environment verification & toolchain discovery..."

COV_ENABLED=false
COV_FLAGS=""
if [ "${CONDOR_VAR_COVERAGE}" = "1" ]; then
    echo "  -> Verilator Coverage enabled (--coverage)"
    COV_FLAGS="--coverage -CFLAGS -DVM_COVERAGE=1"
    COV_ENABLED=true
else
    echo "  -> Optimized execution (Coverage disabled)"
fi

echo "[BUILD Phase 2/4] Performing SystemVerilog static analysis & lint checks..."
"$VERILATOR_BIN" --lint-only -Wall -Wno-fatal "$RTL_FILE"

echo "[BUILD Phase 3/4] Translating SystemVerilog AST to optimized C++ simulation classes..."
"$VERILATOR_BIN" --cc --exe \
    -Wall -Wno-fatal \
    $COV_FLAGS \
    -O3 \
    --top-module alu \
    -Mdir obj_dir \
    -o "$BIN_DIR/alu_sim" \
    "$RTL_FILE" "$TB_FILE"

echo "[BUILD Phase 4/4] Multi-threaded native compilation (g++ -O3)..."
make -C obj_dir -f Valu.mk -j 4

# Machine-readable build status YAML
cat <<EOF > build_status.yaml
phase: "build_rtl"
status: "PASSED"
target_binary: "$BIN_DIR/alu_sim"
coverage_enabled: $COV_ENABLED
rtl_file: "$RTL_FILE"
tb_file: "$TB_FILE"
EOF

echo "[BUILD] Build successfully finished! Binary placed at: $BIN_DIR/alu_sim"
