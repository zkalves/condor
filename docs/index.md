<p align="center">
  <img src="docs/images/condor_logo.svg" alt="Condor logo" width="128" height="128" />
</p>

# Condor Documentation Portal

Welcome to the **Condor** documentation portal. **Condor** is a modern, high-performance parallel process and grid orchestrator built with Modern C++ (C++20). Engineered with an offline-first philosophy and zero external runtime dependencies, Condor executes directed acyclic graph (DAG) pipelines across local multicore CPUs and distributed grid clusters (Slurm, SGE, LSF) with strict process isolation, signal propagation control, real-time terminal progress monitoring, and interactive HTML reporting.

---

## 📚 Documentation Navigation

```text
docs/
├── user/                       # User Guide & Operational Manuals (Primary Source of Truth)
│   ├── index.md                # User Guide top-level hub & role-based roadmaps
│   ├── getting-started.md      # Installation, compilation, and quickstart workflows
│   ├── supported-os.md         # Supported OS, compatibility matrix, and packages (.deb/.rpm)
│   ├── cli-reference.md        # Command-line options, failure policies, path styles, CI flags
│   ├── pipeline-spec.md        # Complete Pipeline YAML configuration specification
│   ├── examples-catalog.md     # Minimal complete set of 6 self-contained examples
│   ├── html-report.md          # Zero-dependency interactive HTML execution report
│   └── architecture.md         # DAG scheduler, POSIX process engine, signal isolation
│
└── dev/                        # Developer & C++ Architecture Guide (Doxygen Flow)
    ├── index.md                # Developer Guide top-level hub & build targets
    ├── architecture.md         # C++20 subsystem architecture and data flow diagrams
    ├── standards.md            # Coding standards, Modern C++ conventions, and doc invariants
    └── testing.md              # Test harness architecture and unit test suites
```

---

## 🧭 Documentation Portals

- @subpage user_guide "📘 Condor User Guide"
  Comprehensive operational manual for verification engineers, software developers, and DevOps teams. Contains role-based learning roadmaps and complete guides for pipeline YAML authoring, parameter sweeps (`seeds`, `count`), modular includes, failure policies, interactive runtime menu controls, and custom execution backends.

- @subpage dev_guide "🛠️ Condor Developer Guide"
  Comprehensive technical documentation for developers contributing to or extending **Condor**. Details the Modern C++20 subsystem architecture, DAG scheduler algorithms, memory layout, process execution engine, POSIX signal propagation, testing infrastructure, and automated Doxygen C++ API reference.

---

## 🚀 Key Highlights & Architectural Strengths

- **Offline-First Zero Runtime Dependencies**: Pure C++20 relying exclusively on standard POSIX system calls (`fork`, `execvp`, `waitpid`, `setpgid`, `kill`) and the standard C++ library. No Python, Node.js, or heavyweight runtime frameworks needed.
- **Strict Process Group & Working Directory Isolation**: Every individual job executes in a completely isolated folder (`<run_dir>/<stage>/<job_id>/`) within its own dedicated POSIX process group (`setpgid`), ensuring runaway processes are terminated cleanly without orphan leaks.
- **Configurable Relative & Absolute Paths**: Support for either absolute host paths or portable relative run paths (`CONDOR_JOB_DIR=.`, `CONDOR_RUN_DIR=../..`, `BUILD_DIR=../../build/0001`), allowing run folders to be archived, relocated, or reproduced offline.
- **Dynamic Terminal Width & Responsive Progress**: Real-time multi-bar ANSI progress visualizer with dynamic terminal width detection (`TIOCGWINSZ`), status summaries, and interactive keyboard menu (`p` pause, `r` resume, `k` kill, `q` quit).
- **Self-Contained Interactive HTML Report**: Automatically generates an embedded, offline-ready `report.html` with per-stage tabbed navigation, stage progression bars, custom machine metrics parsing, embedded log inspectors, search, and deep-linking.
- **Custom Template Backends**: Pluggable grid runner interface with parameter substitution (`{script}`, `{grid_id}`, `{job_dir}`) facilitating integration with Slurm (`sbatch`), Sun Grid Engine (`qsub`), LSF, or custom remote SSH/container executors.

---

## ⚡ Quick Start

```bash
# 1. Build Condor (offline flow, zero network required)
make build-offline

# 2. Run all automated unit test suites
make test-verbose

# 3. Validate pipeline configuration without executing
./build/condor examples/simple/scripts/pipeline.yaml --dry-run

# 4. Execute the pipeline
./build/condor examples/simple/scripts/pipeline.yaml

# 5. Run with summary terminal output and portable relative paths
./build/condor examples/verilator/scripts/pipeline.yaml --relative-paths --output-mode=summary
```

---

## 👤 Author & Project Repository

- **Author**: Ezequiel Alves ([@@zkalves](https://github.com/zkalves))
- **Email**: [alvesel@gmail.com](mailto:alvesel@gmail.com)
- **GitHub Page**: [https://github.com/zkalves/condor](https://github.com/zkalves/condor)
- **License**: GNU General Public License v3.0 (GPL-3.0) (see [`LICENSE`](file:///home/eze/projects/github/condor/LICENSE))
