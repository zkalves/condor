# Code & Documentation Standards {#dev_standards}

To maintain high code quality, security, and architectural integrity across **Condor**, all contributions must strictly adhere to the following development and documentation standards.

---

## C++ Code Documentation Standards

All public classes, methods, enums, structs, and interfaces must be documented directly in the C++ header files (`include/condor/`) using standard Doxygen docstrings.

Developers must follow this formatting convention:

```cpp
/**
 * @class ExampleManager
 * @brief Thread-safe manager governing resource lifecycle and configuration.
 *
 * Detailed documentation on synchronization guarantees, POSIX contracts,
 * and memory ownership semantics.
 */
class ExampleManager {
public:
    /**
     * @brief Submit an execution context to the worker pool.
     * @param ctx Execution context containing job metadata and paths.
     * @throws ExecutionError If process creation or directory change fails.
     */
    void submit(JobExecutionContext& ctx);

    /**
     * @brief Poll active status of a running execution context.
     * @param ctx Running context to inspect.
     * @return PollResult containing completion status and exit code.
     */
    PollResult poll(JobExecutionContext& ctx);
};
```

---

## C++20 Coding Conventions

1. **Zero External Runtime Dependencies**:
   Condor must never introduce external shared libraries, dynamic runtime dependencies, or interpreted runtime environments (Python, Node.js). Pure C++20 and standard POSIX system calls only.
2. **Memory Ownership**:
   - Prefer `std::unique_ptr` for exclusive resource ownership (`ExecutionBackend`, `ProgressDisplay`).
   - Use `std::string_view` for non-owning read-only string parameters.
   - Avoid raw pointers except for non-owning observe-only references.
3. **POSIX Process Group Safety**:
   - Every spawned child process must execute `setpgid(0, 0)` immediately following `fork()`.
   - Signals (`SIGTERM`, `SIGKILL`) must always be delivered to the process group (`kill(-pid, ...)`).
4. **Identifier Safety**:
   - All stage names, job names, and variable keys must be strictly validated against `^[a-zA-Z0-9_-]+$`.
   - Paths must never be passed to unquoted shell evaluations. Direct binary execution via `execvp` or explicit `bash job_runner.sh` invocations is mandatory.

---

## Documentation & Implementation Lockstep Invariant

To ensure that documentation and implementation never diverge, **Condor** enforces strict bidirectional synchronization:

1. **Documentation &rarr; Implementation**:
   - Any modification or addition to `docs/` must be accompanied by working, tested code in `src/`, `include/`, and `tests/`.
   - Specifications must never run ahead of working implementation.
2. **Implementation &rarr; Documentation**:
   - Any pull request or commit modifying CLI arguments (`src/main.cpp`), YAML settings (`src/parser.cpp`), environment variables (`src/backend.cpp`), or report layouts (`src/html_report.cpp`) must update the corresponding guides in `docs/user/` and `docs/dev/`.
3. **Verification**:
   - Tested automatically via `make test-verbose` and verified by building the documentation via `make docs-doxygen`.
