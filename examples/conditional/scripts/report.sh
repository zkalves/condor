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
echo "[REPORT] Generating conditional pipeline summary..."
cat <<EOF > pipeline_summary.yaml
pipeline: "conditional"
fast_mode: "${CONDOR_VAR_FAST_MODE}"
coverage_mode: "${CONDOR_VAR_COVERAGE}"
status: "COMPLETED"
EOF
echo "[REPORT] Summary report written to pipeline_summary.yaml"
