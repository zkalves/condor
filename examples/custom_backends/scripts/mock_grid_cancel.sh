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
# Simulates grid job cancellation (scancel / qdel)
GRID_ID="$1"
STATE_DIR="/tmp/condor_mock_grid"
PID_FILE="${STATE_DIR}/${GRID_ID}.pid"

if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE")
    kill -9 "$PID" 2>/dev/null || true
    rm -f "$PID_FILE"
fi
echo "CANCELLED"
