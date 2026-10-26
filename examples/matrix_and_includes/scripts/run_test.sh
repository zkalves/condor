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

TEST_NAME="${CONDOR_VAR_TEST_NAME:-${CONDOR_JOB_NAME:-test}}"
SEED="${CONDOR_VAR_SEED:-0}"
INDEX="${CONDOR_VAR_INDEX:-1}"
ITERATIONS="${CONDOR_VAR_ITERATIONS:-1000}"

echo "[TEST] Executing: ${TEST_NAME} (Seed: ${SEED}, Index: ${INDEX}, Iterations: ${ITERATIONS})"
echo "Working Directory: ${CONDOR_JOB_DIR} (Relative Run Dir: ${CONDOR_RUN_DIR})"

# Generate test metric file
cat <<EOF > test_metric.yaml
test_name: "${TEST_NAME}"
seed: ${SEED}
index: ${INDEX}
status: "PASSED"
assertions: $(( ITERATIONS * 2 ))
EOF

echo "[TEST] Finished ${TEST_NAME} successfully."
