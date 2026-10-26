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
echo "[PACKAGE] Packaging artifacts from ${BUILD_DIR}..."
mkdir -p dist
tar -czf dist/package.tar.gz -C "${BUILD_DIR}" bin plugins
echo "[PACKAGE] Package created at dist/package.tar.gz"
