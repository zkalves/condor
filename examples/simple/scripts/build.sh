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
ARCH="${CONDOR_VAR_ARCH:-x86_64}"
BUILD_TYPE="${CONDOR_VAR_BUILD_TYPE:-Release}"
echo "[BUILD] Compiling core for ${ARCH} in ${BUILD_TYPE} mode..."
mkdir -p bin
echo "app_binary_${ARCH}_${BUILD_TYPE}" > bin/app
mkdir -p plugins
echo "plugin_core" > plugins/core.so
echo "[BUILD] Compilation finished successfully."
