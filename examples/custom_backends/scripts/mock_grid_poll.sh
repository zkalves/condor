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
# Simulates grid job status query (qstat / squeue)
GRID_ID="$1"
STATE_DIR="/tmp/condor_mock_grid"
PID_FILE="${STATE_DIR}/${GRID_ID}.pid"

if [ ! -f "$PID_FILE" ]; then
    echo "COMPLETED"
    exit 0
fi

PID=$(cat "$PID_FILE")
if kill -0 "$PID" 2>/dev/null; then
    echo "RUNNING"
else
    rm -f "$PID_FILE"
    echo "COMPLETED"
fi
