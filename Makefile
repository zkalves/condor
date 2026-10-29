# ==============================================================================
# Condor Makefile - Pipeline & Build Flow Orchestration
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Copyright (C) 2026 Ezequiel Alves. All rights reserved.
# ==============================================================================

SHELL        := /bin/bash
BUILD_DIR    ?= build
BUILD_TYPE   ?= Release
JOBS         ?= $(shell nproc 2>/dev/null || echo 4)
OUTPUT_MODE  ?= all
CONDOR_BIN   := $(BUILD_DIR)/condor
TEST_BIN     := $(BUILD_DIR)/condor_tests
HUB_BIN      := $(BUILD_DIR)/condor-hub

HUB_PORT     ?= 8080
HUB_HOST     ?= 0.0.0.0
HUB_TOKEN    ?=
HUB_WORKERS  ?= 4
HUB_ARTIFACTS_DIR ?= ./hub_artifacts

PREFIX       ?= /usr/local
DESTDIR      ?=

# Without a target, default to showing the help menu with all available targets
.DEFAULT_GOAL := help

.PHONY: help all build build-offline build-custom download-deps vendor-deps test test-verbose \
        build-server build-hub run-server run-hub server hub run-remote \
        run-verilator-remote run-verilator-coverage-remote \
        dry-run run-simple run-conditional run-coverage run-ci \
        run-verilator run-verilator-coverage \
        docs docs-doxygen docs-serve clean-docs \
        install uninstall package-deb package-rpm package-tar package packages \
        clean clean-runs clean-all

# ------------------------------------------------------------------------------
# Default Target: Help Menu
# ------------------------------------------------------------------------------
help: ## Show this help message with all available targets
	@echo "========================================================================"
	@echo " Condor Orchestrator - Available Flows & Targets"
	@echo "========================================================================"
	@echo "Usage: make [target] [VARIABLE=value]"
	@echo ""
	@echo "Build Flows:"
	@echo "  build                Build Condor (auto-detects local third_party, system, or FetchContent)"
	@echo "  build-offline        Build using local third_party/ (offline flow, zero network)"
	@echo "  build-custom         Build using FetchContent/system packages (custom/online flow)"
	@echo "  download-deps        Fetch dependencies into third_party/ for offline development"
	@echo "  all                  Build Condor executable"
	@echo ""
	@echo "Test Flows:"
	@echo "  test                 Run test suite via CTest"
	@echo "  test-verbose         Run tests directly with full individual test output"
	@echo ""
	@echo "Server / Hub Flows:"
	@echo "  build-server         Build Condor Hub server (condor-hub, alias: build-hub)"
	@echo "  run-server           Start Condor Hub server (http://0.0.0.0:8080, aliases: server, run-hub, hub)"
	@echo "  run-remote           Run example pipeline connected to Condor Hub (ws://127.0.0.1:8080/api/v1/ws)"
	@echo "  run-verilator-remote Run Verilator ALU pipeline reporting to Condor Hub"
	@echo "  run-verilator-coverage-remote Run Verilator ALU pipeline with coverage reporting to Condor Hub"
	@echo ""
	@echo "Documentation Flows:"
	@echo "  docs                 Generate HTML documentation portal"
	@echo "  docs-doxygen         Generate Doxygen C++ API & Markdown documentation portal (_site/)"
	@echo "  docs-serve           Serve documentation portal on http://127.0.0.1:8000"
	@echo ""
	@echo "Pipeline Execution Flows:"
	@echo "  dry-run              Validate YAML syntax, DAG cycles, and print execution plan"
	@echo "  run-simple           Run basic build/test/package pipeline (examples/simple/scripts/pipeline.yaml)"
	@echo "  run-conditional      Run conditional pipeline with default vars (coverage=0, fast_mode=1)"
	@echo "  run-coverage         Run conditional pipeline with coverage enabled (--define-variable coverage=1)"
	@echo "  run-matrix           Run matrix replication, parameter sweeps, and includes pipeline"
	@echo "  run-failures         Run error handling, failure policies, and always-run hooks pipeline"
	@echo "  run-backends         Run custom template grid backend pipeline (Slurm/SGE simulation)"
	@echo "  run-verilator        Run Verilator ALU build & tests WITHOUT coverage"
	@echo "  run-verilator-coverage Run Verilator ALU build & tests WITH coverage (--define-variable coverage=1)"
	@echo "  run-ci               Run pipeline in CI mode (--no-interactive, 2s status dump interval)"
	@echo ""
	@echo "Installation & Packaging Flows:"
	@echo "  install              Install condor binary, examples, and documentation to PREFIX (current: $(PREFIX))"
	@echo "  uninstall            Uninstall files installed by previous make install"
	@echo "  package-deb          Build Debian (.deb) package"
	@echo "  package-rpm          Build Enterprise Linux RPM (.rpm) package"
	@echo "  package-tar          Build standalone portable tarball (.tar.gz) package"
	@echo "  package              Build all distribution packages (DEB, RPM, TGZ)"
	@echo ""
	@echo "Cleaning Flows:"
	@echo "  clean                Remove build directory ($(BUILD_DIR)/)"
	@echo "  clean-runs           Remove generated execution directories (condor_runs/)"
	@echo "  clean-all            Remove both build directory and all run outputs"
	@echo ""
	@echo "Configuration Variables:"
	@echo "  BUILD_DIR            Build directory path (current: $(BUILD_DIR))"
	@echo "  BUILD_TYPE           CMake build type: Release, Debug, RelWithDebInfo (current: $(BUILD_TYPE))"
	@echo "  JOBS                 Parallel compilation jobs (current: $(JOBS))"
	@echo "  OUTPUT_MODE          Terminal output mode: all, failures, summary (current: $(OUTPUT_MODE))"
	@echo "  HUB_PORT             Condor Hub listening port (current: $(HUB_PORT))"
	@echo "  HUB_HOST             Condor Hub bind address (current: $(HUB_HOST))"
	@echo "  HUB_TOKEN            Condor Hub authentication token (optional)"
	@echo "  HUB_WORKERS          Condor Hub worker thread count (current: $(HUB_WORKERS))"
	@echo "  HUB_ARTIFACTS_DIR    Directory for storing client HTML reports (current: $(HUB_ARTIFACTS_DIR))"
	@echo "========================================================================"

# ------------------------------------------------------------------------------
# Build Flows
# ------------------------------------------------------------------------------
all: build ## Build Condor executable

build: ## Build Condor (auto-detects local third_party, system packages, or FetchContent)
	@echo "==> Building Condor..."
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	@cmake --build $(BUILD_DIR) -j$(JOBS)
	@echo "==> Build complete: $(CONDOR_BIN)"

build-offline: ## Configure & build using local third_party/ (offline flow, zero network)
	@echo "==> Building Condor [Offline Flow]..."
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCONDOR_USE_LOCAL_DEPS=ON
	@cmake --build $(BUILD_DIR) -j$(JOBS)
	@echo "==> Build complete: $(CONDOR_BIN)"

build-custom: ## Configure & build using FetchContent/system packages (custom/online flow)
	@echo "==> Building Condor [Custom/Online Flow]..."
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCONDOR_USE_LOCAL_DEPS=OFF
	@cmake --build $(BUILD_DIR) -j$(JOBS)
	@echo "==> Build complete: $(CONDOR_BIN)"

download-deps vendor-deps: ## Fetch dependencies into third_party/ for offline development
	@mkdir -p third_party
	@if [ ! -d "third_party/CLI11" ]; then \
		echo "==> Fetching CLI11 (v2.6.0) into third_party/CLI11..."; \
		git clone --depth 1 --branch v2.6.0 https://github.com/CLIUtils/CLI11.git third_party/CLI11; \
	fi
	@if [ ! -d "third_party/yaml-cpp" ]; then \
		echo "==> Fetching yaml-cpp (0.9.0) into third_party/yaml-cpp..."; \
		git clone --depth 1 --branch yaml-cpp-0.9.0 https://github.com/jbeder/yaml-cpp.git third_party/yaml-cpp; \
	fi
	@if [ ! -d "third_party/nlohmann_json" ]; then \
		echo "==> Fetching nlohmann_json (v3.11.3) into third_party/nlohmann_json..."; \
		git clone --depth 1 --branch v3.11.3 https://github.com/nlohmann/json.git third_party/nlohmann_json; \
	fi
	@if [ ! -d "third_party/json-schema-validator" ]; then \
		echo "==> Fetching json-schema-validator (2.4.1) into third_party/json-schema-validator..."; \
		git clone --depth 1 https://github.com/pboettch/json-schema-validator.git third_party/json-schema-validator; \
	fi
	@echo "==> Dependencies successfully downloaded to third_party/"

$(CONDOR_BIN):
	@$(MAKE) build

$(TEST_BIN):
	@$(MAKE) build

$(HUB_BIN):
	@$(MAKE) build

# ------------------------------------------------------------------------------
# Test Flows
# ------------------------------------------------------------------------------
test: $(TEST_BIN) ## Run test suite via CTest
	@echo "==> Running CTest suite..."
	@ctest --test-dir $(BUILD_DIR) --output-on-failure

test-verbose: $(TEST_BIN) ## Run tests directly with full individual test output
	@echo "==> Running Condor test binary directly..."
	@$(TEST_BIN)

# ------------------------------------------------------------------------------
# Server / Hub Flows
# ------------------------------------------------------------------------------
build-server build-hub: ## Build Condor Hub server executable
	@echo "==> Building Condor Hub server..."
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	@cmake --build $(BUILD_DIR) --target condor-hub -j$(JOBS)
	@echo "==> Server build complete: $(HUB_BIN)"

run-server run-hub server hub: $(HUB_BIN) ## Start Condor Hub reporting and remote control server
	@echo "==> Starting Condor Hub on $(HUB_HOST):$(HUB_PORT) (artifacts: $(HUB_ARTIFACTS_DIR))..."
	@$(HUB_BIN) --host $(HUB_HOST) --port $(HUB_PORT) --workers $(HUB_WORKERS) --artifacts-dir $(HUB_ARTIFACTS_DIR) $(if $(HUB_TOKEN),--token $(HUB_TOKEN),)

run-remote: $(CONDOR_BIN) ## Run example pipeline reporting to Condor Hub
	@echo "==> Executing examples/simple/scripts/pipeline.yaml connected to Condor Hub..."
	@$(CONDOR_BIN) examples/simple/scripts/pipeline.yaml --server-url ws://127.0.0.1:$(HUB_PORT)/api/v1/ws $(if $(HUB_TOKEN),--server-token $(HUB_TOKEN),)

run-verilator-remote: $(CONDOR_BIN) ## Run Verilator ALU pipeline reporting to Condor Hub
	@echo "==> Executing Verilator ALU Pipeline connected to Condor Hub..."
	@$(CONDOR_BIN) examples/verilator/scripts/pipeline.yaml --output-mode $(OUTPUT_MODE) --server-url ws://127.0.0.1:$(HUB_PORT)/api/v1/ws $(if $(HUB_TOKEN),--server-token $(HUB_TOKEN),) --server-project "Verilator ALU" --server-tags "rtl,sim,verilator"

run-verilator-coverage-remote: $(CONDOR_BIN) ## Run Verilator ALU pipeline with coverage reporting to Condor Hub
	@echo "==> Executing Verilator ALU Pipeline [WITH COVERAGE] connected to Condor Hub..."
	@$(CONDOR_BIN) examples/verilator/scripts/pipeline.yaml --define-variable coverage=1 --output-mode $(OUTPUT_MODE) --server-url ws://127.0.0.1:$(HUB_PORT)/api/v1/ws $(if $(HUB_TOKEN),--server-token $(HUB_TOKEN),) --server-project "Verilator ALU" --server-tags "rtl,coverage,verilator"

# ------------------------------------------------------------------------------
# Pipeline Execution Flows
# ------------------------------------------------------------------------------
dry-run: $(CONDOR_BIN) ## Validate YAML syntax, DAG cycles, and print execution plan
	@echo "==> Validating simple/scripts/pipeline.yaml..."
	@$(CONDOR_BIN) --dry-run examples/simple/scripts/pipeline.yaml
	@echo ""
	@echo "==> Validating conditional/scripts/pipeline.yaml..."
	@$(CONDOR_BIN) --dry-run examples/conditional/scripts/pipeline.yaml

run-simple: $(CONDOR_BIN) ## Run basic build/test/package pipeline
	@echo "==> Executing examples/simple/scripts/pipeline.yaml..."
	@$(CONDOR_BIN) examples/simple/scripts/pipeline.yaml

run-conditional: $(CONDOR_BIN) ## Run conditional pipeline with default variables
	@echo "==> Executing examples/conditional/scripts/pipeline.yaml (default)..."
	@$(CONDOR_BIN) examples/conditional/scripts/pipeline.yaml

run-coverage: $(CONDOR_BIN) ## Run conditional pipeline with coverage enabled (coverage=1)
	@echo "==> Executing examples/conditional/scripts/pipeline.yaml with coverage=1..."
	@$(CONDOR_BIN) examples/conditional/scripts/pipeline.yaml --define-variable coverage=1

run-matrix: $(CONDOR_BIN) ## Run matrix replication, parameter sweeps, and includes pipeline
	@echo "==> Executing examples/matrix_and_includes/scripts/pipeline.yaml..."
	@$(CONDOR_BIN) examples/matrix_and_includes/scripts/pipeline.yaml

run-failures: $(CONDOR_BIN) ## Run failure policies and error handling pipeline
	@echo "==> Executing examples/failure_policies/scripts/pipeline.yaml..."
	@$(CONDOR_BIN) examples/failure_policies/scripts/pipeline.yaml

run-backends: $(CONDOR_BIN) ## Run custom template grid backend pipeline
	@echo "==> Executing examples/custom_backends/scripts/pipeline.yaml..."
	@$(CONDOR_BIN) examples/custom_backends/scripts/pipeline.yaml

run-verilator: $(CONDOR_BIN) ## Run Verilator ALU build & tests WITHOUT coverage
	@echo "==> Executing Verilator ALU Pipeline [NO COVERAGE, mode: $(OUTPUT_MODE)]..."
	@$(CONDOR_BIN) examples/verilator/scripts/pipeline.yaml --output-mode $(OUTPUT_MODE)

run-verilator-coverage: $(CONDOR_BIN) ## Run Verilator ALU build & tests WITH coverage
	@echo "==> Executing Verilator ALU Pipeline [WITH COVERAGE, mode: $(OUTPUT_MODE)]..."
	@$(CONDOR_BIN) examples/verilator/scripts/pipeline.yaml --define-variable coverage=1 --output-mode $(OUTPUT_MODE)

run-ci: $(CONDOR_BIN) ## Run pipeline in non-interactive CI mode with periodic status dumps
	@echo "==> Executing in CI mode (--no-interactive, 2s status dump interval)..."
	@$(CONDOR_BIN) --no-interactive --status-interval-sec 2 examples/simple/scripts/pipeline.yaml

# ------------------------------------------------------------------------------
# Documentation Flows
# ------------------------------------------------------------------------------
docs: docs-doxygen ## Generate HTML documentation portal

docs-doxygen: ## Generate Doxygen C++ API & Markdown documentation portal (_site/)
	@if command -v doxygen >/dev/null 2>&1; then \
		echo "==> Generating Doxygen documentation portal..."; \
		doxygen docs/Doxyfile; \
		echo "==> Documentation successfully generated in _site/"; \
	else \
		echo "======================================================================"; \
		echo "ERROR: Doxygen is required to generate documentation,"; \
		echo "       but 'doxygen' was not found on PATH."; \
		echo "======================================================================"; \
		echo "Please install Doxygen:"; \
		echo "  - Ubuntu/Debian: sudo apt-get install -y doxygen graphviz"; \
		echo "  - Fedora/RHEL:   sudo dnf install doxygen graphviz"; \
		echo "  - Arch Linux:    sudo pacman -S doxygen graphviz"; \
		exit 1; \
	fi

docs-serve: ## Serve documentation portal locally on http://127.0.0.1:8000
	@if [ ! -d "_site" ] || [ ! -f "_site/index.html" ]; then \
		echo "==> Documentation portal not yet generated. Building with 'make docs'..."; \
		$(MAKE) docs; \
	fi
	@echo "==> Serving documentation portal on http://127.0.0.1:8000 (Ctrl+C to stop)..."
	@python3 -m http.server 8000 --directory _site

# ------------------------------------------------------------------------------
# Installation & Packaging Flows
# ------------------------------------------------------------------------------
install: $(CONDOR_BIN) $(HUB_BIN) ## Install condor binaries, examples, and documentation to PREFIX
	@echo "==> Installing Condor to $(DESTDIR)$(PREFIX)..."
	@DESTDIR="$(DESTDIR)" cmake --install $(BUILD_DIR) --prefix "$(PREFIX)"

uninstall: ## Uninstall files installed by previous make install
	@if [ -f $(BUILD_DIR)/install_manifest.txt ]; then \
		echo "==> Uninstalling files listed in $(BUILD_DIR)/install_manifest.txt..."; \
		xargs rm -f < $(BUILD_DIR)/install_manifest.txt; \
		rm -rf "$(DESTDIR)$(PREFIX)/share/condor" 2>/dev/null || true; \
		rm -rf "$(DESTDIR)$(PREFIX)/share/doc/condor" 2>/dev/null || true; \
		echo "==> Uninstall complete."; \
	else \
		rm -f "$(DESTDIR)$(PREFIX)/bin/condor" "$(DESTDIR)$(PREFIX)/bin/condor-hub"; \
		rm -rf "$(DESTDIR)$(PREFIX)/share/condor" 2>/dev/null || true; \
		rm -rf "$(DESTDIR)$(PREFIX)/share/doc/condor" 2>/dev/null || true; \
		echo "==> Removed binary and share directories."; \
	fi

package-deb: $(CONDOR_BIN) ## Build Debian (.deb) package
	@echo "==> Building Debian (.deb) package..."
	@mkdir -p $(BUILD_DIR)/packages
	@cd $(BUILD_DIR) && cpack -G DEB -B packages

package-rpm: $(CONDOR_BIN) ## Build Enterprise Linux RPM (.rpm) package
	@echo "==> Building RPM (.rpm) package..."
	@mkdir -p $(BUILD_DIR)/packages
	@cd $(BUILD_DIR) && cpack -G RPM -B packages

package-tar: $(CONDOR_BIN) ## Build standalone portable tarball (.tar.gz) package
	@echo "==> Building portable tarball (.tar.gz) package..."
	@mkdir -p $(BUILD_DIR)/packages
	@cd $(BUILD_DIR) && cpack -G TGZ -B packages

package packages: package-deb package-rpm package-tar ## Build all distribution packages (DEB, RPM, TGZ)

# ------------------------------------------------------------------------------
# Cleaning Flows
# ------------------------------------------------------------------------------
clean: ## Remove build artifacts
	@echo "==> Cleaning build directory..."
	@rm -rf $(BUILD_DIR)

clean-runs: ## Remove generated execution directories (condor_runs/)
	@echo "==> Cleaning run output directories..."
	@rm -rf condor_runs/ test_condor_runs/ test_sched_run/ test_sched_fail/ test_pipe_env/ test_pipe_seq/ hub_artifacts/

clean-docs: ## Remove generated documentation portal (_site/)
	@echo "==> Cleaning documentation portal directory..."
	@rm -rf _site

clean-all: clean clean-runs clean-docs ## Clean build artifacts, run logs, and docs
