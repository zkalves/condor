# Supported Operating Systems & Compatibility Matrix {#supported_os}

This document defines the supported operating systems, Linux distribution compatibility baselines, glibc requirements, pre-built package deliverables, and system-specific toolchain installation instructions for **Condor**.

---

## 1. Supported Linux Distributions & Compatibility Matrix

**Condor** is built with Modern C++ (C++20) and adheres to standard POSIX system interfaces. It runs on both `x86_64` and `aarch64` (ARM64) architectures without external runtime dependencies:

| Distribution Family | Earliest Supported Version | glibc Baseline | Toolchain / Compiler | Status & Supported Deliverables |
| :--- | :--- | :--- | :--- | :--- |
| **Ubuntu** | **22.04 LTS (Jammy)** / **24.04 LTS (Noble)** | 2.35 / 2.38 | GCC 11 / 12 / 13 / 14 | **Tier-1 Native**: Pre-built `.deb`, `.tar.gz`, Docker, source build |
| **Debian** | **12 (Bookworm)** / **13 (Trixie)** | 2.36 / 2.38 | GCC 12 / 13 / 14 | **Tier-1 Native**: Pre-built `.deb`, `.tar.gz`, Docker, source build |
| **Fedora** | **38+** | &ge; 2.37 | GCC 13 / 14 | **Tier-1 Native**: Pre-built `.rpm`, `.tar.gz`, source build |
| **RHEL / Rocky / AlmaLinux 9** | **9.0+** (Full support) | 2.34 | GCC 11 / 12 (AppStream) | **Tier-1 Native**: Pre-built `.rpm`, `.tar.gz`, source build |
| **CentOS Stream 9** | **9.0+** (Full support) | 2.34 | GCC 11 / 12 | **Tier-1 Native**: Pre-built `.rpm`, `.tar.gz`, source build |
| **RHEL / Rocky / AlmaLinux 8** | **8.6+** | 2.28 | `gcc-toolset-11` (EPEL 8) | **Supported via Toolset**: Native source build (`make package-rpm`) |
| **openSUSE Leap** | **15.5+** / **Tumbleweed** | &ge; 2.38 | GCC 12 / 13 / 14 | **Tier-1 Native**: Pre-built `.rpm`, `.tar.gz`, source build |
| **SUSE Linux Enterprise (SLES)**| **15 SP4+** / **15 SP6** | 2.31 / 2.38 | GCC 11 / 12 | **Supported**: Native source build, `.rpm` |
| **Arch Linux / Manjaro** | **Rolling** | &ge; 2.39 | GCC 13 / 14 / Clang 16+ | **Tier-1 Native**: Native source build, AUR package |
| **Alpine Linux** | **3.16+** | musl libc | GCC 11 / 12 / 13 | **Tier-1 Native**: Lightweight static / musl source build |

### macOS Support
* **macOS 12+ (Monterey, Ventura, Sonoma, Sequoia)**: Supported natively on both Apple Silicon (`arm64`: M1/M2/M3/M4) and Intel (`x86_64`) architectures. Install via Homebrew formula (`brew install zkalves/condor/condor`) or compile with Apple Clang 14+ (`brew install cmake`).

### Windows Support
* **Windows 10 / 11 via WSL2 (Windows Subsystem for Linux)**: Fully supported using Ubuntu 22.04 LTS, Ubuntu 24.04 LTS, or Debian 12 inside WSL2. Condor utilizes native Linux POSIX process group controls (`fork`, `setpgid`, `kill(-pid, ...)`).

---

## 2. Pre-built Distribution Packages & Quick Install

Pre-compiled distribution packages and standalone binary tarballs are published on the GitHub [Releases](https://github.com/zkalves/condor/releases) page for every release.

### Option A: Portable Standalone Binary Tarball (All Linux Distributions)

The portable tarball is self-contained with zero runtime dependencies. It installs cleanly into `/usr/local` or any user directory without requiring root privileges:

```bash
# Download and unpack
tar -xzf condor-1.0.0-Linux.tar.gz

# Run directly
./condor-1.0.0-Linux/usr/bin/condor --version

# Optional: Install to system /usr/local
sudo cp -r condor-1.0.0-Linux/usr/* /usr/local/
```

### Option B: Debian / Ubuntu Package (`.deb`)

Installs `condor` system-wide into `/usr/bin/condor` along with manual pages, example pipelines, and offline documentation:

```bash
# Install with apt
sudo apt update
sudo apt install ./condor_1.0.0_amd64.deb

# Or install using dpkg:
sudo dpkg -i condor_1.0.0_amd64.deb

# Verify installation:
condor --version
```

### Option C: Fedora / RHEL 9 / Rocky Linux / openSUSE (`.rpm`)

Install the Enterprise Linux RPM package using `dnf`, `yum`, or `zypper`:

```bash
# Fedora / RHEL 9 / Rocky Linux 9 / AlmaLinux 9 / CentOS Stream 9
sudo dnf install ./condor-1.0.0.x86_64.rpm

# openSUSE Leap / SLES
sudo zypper install ./condor-1.0.0.x86_64.rpm

# Verify installation:
condor --version
```

### Option D: OCI / Docker Container Image (Multi-Platform / CI/CD)

Container images are hosted on GitHub Container Registry (`ghcr.io/zkalves/condor:latest`) for containerized pipelines, GitLab CI, GitHub Actions, and Kubernetes runners:

```bash
# Pull container image
docker pull ghcr.io/zkalves/condor:latest

# Display CLI help
docker run --rm ghcr.io/zkalves/condor:latest condor --help

# Execute a pipeline mounting current repository workspace
docker run --rm -v "$(pwd):/work" -w /work ghcr.io/zkalves/condor:latest \
  condor examples/simple/scripts/pipeline.yaml
```

### Option E: Homebrew Formula (macOS & Linuxbrew)

Package formula for the Homebrew package manager supporting macOS (Apple Silicon & Intel) and Linux:

```bash
# Tap and install condor
brew install zkalves/condor/condor

# Verify installed binary
condor --version
```

### Option F: Environment Modules / Lmod (HPC & EDA Clusters)

For semiconductor design teams, shared EDA server clusters, and HPC clusters utilizing Lmod or classical Environment Modules:

```bash
# Extract relocatable package into shared cluster tools directory (e.g., /tools)
tar -xzf condor-1.0.0-module-linux-x86_64.tar.gz -C /tools/

# Add modulefiles to search path
module use /tools/modulefiles

# Load condor into current shell session
module load condor/1.0.0

# Verify
condor --version
```

---

## 3. Installing Build Dependencies Across Operating Systems

If you are compiling Condor from source, choose your operating system below to install the minimal required build toolchain (CMake 3.20+ and a C++20 compiler):

### Ubuntu & Debian
Supported: Ubuntu 22.04 LTS, 24.04 LTS, Debian 12 (Bookworm), Debian 13 (Trixie):
```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  git
```

### Fedora & Red Hat Enterprise Linux 9
Supported: Fedora 38+, RHEL 9, Rocky Linux 9, AlmaLinux 9, CentOS Stream 9:
```bash
sudo dnf install -y \
  gcc-c++ \
  cmake \
  git \
  rpm-build
```

### RHEL 8 / Rocky Linux 8 / AlmaLinux 8 (Enterprise Legacy)
RHEL 8 default repositories provide GCC 8. Modern C++20 requires **GCC Toolset 11** or **12**:
```bash
# 1. Enable EPEL and PowerTools/CRB
sudo dnf install -y epel-release dnf-plugins-core
sudo dnf config-manager --set-enabled powertools  # On Rocky/Alma: 'crb'

# 2. Install GCC Toolset 11, CMake, and RPM tools
sudo dnf install -y \
  gcc-toolset-11-gcc-c++ \
  cmake \
  git \
  rpm-build

# 3. Enable GCC 11 in current terminal session
source /opt/rh/gcc-toolset-11/enable
```

### openSUSE & SUSE Linux Enterprise Server (SLES)
Supported: openSUSE Leap 15.5+, Tumbleweed, SLES 15 SP4+:
```bash
sudo zypper install -y \
  gcc-c++ \
  cmake \
  git \
  rpm-build
```

### Arch Linux & Manjaro
```bash
sudo pacman -S --needed \
  base-devel \
  cmake \
  git
```

### Alpine Linux (musl libc / Minimal Containers)
```bash
apk update
apk add --no-cache \
  build-base \
  cmake \
  git \
  bash
```

### macOS (Apple Silicon & Intel)
Using Homebrew:
```bash
# Install CMake (Apple Clang is included with Xcode Command Line Tools)
xcode-select --install
brew install cmake git
```

### Windows 10 & 11 (WSL2)
Inside WSL2 (Ubuntu / Debian prompt):
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git
```

---

## 4. Compiling and Installing from Source

Condor incorporates an **offline-first build flow** that compiles completely without network access using local vendored dependencies in `third_party/`:

```bash
# 1. Clone repository
git clone https://github.com/zkalves/condor.git
cd condor

# 2. Build offline with Make (defaults to release binary in build/condor)
make build-offline

# 3. Run verification test suite
make test-verbose

# 4. Install system-wide (defaults to /usr/local)
sudo make install

# To install into a custom prefix:
make install PREFIX=$HOME/.local
```

### Generating Distribution Packages Locally
You can package Condor directly into `.deb`, `.rpm`, or `.tar.gz` packages:
```bash
# Build Debian package (.deb) in build/packages/
make package-deb

# Build Red Hat / SUSE RPM package (.rpm) in build/packages/
make package-rpm

# Build portable tarball (.tar.gz) in build/packages/
make package-tar

# Build all packages simultaneously
make package
```

---

[Back to Getting Started](@ref getting_started) | [Next: CLI Reference &rarr;](@ref cli_reference)
