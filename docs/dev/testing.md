# Testing Infrastructure & Quality Gates {#dev_testing}

Condor includes a standalone, self-contained unit testing framework requiring zero external test libraries (such as GoogleTest or Catch2). The test framework is implemented in `tests/test_main.cpp`.

---

## 1. Test Harness Architecture

The test harness uses a macro-based registration pattern:

```cpp
// tests/test_main.cpp
struct TestCase {
    std::string name;
    std::function<void()> func;
};

std::vector<TestCase>& get_test_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

void register_test(const std::string& name, std::function<void()> func) {
    get_test_registry().push_back({name, std::move(func)});
}

#define TEST_CASE(name) \
    void test_##name(); \
    struct Register_##name { \
        Register_##name() { register_test(#name, test_##name); } \
    } global_register_##name; \
    void test_##name()
```

Each test file registers tests globally at initialization time. Assertion failures trigger immediate exit with code `1` and descriptive messages:
- `ASSERT_TRUE(cond)`
- `ASSERT_FALSE(cond)`
- `ASSERT_EQ(a, b)`

---

## 2. Unit Test Catalog (23 Test Suites)

The unit test suite consists of 23 targeted test cases across 6 test modules:

| Test File | Test Case Name | Functionality Verified |
| :--- | :--- | :--- |
| `test_validation.cpp` | `identifier_validation` | Validates regex `^[a-zA-Z0-9_-]+$` on valid and invalid identifiers. |
| | `invalid_stage_name_yaml` | Ensures YAML parser rejects stage names with illegal characters (`"test stage"`). |
| | `dag_cycle_detection` | Verifies DFS cycle detector detects cyclic dependencies (`A -> B -> C -> A`). |
| | `dag_unknown_dependency`| Verifies parser rejects stages depending on non-existent stages. |
| | `seed_and_seeds_rejection` | Enforces rejection of unsupported seed properties. |
| | `matrix_expansion_and_defaults` | Tests matrix parameter sweep job expansion and defaults inheritance. |
| | `count_expansion` | Tests `count: 5` matrix job replication with sequential indices (`0001` to `0005`). |
| | `variable_interpolation` | Tests `${VAR}`, `$VAR`, and `${VAR:-default}` string replacements. |
| | `variable_cli_definitions`| Tests CLI variable overriding (`--define-variable KEY=VAL`). |
| | `variable_env_export` | Tests environment conversion to `CONDOR_VAR_<KEY>`. |
| | `variable_invalid_name` | Ensures invalid variable identifiers throw `ConfigurationError`. |
| `test_expression.cpp` | `expression_empty` | Verifies empty expressions evaluate to `true`. |
| | `expression_comparisons`| Verifies `==`, `!=`, `<`, `<=`, `>`, `>=` on strings and numbers. |
| | `expression_numeric_and_logical` | Verifies compound boolean expressions (`&&`, `||`, `!`). |
| `test_workspace.cpp` | `workspace_folder_format`| Validates timestamped folder formatting `YYYYMMDD_HHMMSS_<rand>`. |
| | `workspace_initialization_and_symlink` | Verifies directory creation and atomic `latest` symlink swap. |
| `test_scheduler.cpp` | `scheduler_local_execution` | Tests end-to-end job submission, execution, exit code capture, and log generation. |
| | `scheduler_allow_failure`| Tests `allow_failure: true` allowing pipeline completion despite job failures. |
| `test_pipeline.cpp` | `pipeline_variables_and_environment` | Tests multi-stage execution, DAG edge satisfaction on skipped stages, and HTML report skip counters. |
| | `per_stage_job_id_sequence` | Verifies per-stage sequential job ID resetting (`0001`, `0002`). |
| | `output_mode_yaml_parsing`| Tests parsing `output_mode` settings (`all`, `failures`, `summary`). |
| | `build_dir_and_test_dir_environment` | Tests build directory discovery and artifact hand-off (`${BUILD_DIR}`). |
| | `relative_paths_configuration` | Tests `path_style: relative` mode, validating relative paths (`.`, `..`, `../..`) in `job_runner.sh` and `job_status.yaml`. |

---

## 3. Running Test Suites

```bash
# Run tests via CTest
make test

# Run test binary directly with full verbose output
make test-verbose
```
