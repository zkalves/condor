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
# Simulates grid job submission (sbatch / qsub / bsub)
SCRIPT="$1"
JOB_ID="$2"

GRID_ID="GRID_$(date +%s%N | cut -b1-12)"
bash "$SCRIPT" > "${SCRIPT}.grid.log" 2>&1 &
PID=$!

STATE_DIR="/tmp/condor_mock_grid"
mkdir -p "$STATE_DIR"
echo "$PID" > "${STATE_DIR}/${GRID_ID}.pid"

echo "$GRID_ID"
