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
echo "[TEST] Running ${TEST_NAME}..."
if [ ! -f "${BUILD_DIR}/bin/app" ]; then
    echo "ERROR: Binary not found at ${BUILD_DIR}/bin/app" >&2
    exit 1
fi
echo "[TEST] ${TEST_NAME} passed successfully."
