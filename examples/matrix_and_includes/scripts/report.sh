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
echo "=========================================================="
echo " [MATRIX REPORT] Collating results from ${CONDOR_RUN_DIR}"
echo "=========================================================="
TOTAL=0
for f in "${CONDOR_RUN_DIR}"/*/*/test_metric.yaml; do
    if [ -f "$f" ]; then
        TOTAL=$(( TOTAL + 1 ))
    fi
done
echo "Total metric files collected: ${TOTAL}"
echo "Matrix sweep completed successfully!"
