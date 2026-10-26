# Condor Developer Guide {#dev_guide}

Welcome to the **Condor** Developer Guide. This guide covers internal subsystem architecture, Modern C++20 development practices, coding standards, test infrastructure, and the automated Doxygen C++ API reference for developers contributing to or maintaining **Condor**.

---

## Developer Guide Chapters

- @subpage dev_architecture "C++20 Subsystem Architecture"
  Detailed architectural walkthrough of the core C++ subsystems (Scheduler, Workspace Manager, YAML Parser, Expression Evaluator, Variable Manager, Execution Backends, and Reporting Engine).

- @subpage dev_standards "Code & Documentation Standards"
  Header documentation rules, Modern C++20 conventions, error handling paradigms, and the Documentation-Implementation Lockstep Invariant.

- @subpage dev_testing "Testing Infrastructure & Quality Gates"
  Lightweight standalone C++20 test framework architecture, test case registration macros, and complete unit test catalog.

---

## Automated Doxygen C++ API Reference

All class interfaces, data structures, method contracts, and namespaces are documented directly within the C++ headers (`include/condor/`) using Doxygen docstrings.

- [**C++ Class List (API Reference)**](annotated.html): Annotated index of all classes, structs, and type definitions.
- [**Class Inheritance Hierarchy**](hierarchy.html): Inheritance tree and relationship graphs for execution backends and exception hierarchies.
- [**Source Code File List**](files.html): Complete browsable directory of header and implementation files with syntax-highlighted source code.
- [**Global Enums & Functions**](globals.html): Global functions, enums (`JobStatus`, `FailureAction`, `OutputMode`, `PathStyle`), and helper utilities.

### Generating Doxygen Documentation Locally

```bash
# Build Doxygen documentation into _site/
make docs-doxygen

# Serve the documentation portal on http://localhost:8000
make docs-serve
```

---

## Development Environment & Build Targets

```bash
# Build in Debug mode with address sanitizers
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build -j$(nproc)

# Run test suite with verbose output
make test-verbose

# Validate execution against all 6 minimal examples
make dry-run
make run-simple
make run-conditional
make run-matrix
make run-failures
make run-backends
make run-verilator
```

---

## 👤 Author & Repository

- **Author**: Ezequiel Alves ([@@zkalves](https://github.com/zkalves))
- **GitHub Page**: [https://github.com/zkalves/condor](https://github.com/zkalves/condor)
- **Issue Tracker**: [https://github.com/zkalves/condor/issues](https://github.com/zkalves/condor/issues)
