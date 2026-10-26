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
TASK="${CONDOR_JOB_NAME:-task}"
echo "[WORKLOAD] Executing ${TASK} on grid backend..."
sleep 0.2
echo "Result computed for ${TASK}"
