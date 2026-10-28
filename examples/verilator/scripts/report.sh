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

JOB_DIR="${CONDOR_JOB_DIR:-$PWD}"
mkdir -p "$JOB_DIR"

REPORT_TXT="${JOB_DIR}/test_report.txt"
REPORT_MD="${JOB_DIR}/test_report.md"
SUMMARY_YAML="${JOB_DIR}/pipeline_summary.yaml"
TERMINAL_TXT="${JOB_DIR}/terminal_report.txt"

TERM_COLS=$(tput cols 2>/dev/null || echo 80)
if [ -z "$TERM_COLS" ] || [ "$TERM_COLS" -lt 60 ]; then
    TERM_COLS=74
fi
DIVIDER=$(printf '%*s' "$TERM_COLS" '' | tr ' ' '=')
DIVIDER_DASH=$(printf '%*s' "$TERM_COLS" '' | tr ' ' '-')

COV_MODE="DISABLED"
if [ "${CONDOR_VAR_COVERAGE}" = "1" ]; then
    COV_MODE="ENABLED"
fi

NOW=$(date '+%Y-%m-%d %H:%M:%S')

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
if [ -n "$VERILATOR_ROOT" ]; then
    if ! "$VERILATOR_BIN" --version >/dev/null 2>&1; then
        unset VERILATOR_ROOT
    fi
fi
VERILATOR_VER=$("$VERILATOR_BIN" --version 2>/dev/null || echo "Verilator")

# ------------------------------------------------------------------------------
# 1. Parse Status and Metrics Across All Jobs
# ------------------------------------------------------------------------------
TOTAL=0
PASSED=0
FAILED=0

# Start writing pipeline_summary.yaml
cat <<EOF > "$SUMMARY_YAML"
pipeline_summary:
  timestamp: "$NOW"
  run_directory: "${CONDOR_RUN_DIR}"
  coverage_mode: "$COV_MODE"
  target_design: "ALU (32-bit SystemVerilog)"
  simulator: "$VERILATOR_VER"
  stages:
EOF

STAGES_BASE="${CONDOR_RUN_DIR}/stages"
if [ ! -d "$STAGES_BASE" ]; then
    STAGES_BASE="${CONDOR_RUN_DIR}"
fi

for sdir in "$STAGES_BASE"/*; do
    if [ -d "$sdir" ]; then
        STAGE_NAME=$(basename "$sdir")
        if [ "$STAGE_NAME" = "report" ] || [ "$STAGE_NAME" = "bin" ]; then
            continue
        fi

        STAGE_JOBS=0
        for jdir in "$sdir"/*; do
            if [ -d "$jdir" ] && [ -f "$jdir/job_status.yaml" ]; then
                STAGE_JOBS=$((STAGE_JOBS + 1))
            fi
        done

        if [ "$STAGE_JOBS" -eq 0 ]; then
            continue
        fi

        echo "    - name: \"$STAGE_NAME\"" >> "$SUMMARY_YAML"
        echo "      jobs:" >> "$SUMMARY_YAML"

        for jdir in "$sdir"/*; do
            if [ -d "$jdir" ] && [ -f "$jdir/job_status.yaml" ]; then
                TOTAL=$((TOTAL + 1))
                JSTATUS=$(grep "^status:" "$jdir/job_status.yaml" | head -n 1 | awk '{print $2}' | tr -d '"')
                if [ "$JSTATUS" = "PASSED" ]; then
                    PASSED=$((PASSED + 1))
                else
                    FAILED=$((FAILED + 1))
                fi

                # Indent and append job_status.yaml
                echo "        - $(head -n 1 "$jdir/job_status.yaml")" >> "$SUMMARY_YAML"
                tail -n +2 "$jdir/job_status.yaml" | sed 's/^/          /' >> "$SUMMARY_YAML"

                # If test_metrics.yaml exists, nest it
                if [ -f "$jdir/test_metrics.yaml" ]; then
                    echo "          test_metrics:" >> "$SUMMARY_YAML"
                    sed 's/^/            /' "$jdir/test_metrics.yaml" >> "$SUMMARY_YAML"
                fi
            fi
        done
    fi
done

# Append overall verdict to pipeline_summary.yaml
cat <<EOF >> "$SUMMARY_YAML"
  results:
    total_jobs: $TOTAL
    passed_jobs: $PASSED
    failed_jobs: $FAILED
    verdict: "$([ "$FAILED" -eq 0 ] && echo "PASSED" || echo "FAILED")"
EOF

# ------------------------------------------------------------------------------
# 2. Plain-Text Report
# ------------------------------------------------------------------------------
{
    echo "$DIVIDER"
    echo "                  CONDOR VERILATOR TEST STATUS REPORT                     "
    echo "$DIVIDER"
    echo "Generated At:    $NOW"
    echo "Run Directory:   ${CONDOR_RUN_DIR}"
    echo "Summary YAML:    $SUMMARY_YAML"
    echo "Coverage Mode:   $COV_MODE"
    echo "Simulator:       $VERILATOR_VER"
    echo "$DIVIDER"
    echo ""
    printf "%-8s %-18s %-22s %-16s %-10s %-8s\n" "JOB_ID" "STAGE" "TEST / JOB NAME" "STATUS" "EXIT_CODE" "TIME(s)"
    echo "$DIVIDER_DASH"

    for status_file in $(find "${CONDOR_RUN_DIR}" -name "job_status.yaml" | sort); do
        JOB_ID=$(grep '^job_id:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
        JOB_NAME=$(grep '^job_name:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
        STAGE_NAME=$(grep '^stage_name:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
        STATUS=$(grep '^status:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
        EXIT_CODE=$(grep '^exit_code:' "$status_file" | head -n 1 | awk '{print $2}')
        ELAPSED=$(grep '^elapsed_sec:' "$status_file" | head -n 1 | awk '{print $2}')

        printf "%-8s %-18s %-22s %-16s %-10s %-8s\n" \
               "$JOB_ID" "$STAGE_NAME" "$JOB_NAME" "$STATUS" "$EXIT_CODE" "$ELAPSED"
    done

    echo "$DIVIDER_DASH"
    echo ""
    echo "Detailed Test Metrics (from test_metrics.yaml):"
    for tm in $(find "${CONDOR_RUN_DIR}" -name "test_metrics.yaml" | sort); do
        TNAME=$(grep '^test_name:' "$tm" | head -n 1 | awk '{print $2}' | tr -d '"')
        CYCLES=$(grep '^cycles_simulated:' "$tm" | head -n 1 | awk '{print $2}')
        ASSERTS=$(grep '^assertions_verified:' "$tm" | head -n 1 | awk '{print $2}')
        printf "  * %-20s: %'d cycles verified, %'d assertions passed\n" "$TNAME" "$CYCLES" "$ASSERTS"
    done

    echo ""
    echo "Test Verification Verdict:"
    if [ "$FAILED" -eq 0 ]; then
        echo "  STATUS: ALL TESTS PASSED (Total: $TOTAL, Passed: $PASSED, Failed: 0)"
    else
        echo "  STATUS: VERIFICATION FAILED (Total: $TOTAL, Passed: $PASSED, Failed: $FAILED)"
    fi

    if [ "$COV_MODE" = "ENABLED" ]; then
        echo ""
        echo "Coverage Summary:"
        COV_INFO=$(find "${CONDOR_RUN_DIR}" -name "coverage.info" | head -n 1 || true)
        if [ -f "$COV_INFO" ]; then
            LINES_TOTAL=$(grep -c "^DA:" "$COV_INFO" || true)
            LINES_HIT=$(grep -c "^DA:[0-9]*,[1-9]" "$COV_INFO" || true)
            if [ "$LINES_TOTAL" -gt 0 ]; then
                PCT=$(( 100 * LINES_HIT / LINES_TOTAL ))
                echo "  Line Coverage:        ${PCT}%"
                echo "  Covered Lines:        ${LINES_HIT} / ${LINES_TOTAL}"
                echo "  Coverage Database:    $COV_INFO"
            fi
        fi
    fi
    echo "=========================================================================="
} > "$REPORT_TXT"

# ------------------------------------------------------------------------------
# 3. Markdown Report
# ------------------------------------------------------------------------------
{
    echo "# Condor Verilator Test Status Report"
    echo ""
    echo "- **Run Directory**: \`${CONDOR_RUN_DIR}\`"
    echo "- **Machine-Readable YAML**: \`${SUMMARY_YAML}\`"
    echo "- **Timestamp**: \`${NOW}\`"
    echo "- **Coverage Mode**: \`${COV_MODE}\`"
    echo "- **Verdict**: **$([ "$FAILED" -eq 0 ] && echo "ALL TESTS PASSED" || echo "TESTS FAILED")**"
    echo ""
    echo "### Test Execution Results"
    echo ""
    echo "| Job ID | Stage | Test / Job Name | Status | Exit Code | Runtime | Verified Cycles |"
    echo "| :--- | :--- | :--- | :--- | :--- | :--- | :--- |"

    for status_file in $(find "${CONDOR_RUN_DIR}" -name "job_status.yaml" | sort); do
        JOB_ID=$(grep '^job_id:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
        JOB_NAME=$(grep '^job_name:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
        STAGE_NAME=$(grep '^stage_name:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
        STATUS=$(grep '^status:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
        EXIT_CODE=$(grep '^exit_code:' "$status_file" | head -n 1 | awk '{print $2}')
        ELAPSED=$(grep '^elapsed_sec:' "$status_file" | head -n 1 | awk '{print $2}')

        CYCLES_STR="-"
        JDIR=$(dirname "$status_file")
        if [ -f "$JDIR/test_metrics.yaml" ]; then
            CYCLES_STR=$(grep '^cycles_simulated:' "$JDIR/test_metrics.yaml" | head -n 1 | awk '{print $2}')
        fi

        BADGE="✅ PASSED"
        if [ "$STATUS" != "PASSED" ]; then
            BADGE="❌ FAILED"
        fi
        echo "| \`$JOB_ID\` | \`$STAGE_NAME\` | **$JOB_NAME** | $BADGE | \`$EXIT_CODE\` | ${ELAPSED}s | $CYCLES_STR |"
    done

    if [ "$COV_MODE" = "ENABLED" ]; then
        echo ""
        echo "### Coverage Metrics"
        COV_INFO=$(find "${CONDOR_RUN_DIR}" -name "coverage.info" | head -n 1 || true)
        if [ -f "$COV_INFO" ]; then
            LINES_TOTAL=$(grep -c "^DA:" "$COV_INFO" || true)
            LINES_HIT=$(grep -c "^DA:[0-9]*,[1-9]" "$COV_INFO" || true)
            if [ "$LINES_TOTAL" -gt 0 ]; then
                PCT=$(( 100 * LINES_HIT / LINES_TOTAL ))
                echo "| Metric | Value |"
                echo "| :--- | :--- |"
                echo "| **Line Coverage** | **${PCT}%** |"
                echo "| **Lines Covered / Total** | ${LINES_HIT} / ${LINES_TOTAL} |"
                echo "| **Coverage Database** | \`${COV_INFO}\` |"
            fi
        fi
    fi
} > "$REPORT_MD"

MODE="${CONDOR_OUTPUT_MODE:-${CONDOR_VAR_OUTPUT_MODE:-${CONDOR_VAR_CONDOR_OUTPUT_MODE:-all}}}"

case "$MODE" in
    failures)
        {
            echo "$DIVIDER"
            echo "                  CONDOR VERILATOR TEST STATUS REPORT                     "
            echo "$DIVIDER"
            echo "Run Directory:   ${CONDOR_RUN_DIR}"
            echo "Summary YAML:    $SUMMARY_YAML"
            echo "Coverage Mode:   $COV_MODE"
            echo "Simulator:       $VERILATOR_VER"
            echo "$DIVIDER"
            if [ "$FAILED" -eq 0 ]; then
                echo "Status:          ALL TESTS PASSED (0 failures across $TOTAL jobs)"
            else
                echo "Status:          VERIFICATION FAILED ($FAILED of $TOTAL jobs failed)"
                echo ""
                printf "%-8s %-18s %-22s %-16s %-10s %-8s\n" "JOB_ID" "STAGE" "TEST / JOB NAME" "STATUS" "EXIT_CODE" "TIME(s)"
                echo "$DIVIDER_DASH"
                for status_file in $(find "${CONDOR_RUN_DIR}" -name "job_status.yaml" | sort); do
                    STATUS=$(grep '^status:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
                    if [ "$STATUS" != "PASSED" ]; then
                        JOB_ID=$(grep '^job_id:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
                        JOB_NAME=$(grep '^job_name:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
                        STAGE_NAME=$(grep '^stage_name:' "$status_file" | head -n 1 | awk '{print $2}' | tr -d '"')
                        EXIT_CODE=$(grep '^exit_code:' "$status_file" | head -n 1 | awk '{print $2}')
                        ELAPSED=$(grep '^elapsed_sec:' "$status_file" | head -n 1 | awk '{print $2}')
                        printf "%-8s %-18s %-22s %-16s %-10s %-8s\n" \
                               "$JOB_ID" "$STAGE_NAME" "$JOB_NAME" "$STATUS" "$EXIT_CODE" "$ELAPSED"
                    fi
                done
            fi
            echo "$DIVIDER"
        } > "$TERMINAL_TXT"
        ;;
    summary)
        {
            echo "$DIVIDER"
            echo "              CONDOR VERILATOR TEST STATUS REPORT (SUMMARY)               "
            echo "$DIVIDER"
            echo "Run Directory:   ${CONDOR_RUN_DIR}"
            echo "Summary YAML:    $SUMMARY_YAML"
            echo "Coverage Mode:   $COV_MODE"
            echo "Simulator:       $VERILATOR_VER"
            echo "Total Jobs:      $TOTAL"
            echo "Passed Jobs:     $PASSED"
            echo "Failed Jobs:     $FAILED"
            echo "Verdict:         $([ "$FAILED" -eq 0 ] && echo "ALL TESTS PASSED" || echo "VERIFICATION FAILED")"
            if [ "$COV_MODE" = "ENABLED" ]; then
                COV_INFO=$(find "${CONDOR_RUN_DIR}" -name "coverage.info" | head -n 1 || true)
                if [ -f "$COV_INFO" ]; then
                    LINES_TOTAL=$(grep -c "^DA:" "$COV_INFO" || true)
                    LINES_HIT=$(grep -c "^DA:[0-9]*,[1-9]" "$COV_INFO" || true)
                    if [ "$LINES_TOTAL" -gt 0 ]; then
                        PCT=$(( 100 * LINES_HIT / LINES_TOTAL ))
                        echo "Line Coverage:   ${PCT}% (${LINES_HIT} / ${LINES_TOTAL} lines)"
                    fi
                fi
            fi
            echo "$DIVIDER"
        } > "$TERMINAL_TXT"
        ;;
    all|*)
        cp "$REPORT_TXT" "$TERMINAL_TXT"
        ;;
esac

cat "$TERMINAL_TXT"

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
exit 0
