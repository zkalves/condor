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

VERILATOR_COV="${CONDOR_VAR_VERILATOR_COV:-verilator_coverage}"

if ! command -v "$VERILATOR_COV" >/dev/null 2>&1; then
    for candidate in /opt/verilator/v*/bin/verilator_coverage /opt/verilator/current/bin/verilator_coverage /opt/verilator/default/bin/verilator_coverage; do
        if [ -x "$candidate" ]; then
            VERILATOR_COV="$candidate"
            export PATH="$(dirname "$candidate"):$PATH"
            break
        fi
    done
fi

if ! command -v "$VERILATOR_COV" >/dev/null 2>&1; then
    echo "ERROR: '$VERILATOR_COV' was not found in PATH." >&2
    echo "Please ensure verilator_coverage is installed and available in PATH." >&2
    exit 127
fi

echo "=================================================================="
echo " [COVERAGE] Merging and Analyzing Verilator Test Coverage Data"
echo "=================================================================="

COV_FILES=()
for f in "${CONDOR_RUN_DIR}"/stages/sim_tests/*/coverage.dat "${CONDOR_RUN_DIR}"/sim_tests/*/coverage.dat; do
    if [ -f "$f" ]; then
        COV_FILES+=("$f")
        echo "Found coverage data: $f"
    fi
done

if [ ${#COV_FILES[@]} -eq 0 ]; then
    echo "[COVERAGE] Warning: No coverage.dat files found in ${CONDOR_RUN_DIR}/sim_tests/"
    exit 0
fi

mkdir -p annotated/
echo "[COVERAGE] Processing ${#COV_FILES[@]} coverage databases..."
"$VERILATOR_COV" --annotate annotated/ --write-info coverage.info "${COV_FILES[@]}"

LINES_TOTAL=0
LINES_HIT=0
PCT=0

if [ -f "coverage.info" ]; then
    LINES_TOTAL=$(grep -c "^DA:" coverage.info || true)
    LINES_HIT=$(grep -c "^DA:[0-9]*,[1-9]" coverage.info || true)
    if [ "$LINES_TOTAL" -gt 0 ]; then
        PCT=$(( 100 * LINES_HIT / LINES_TOTAL ))
        echo "  Total Instrumented Lines: $LINES_TOTAL"
        echo "  Covered Lines:            $LINES_HIT"
        echo "  Line Coverage:            ${PCT}%"
    fi
fi

# Machine-readable coverage status YAML
cat <<EOF > coverage_status.yaml
phase: "coverage_analysis"
status: "PASSED"
databases_merged: ${#COV_FILES[@]}
line_coverage_percent: $PCT
instrumented_lines: $LINES_TOTAL
covered_lines: $LINES_HIT
coverage_info: "${CONDOR_JOB_DIR}/coverage.info"
annotated_directory: "${CONDOR_JOB_DIR}/annotated"
EOF

echo "[COVERAGE] Annotated source outputs written to: ${CONDOR_JOB_DIR}/annotated/"
echo "=================================================================="
