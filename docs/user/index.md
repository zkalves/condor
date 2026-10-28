# Condor User Guide {#user_guide}

Welcome to the **Condor** User Guide. **Condor** (**Con**current + **D**ependency + **Or**chestrator) is a modern C++20 orchestrator engineered for designing, scheduling, monitoring, and debugging parallel pipelines across local multicore workstations and distributed grid clusters.

---

## Recommended Learning Roadmaps

Depending on your role and operational requirements, follow these recommended reading paths:

- **🖥️ Hardware Verification & EDA Engineers**:
  1. [Getting Started](@ref getting_started) &mdash; Toolchain prerequisites, offline compilation, and quickstart.
  2. [Pipeline YAML Specification](@ref pipeline_spec) &mdash; Authoring stages, parameter sweeps (`matrix`, `count`), defaults, and reusable test suites (`include:`).
  3. [Examples Catalog](@ref examples_catalog) &mdash; EDA SystemVerilog ALU pipeline flow with Assertion & Coverage collection.
  4. [Interactive HTML Report](@ref html_report) &mdash; Analyzing per-stage test execution metrics, waveform artifacts, and logs.
  5. [System Architecture](@ref user_architecture) &mdash; Process isolation, build artifact hand-offs (`${BUILD_DIR}` &rarr; `${TEST_DIR}`), and relative path execution.

- **⚙️ DevOps & CI/CD Automation Engineers**:
  1. [Getting Started](@ref getting_started) &mdash; Headless CI configuration and non-interactive flags.
  2. [CLI Reference & Automation](@ref cli_reference) &mdash; Command-line options, failure policies (`abort`, `continue`, `stage_barrier`), terminal output filters, and variable overrides.
  3. [Pipeline YAML Specification](@ref pipeline_spec) &mdash; Custom execution backends (Slurm, SGE, LSF), conditional branches (`enabled_if`), and lifecycle hooks (`always: true`).
  4. [Interactive HTML Report](@ref html_report) &mdash; Archiving zero-dependency reports in CI artifacts and machine-readable metadata.
  5. [Examples Catalog](@ref examples_catalog) &mdash; Exploring the 6 canonical configuration templates.

---

## User Guide Chapters

- @subpage getting_started "Getting Started"
  Prerequisites, pre-built packages, toolchain dependencies, building from source (offline and custom flows), and initial pipeline validation.

- @subpage supported_os "Supported Operating Systems & Compatibility Matrix"
  Linux distribution compatibility baselines (Ubuntu, Debian, RHEL, Rocky, openSUSE, Arch, Alpine), glibc requirements, pre-built packages (.deb, .rpm, .tar.gz, Docker, Homebrew, Environment Modules), and per-system toolchain installation instructions.

- @subpage cli_reference "CLI Reference & Automation"
  Complete command-line syntax, options, failure handling modes, terminal output filters, path style overrides, and variable injection.

- @subpage pipeline_spec "Pipeline YAML Specification"
  In-depth reference for pipeline configuration syntax: variables, DAG dependencies, stage and global defaults, parameter sweeps, conditional execution, modular includes, lifecycle hooks, and custom template backends.

- @subpage examples_catalog "Examples Catalog"
  Walkthrough of the minimal complete set of 6 self-contained examples covering 100% of Condor features and execution patterns.

- @subpage html_report "Interactive HTML Execution Report"
  Guide to the zero-dependency, self-contained `report.html`: per-stage filtering, stage progression bars, machine metrics parsing, embedded log viewers, search, and URL deep-linking.

- @subpage user_architecture "System Architecture & Execution Engine"
  Overview of Condor's execution model: DAG scheduling, process group management, signal handling, working directory isolation, and relative path mechanics.
