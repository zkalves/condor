# Condor

A modern C++20 parallel process and grid orchestrator designed for high-performance pipelines, simulation harnesses, and build/test workflows.

---

## Features

- **Modern C++20 Standard**: Built using standard POSIX APIs, `yaml-cpp` (v0.9.0), and `CLI11` (v2.6+). Zero Boost or proprietary dependencies.
- **Strict Identifier Validation**: Stage names and variables are strictly validated against POSIX-safe identifier regex `^[a-zA-Z0-9_-]+$`.
- **DAG Scheduler & Cycle Detection**: Automatically detects circular dependencies and schedules jobs based on cross-stage dependencies.
- **Hierarchical Working Directories**:
  - Timestamped runs: `./condor_runs/YYYYMMDD_HHMMSS_<6_digit_random>`
  - Atomic `latest` symlink pointing to active run directory
  - Fixed run directories via `--run-dir <path>`
  - Job execution directly inside `<run_dir>/<stage_name>/<job_id>/`
  - Automated `stdout.log`, `stderr.err`, `job_status.txt`, and `stage_manifest.txt`
- **Application-Independent Variables**:
  - Top-level `variables:` map in YAML with CLI overrides via `--define-variable KEY=VAL`
  - `${KEY}` placeholder interpolation in commands and hooks
  - Environment export as `CONDOR_VAR_<UPPER_KEY>=<VAL>`
- **Conditional Execution (`enabled_if`)**:
  - Stage and job-level conditions (e.g. `"${coverage} == '1'"` or `"${coverage} != '0'"`)
  - Skipped jobs satisfy downstream DAG dependency edges automatically
- **Configurable Failure Modes**:
  - `on_failure_action`: `abort`, `continue`, or `stage_barrier`
  - Per-job and per-stage `allow_failure: true | false`
  - Global `--exit-zero-on-failure` support
- **Signal Handling & Interactive Runtime Menu**:
  - Double `CTRL+C` within 2 seconds immediately terminates all local process groups (`kill(-pgid, SIGKILL)`) and cancels grid jobs
  - Interactive menu accessible via single `CTRL+C` or pressing `m` during execution (`[s]` status, `[p]` pause/resume, `[k]` kill job, `[j]` scale slots, `[c]` continue, `[q]` quit)
- **Live Independent Stage Progress & Configurable Terminal Output**:
  - Independent stage progress bars `[##########] 100% [PASSED]` and status badges in interactive TTY mode
  - Configurable output modes via CLI `--output-mode <all|failures|summary>` or YAML `output_mode`:
    - `all`: Displays full test execution events, stage bars, and detailed results
    - `failures`: Displays progress bars and logs only failing tests and errors
    - `summary`: Displays clean progress tracking and overall consolidated summary only
  - Periodic single-line timestamped summaries (`--status-interval-sec <N>`) in CI / `--no-interactive` mode
- **Deterministic Per-Stage Sequential Job IDs & Machine-Readable YAML**:
  - Deterministic job IDs (`0001`, `0002`, ...) assigned per-stage rather than globally
  - Every job generates both human-readable `job_status.txt` and machine-readable `job_status.yaml`
  - Verilator test harness generates `test_metrics.yaml` and consolidated `pipeline_summary.yaml`
- **Pluggable Execution Backends**:
  - `LocalBackend`: POSIX `fork()`, `setpgid()`, `chdir()`, `dup2()`, non-blocking `waitpid(WNOHANG)`
  - `TemplateBackend`: Generates executable `job_runner.sh`, template placeholders (`{job_id}`, `{script}`, `{grid_id}`), with built-in presets for `slurm` and `sge`

---

## Installation & Multi-System Support

Condor is built with an offline-first philosophy and runs natively on all major Linux distributions, macOS, and Windows (via WSL2).

### 1. Pre-built Distribution Packages (No Build Tools Required)

Pre-built packages are published on the GitHub [Releases](https://github.com/zkalves/condor/releases) page:

* **Debian / Ubuntu (`.deb`)**:
  ```bash
  sudo apt install ./condor_1.0.0_amd64.deb
  ```
* **Fedora / RHEL 9 / Rocky Linux 9 / openSUSE (`.rpm`)**:
  ```bash
  # Fedora / RHEL 9 / Rocky Linux 9
  sudo dnf install ./condor-1.0.0.x86_64.rpm
  # openSUSE / SLES
  sudo zypper install ./condor-1.0.0.x86_64.rpm
  ```
* **Portable Standalone Tarball (Any Linux Distro, No Root Required)**:
  ```bash
  tar -xzf condor-1.0.0-Linux.tar.gz
  ./condor-1.0.0-Linux/usr/bin/condor --version
  ```
* **Homebrew (macOS & Linux)**:
  ```bash
  brew install zkalves/condor/condor
  ```
* **OCI / Docker Container (CI/CD Pipelines)**:
  ```bash
  docker run --rm -v "$(pwd):/work" -w /work ghcr.io/zkalves/condor:latest condor --help
  ```
* **Environment Modules / Lmod (EDA & HPC Compute Clusters)**:
  ```bash
  module use /tools/modulefiles && module load condor/1.0.0
  ```

---

### 2. Installing Build Dependencies by System

If compiling Condor from source, install the minimal build dependencies (CMake 3.20+ and a C++20 compiler):

* **Ubuntu / Debian (22.04 LTS, 24.04 LTS, Debian 12+)**:
  ```bash
  sudo apt-get update && sudo apt-get install -y build-essential cmake git
  ```
* **Fedora / RHEL 9 / Rocky Linux 9 / AlmaLinux 9 / CentOS Stream 9**:
  ```bash
  sudo dnf install -y gcc-c++ cmake git rpm-build
  ```
* **RHEL 8 / Rocky Linux 8 / AlmaLinux 8 (Enterprise Legacy)**:
  ```bash
  sudo dnf install -y epel-release && sudo dnf install -y gcc-toolset-11-gcc-c++ cmake git rpm-build
  source /opt/rh/gcc-toolset-11/enable
  ```
* **openSUSE (Leap 15.5+, Tumbleweed) & SLES 15 (SP4+)**:
  ```bash
  sudo zypper install -y gcc-c++ cmake git rpm-build
  ```
* **Arch Linux & Manjaro**:
  ```bash
  sudo pacman -S --needed base-devel cmake git
  ```
* **Alpine Linux (musl libc / Minimal Container Images)**:
  ```bash
  apk add --no-cache build-base cmake git bash
  ```
* **macOS (Apple Silicon & Intel)**:
  ```bash
  xcode-select --install
  brew install cmake git
  ```
* **Windows 10 & 11 (via WSL2)**:
  ```bash
  # Inside Ubuntu/Debian WSL2 terminal:
  sudo apt-get update && sudo apt-get install -y build-essential cmake git
  ```

For the complete distribution matrix and glibc baselines, see the [Supported Operating Systems Guide](file:///home/eze/projects/github/condor/docs/user/supported-os.md).

---

## Building and Testing

### Prerequisites
- CMake 3.20+
- Modern C++20 compiler (`g++` 11+, `clang++` 13+)

### Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run Tests
```bash
ctest --test-dir build --output-on-failure
# Or run test binary directly:
./build/condor_tests
```

---

## Documentation

Condor features a comprehensive interactive HTML documentation portal styled with Solarized Dark and Doxygen-Awesome:
- **User Guide**: Getting started, CLI reference, pipeline YAML schema, examples catalog, HTML reports, and architecture.
- **Developer Guide**: C++20 subsystem architecture, coding & quality standards, test harness architecture, and 23-suite unit test catalog.
- **C++ API Reference**: Complete class hierarchies, interactive SVG dependency graphs, and Doxygen docstrings.

### Generating & Viewing Documentation:
```bash
# Generate documentation portal (_site/)
make docs

# Serve documentation portal locally at http://127.0.0.1:8000
make docs-serve
```

---

## CLI Usage

```
Syntax: condor [OPTIONS] <pipeline.yaml>

POSITIONALS:
  pipeline_file               Path to pipeline YAML configuration

OPTIONS:
  -h, --help                  Print this help message and exit
  --pipeline-config <path>    Path to pipeline YAML configuration
  --stage <name>              Run only the specified stage (^[a-zA-Z0-9_-]+$)
  --max-parallel-jobs <N>     Maximum concurrent local jobs (default: hardware concurrency)
  --base-output-dir <path>    Base folder for timestamped runs (default: ./condor_runs)
  --run-dir <path>            Run inside explicit fixed directory
  --backend-override <name>   Override default execution backend
  --on-failure-action <mode>  Override failure policy (abort, continue, stage_barrier)
  --exit-zero-on-failure      Force return code 0 even if jobs or stages fail
  --status-interval-sec <N>   Interval in seconds to dump status summaries in CI mode (default: 30)
  --output-mode <mode>        Terminal output mode (all, failures, summary)
  --path-style <mode>         Path representation inside runs (absolute, relative; default: absolute)
  --relative-paths            Use relative paths inside runs (equivalent to --path-style=relative)
  --absolute-paths            Use absolute paths inside runs (equivalent to --path-style=absolute)
  --define-variable <KEY=VAL> Define or override configuration variables (repeatable)
  --dry-run                   Validate names, DAG cycles, evaluate conditions, print plan, and exit
  --no-interactive            Disable interactive stdin polling and ANSI progress redraws
  --verbose-logging           Enable detailed scheduler event logging
```

---

## Example Pipeline

```yaml
name: build_and_test

variables:
  coverage: "0"
  opt_level: "O2"

settings:
  max_parallel_jobs: 4
  base_output_dir: "./condor_runs"
  on_failure_action: "abort"
  path_style: "relative"  # "absolute" (default) or "relative" (portable relative paths inside runs)

stages:
  - name: build
    jobs:
      - name: compile_app
        command: "g++ -${opt_level} main.cpp -o app"

  - name: test
    depends_on: [build]
    jobs:
      - name: smoke_test
        command: "./app --smoke"

  - name: coverage_report
    depends_on: [test]
    enabled_if: "${coverage} == '1'"
    command: "gcov app"
```

---

## Interactive HTML Execution Report (`report.html`)

For every pipeline run, Condor automatically compiles a zero-dependency, self-contained interactive HTML report in `<run_dir>/report.html`:

- **Per-Stage Filtering & Views**:
  - **Stage Navigation Tabs**: Select any individual stage pill (`build_rtl`, `sim_tests`, etc.) or `All Stages` to immediately filter the view.
  - **Clickable Stage Progression Bars**: Clicking any stage bar in the overview scrolls to and isolates that stage.
  - **Stage Sections**: Jobs are cleanly grouped into stage containers featuring stage status badges, execution duration, and individual collapse/expand toggles.
  - **Deep-Linking via URL Hash**: Share direct links to specific stages (e.g., `report.html#stage=sim_tests`).
- **Tool-Agnostic YAML Machine Metrics**: Automatically parses any `*.yaml` files written to job folders into structured metric cards.
- **Embedded Log Inspectors**: Live collapsibles for standard output (`stdout.log`) and standard error (`stderr.err`).
- **Search & Status Filtering**: Instant real-time filtering across job IDs, stages, and command lines combined with Pass/Fail/Skip status filters.

---

## Example Pipelines (Minimal Set Covering All Configurations)

Every example adheres to the exact same self-contained directory layout:
```
examples/<example_name>/
└── scripts/
    ├── pipeline.yaml         # Pipeline configuration inside scripts/
    └── [runner scripts and modular includes]
```

The table below summarizes the minimal set of examples covering 100% of Condor's configuration capabilities:

| Example | Directory | Key Configurations Covered |
| :--- | :--- | :--- |
| **Simple** | [`examples/simple/`](file:///home/eze/projects/github/condor/examples/simple/) | Linear DAG (`build -> test -> package`), stage single-command shorthand vs explicit `jobs:`, global & stage variables, status interval. |
| **Conditional DAG** | [`examples/conditional/`](file:///home/eze/projects/github/condor/examples/conditional/) | Diamond/branching DAG, stage & job-level `enabled_if` conditionals (`==`, `!=`, comparisons), skipped stage dependency satisfaction. |
| **Matrix & Includes** | [`examples/matrix_and_includes/`](file:///home/eze/projects/github/condor/examples/matrix_and_includes/) | `path_style: relative`, global `defaults:`, stage-level `defaults:` merging, test replication (`count: 4`), multi-seed sweeps (`seeds: [...]`), modular test suite `include: "test_suite.yaml"`. |
| **Failure Policies** | [`examples/failure_policies/`](file:///home/eze/projects/github/condor/examples/failure_policies/) | `on_failure_action: continue`, `exit_zero_on_failure: true`, `allow_failure: true` on stages & jobs, lifecycle hooks with `always: true`, `output_mode: failures`. |
| **Custom Backends** | [`examples/custom_backends/`](file:///home/eze/projects/github/condor/examples/custom_backends/) | Custom template backends (`type: template` e.g. Slurm/SGE simulation), `{script}`, `{grid_id}`, `{job_id}`, stage-level backend, job-level backend overrides, job-level `depends_on: ["prepare/0002"]`. |
| **Verilator EDA** | [`examples/verilator/`](file:///home/eze/projects/github/condor/examples/verilator/) | Real-world hardware verification pipeline, SystemVerilog RTL, cycle-accurate C++ testbench, 10 test suites, conditional coverage (`verilator_coverage`), unified report generation. |

### Running the Examples:
```bash
# Basic build/test/package pipeline
make run-simple

# Conditional execution (default: fast smoke test, coverage skipped)
make run-conditional

# Conditional execution with coverage enabled
make run-coverage

# Matrix scaling (count: 4), seeds parameter sweep, and modular includes
make run-matrix

# Failure policies (continue on failure), allowed failures, and always-run cleanup hooks
make run-failures

# Custom template backends (Slurm/SGE grid cluster simulation)
make run-backends

# Verilator Hardware Verification (without and with coverage)
make run-verilator
make run-verilator-coverage
```

---

## Verilator Pipeline Example (With & Without Coverage)

A fully working hardware verification pipeline is included in [`examples/verilator/`](file:///home/eze/projects/github/condor/examples/verilator/) with [`examples/verilator/scripts/pipeline.yaml`](file:///home/eze/projects/github/condor/examples/verilator/scripts/pipeline.yaml):

- **RTL Design**: [`examples/verilator/rtl/alu.sv`](file:///home/eze/projects/github/condor/examples/verilator/rtl/alu.sv) - Parametric 32-bit ALU implementing arithmetic, logic, shift, and multiplication operations adhering to project HDL guidelines.
- **C++ Testbench**: [`examples/verilator/tb/tb_alu.cpp`](file:///home/eze/projects/github/condor/examples/verilator/tb/tb_alu.cpp) - Driver supporting 10 distinct verification test suites (`arithmetic`, `logic`, `shift`, `mult`, `overflow`, `zero_flag`, `boundary`, `fuzz`, `comparison`, `pipeline_stress`) and generating coverage data.
- **Pipeline Stages**:
  1. `build_rtl`: Staged compilation across 4 phases (linting, AST translation, -O3 optimization, linking) lasting ~12s.
  2. `sim_tests`: Concurrently executes 10 stress test suites across worker slots lasting ~5–7s each with millions of verified assertions.
  3. `coverage_analysis`: Skipped when `coverage == '0'`. When `coverage == '1'`, merges all 10 coverage databases and annotates source code.
  4. `report`: Generates consolidated summary reports (`pipeline_summary.yaml`, `test_report.txt`, `test_report.md`, `terminal_report.txt`).

### Running with Make:
```bash
# Run without coverage (standard performance flow, default mode: all)
make run-verilator

# Run with only summary terminal output
make run-verilator OUTPUT_MODE=summary

# Run showing only failures in terminal
make run-verilator OUTPUT_MODE=failures

# Run with coverage (enables coverage compilation, analysis, and line metrics)
make run-verilator-coverage
```

---

## License

This project is licensed under the terms of the **GNU General Public License v3.0 (GPL-3.0)**. See the [LICENSE](file:///home/eze/projects/github/condor/LICENSE) file for complete details.

Copyright (C) 2026 Ezequiel Alves. All rights reserved.
