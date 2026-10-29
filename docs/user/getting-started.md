# Getting Started {#getting_started}

Welcome to **Condor** (**Con**current + **D**ependency + **Or**chestrator)! This guide assists you in getting started quickly with Condor, whether you prefer installing pre-compiled distribution packages (zero build tools required) or compiling from source.

---

## Choosing Your Installation Method

| Installation Method | Best For | Prerequisites Required | Installation Effort |
| :--- | :--- | :--- | :--- |
| **Option 1: Portable Binary Tarball** | Quick evaluation, portable across all Linux distros | None (zero runtime dependencies) | Download & extract |
| **Option 2: Native Package (.deb / .rpm)** | Desktop users, standard workstation installations | System package manager (`apt`, `dnf`, `zypper`) | 1 command (`apt install` / `dnf install`) |
| **Option 3: OCI / Docker Container** | CI/CD pipelines, containerized build environments | Docker or Podman | 1 command (`docker run`) |
| **Option 4: Homebrew Formula** | macOS users and Linuxbrew environments | Homebrew (`brew`) | 1 command (`brew install`) |
| **Option 5: Environment Modules / Lmod** | Shared EDA server clusters & HPC environments | `lmod` or `environment-modules` | Extract & `module load` |
| **Option 6: Compiling from Source** | Developers, contributors, air-gapped secure builds | C++20 compiler (`gcc` 11+ or `clang` 13+), CMake 3.20+ | Build with `make` or `cmake` |

---

## 1. Quick Start: Pre-built Packages (No Build Tools Required)

Pre-built distribution packages and standalone binary tarballs for 64-bit Linux (`x86_64`) and ARM64 (`aarch64`) are published on the GitHub [Releases](https://github.com/zkalves/condor/releases) page.

### Option A: Portable Standalone Binary Tarball (All Linux Distributions)

The portable tarball is completely self-contained with zero external runtime dependencies. It requires no root privileges:

```bash
# Download and unpack
tar -xzf condor-1.0.0-Linux.tar.gz

# Run directly
./condor-1.0.0-Linux/usr/bin/condor --version

# Optional: Install to system /usr/local
sudo cp -r condor-1.0.0-Linux/usr/* /usr/local/
```

### Option B: Debian / Ubuntu Package (.deb)

Installs `condor` system-wide with man pages, example workflows, and offline documentation:

```bash
# Install with apt
sudo apt update
sudo apt install ./condor_1.0.0_amd64.deb

# Or install using dpkg:
sudo dpkg -i condor_1.0.0_amd64.deb

# Verify installation:
condor --version
```

### Option C: Fedora / RHEL 9 / Rocky Linux / openSUSE (.rpm)

Installs `condor` system-wide using your distribution's native package manager:

```bash
# Fedora / RHEL 9 / Rocky Linux 9 / AlmaLinux 9 / CentOS Stream 9
sudo dnf install ./condor-1.0.0.x86_64.rpm

# openSUSE Leap / SLES
sudo zypper install ./condor-1.0.0.x86_64.rpm

# Verify installation:
condor --version
```

### Option D: OCI / Docker Container Image (Docker / Podman)

Official container images are published to the GitHub Container Registry (`ghcr.io/zkalves/condor:latest`):

```bash
# Pull the latest container image
docker pull ghcr.io/zkalves/condor:latest

# Display version
docker run --rm ghcr.io/zkalves/condor:latest condor --version

# Run a pipeline mounting current repository workspace
docker run --rm -v "$(pwd):/work" -w /work ghcr.io/zkalves/condor:latest \
  condor examples/simple/scripts/pipeline.yaml
```

### Option E: Homebrew Formula (macOS & Linux)

Install `condor` using Homebrew on macOS (Apple Silicon and Intel) or Linux (Linuxbrew):

```bash
# Tap repository and install condor
brew install zkalves/condor/condor

# Verify installation
condor --version
```

### Option F: Environment Modules / Lmod (HPC & EDA Clusters)

For semiconductor design teams, shared compute clusters, and HPC environments utilizing Lmod or classical Environment Modules:

```bash
# Extract relocatable package into cluster software repository (e.g., /tools)
tar -xzf condor-1.0.0-module-linux-x86_64.tar.gz -C /tools/

# Add modulefiles directory to MODULEPATH
module use /tools/modulefiles

# Load condor module into current shell session
module load condor/1.0.0

# Verify
condor --version
```

@note For the complete distribution compatibility matrix, glibc baselines, and packaging details, see the [Supported Operating Systems & Compatibility Matrix Guide](@ref supported_os).

---

## 2. Compiling from Source

Follow these steps if you wish to compile **Condor** from source code or contribute to development.

### Build Toolchain & System Prerequisites
- **C++ Compiler**: Modern C++20 compiler (`g++` 11+ or `clang++` 13+)
- **Build System**: CMake 3.20+ and `make` (or `ninja`)
- **Third-Party C++ Dependencies**: Condor relies on `CLI11` (v2.6+) and `yaml-cpp` (v0.9+). They are resolved automatically during build:
  - **Automatic Download (Default)**: CMake uses `FetchContent` to download and build them on first compilation with zero configuration needed.
  - **System Packages**: Can be installed via system package manager (e.g. `libyaml-cpp-dev` & `libcli11-dev` on Debian/Ubuntu, `yaml-cpp-devel` & `cli11-devel` on Fedora/RHEL).
  - **Local Offline Vendoring**: For strictly air-gapped environments without network access, run `make download-deps` (which fetches them into `third_party/`, a git-ignored directory). CMake will automatically detect them locally.

### Installing Build Dependencies Across Operating Systems

#### Ubuntu & Debian (22.04 LTS / 24.04 LTS / Debian 12+)
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git
```

#### Fedora / RHEL 9 / Rocky Linux 9 / AlmaLinux 9 / CentOS Stream 9
```bash
sudo dnf install -y gcc-c++ cmake git rpm-build
```

#### RHEL 8 / Rocky Linux 8 / AlmaLinux 8 (Enterprise Legacy)
RHEL 8 default repositories provide GCC 8. Modern C++20 requires **GCC Toolset 11** or **12**:
```bash
# Enable EPEL and PowerTools/CRB
sudo dnf install -y epel-release dnf-plugins-core
sudo dnf config-manager --set-enabled powertools  # On Rocky/Alma: 'crb'

# Install GCC Toolset 11, CMake, and RPM tools
sudo dnf install -y gcc-toolset-11-gcc-c++ cmake git rpm-build

# Enable GCC 11 in current terminal session
source /opt/rh/gcc-toolset-11/enable
```

#### openSUSE (Leap 15.5+ / Tumbleweed) & SLES 15 (SP4+)
```bash
sudo zypper install -y gcc-c++ cmake git rpm-build
```

#### Arch Linux & Manjaro
```bash
sudo pacman -S --needed base-devel cmake git
```

#### Alpine Linux (musl libc / Minimal Containers)
```bash
apk update
apk add --no-cache build-base cmake git bash
```

#### macOS (Apple Silicon & Intel)
```bash
xcode-select --install
brew install cmake git
```

#### Windows 10 & 11 (WSL2)
Inside WSL2 (Ubuntu / Debian prompt):
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git
```

---

## 3. Building and Installing

Condor supports multiple build workflows depending on your environment:

### Standard Build (Auto-Fetching or System Packages)
The default build automatically resolves `CLI11` and `yaml-cpp` via system libraries or CMake `FetchContent`:

```bash
# Clone the repository
git clone https://github.com/zkalves/condor.git
cd condor

# Build in Release mode (uses local third_party if present, otherwise system or FetchContent)
make build

# Run verification test suite
make test-verbose

# Install system-wide (defaults to /usr/local)
sudo make install

# To install into a custom prefix:
make install PREFIX=$HOME/.local
```

### Air-Gapped Offline Build (Zero Network Access)
For restricted HPC/EDA compute clusters or air-gapped workstations where compile nodes have no internet access:

```bash
# 1. On an internet-connected staging machine, populate third_party/
make download-deps

# 2. Transfer the tree to the air-gapped machine and compile with zero network requests
make build-offline
```

To remove installed files later:
```bash
sudo make uninstall
```

---

## 4. Running Your First Pipeline

@note **PATH Requirement**: `condor` must be available in your system `PATH`.
- If installed system-wide (`sudo make install`), `condor` is located in `/usr/local/bin` (already in standard `PATH`).
- If installed to a custom prefix (e.g. `make install PREFIX=$HOME/.local`), ensure `$HOME/.local/bin` is exported in your `PATH`: `export PATH="$HOME/.local/bin:$PATH"`
- If running directly from source build without installing, add the build directory to your `PATH`: `export PATH="$PWD/build:$PATH"`
All commands below invoke `condor` directly from the `PATH`.

### Step 1: Validate the Pipeline Plan (--dry-run)

Use the `--dry-run` flag to validate stage names, verify DAG dependencies, evaluate conditions, and inspect the scheduled jobs plan without spawning any processes:

```bash
condor examples/simple/scripts/pipeline.yaml --dry-run
```

Expected output:
```text
================ CONDOR DRY-RUN EXECUTION PLAN ================
Pipeline Name:       simple_pipeline
Max Parallel Jobs:   4
Base Output Dir:     ./condor_runs
On Failure Action:   abort
Exit Zero On Fail:   false
Output Mode:         all
Path Style:          absolute
----------------------------------------------------------------
Stages & Jobs Plan:
[STAGE] build (Enabled: YES, Allow Failure: false)
  - [0001] compile_app (Command: bash .../build.sh)
[STAGE] test (Depends On: build)
  - [0001] unit_tests (Command: bash .../test.sh)
  - [0002] integration_tests (Command: bash .../test.sh)
[STAGE] package (Depends On: test)
  - [0001] package (Command: bash .../package.sh)
================================================================
```

### Step 2: Execute the Pipeline

Run the pipeline using the default local execution backend:

```bash
condor examples/simple/scripts/pipeline.yaml
```

During execution, Condor displays a real-time responsive ANSI progress display showing the overall DAG progression, per-stage progress tracks, active running jobs, PIDs, and elapsed runtimes:

```text
[DAG Pipeline]             [######################] 100% | Jobs: 4 | Running: 0 | Queued: 0 | Passed: 4
  Stage: build             [######################] 100% [PASSED]   (1/1 passed)
  Stage: test              [######################] 100% [PASSED]   (2/2 passed)
  Stage: package           [######################] 100% [PASSED]   (1/1 passed)

====================== CONDOR PIPELINE EXECUTION SUMMARY =======================
Result:       SUCCESS
Total Jobs:   4
Passed:       4
Failed:       0
Skipped:      0
Total Time:   00:00:01
HTML Report:  /path/to/condor_runs/20261026_145117_491021/artifacts/report.html
================================================================================
```

### Step 3: Inspect the Generated HTML Report

Open the generated HTML report in your browser:
```bash
xdg-open ./condor_runs/latest/artifacts/report.html
```

---

## 5. Running with Condor Hub (Central Reporting & Remote Control Plane)

Condor includes **Condor Hub** (`condor-hub`), a standalone C++20 socket server that provides centralized real-time telemetry streaming and bidirectional remote control across runners.

### Step 1: Start the Condor Hub Server

```bash
# Start Condor Hub on http://0.0.0.0:8080 (or use 'make run-server')
condor-hub --port 8080
```

Open [`http://localhost:8080/`](http://localhost:8080/) in your web browser. You will see the live dark-mode fleet dashboard ready to receive pipeline runners.

### Step 2: Execute Pipelines with Server Reporting

Run any pipeline with `--server-url`:

```bash
# Run simple pipeline reporting to the Hub
condor examples/simple/scripts/pipeline.yaml --server-url ws://localhost:8080/api/v1/ws

# Run Verilator hardware verification with tags and project name
condor examples/verilator/scripts/pipeline.yaml \
    --server-url ws://localhost:8080/api/v1/ws \
    --server-project "Verilator ALU" \
    --server-tags "rtl,sim"
```

From the web dashboard, you can monitor stage progression live and interactively pause/resume execution or abort runaway jobs remotely.

---

## 6. Next Steps

- Consult the [Supported Operating Systems & Compatibility Matrix Guide](@ref supported_os) for enterprise packaging and container deployment.
- Explore the [CLI Reference](@ref cli_reference) for all command-line options and execution modifiers.
- Learn how to author custom workflows in the [Pipeline YAML Specification](@ref pipeline_spec).
- Browse the [Examples Catalog](@ref examples_catalog) for EDA hardware verification, matrix sweeps, and distributed grid backends.
