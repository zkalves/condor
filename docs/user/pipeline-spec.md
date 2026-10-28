# Pipeline YAML Specification {#pipeline_spec}

The Condor pipeline configuration file is a declarative YAML document defining execution settings, variables, global defaults, stages, job dependencies, test sweeps, and backend targets.

---

## Top-Level Schema

```yaml
name: <string>                  # Pipeline identifier (required)

variables:                      # Global variable declarations
  <KEY>: <VALUE>

settings:                       # Execution and scheduler settings
  max_parallel_jobs: <int>
  base_output_dir: <path>
  on_failure_action: <string>
  exit_zero_on_failure: <bool>
  status_interval_sec: <int>
  output_mode: <string>
  path_style: <string>
  backend: <string>             # Pipeline execution backend ("local", "slurm", "lsf", "sge", etc.)

defaults:                       # Unified defaults hierarchy
  stage:                        # Inherited by all stage boundaries
    hooks:
      pre: <commands>
      post: <commands>
      always: <bool>
      allow_failure: <bool>
  job:                          # Inherited by all jobs across the pipeline
    command: <string>
    args: <string>
    variables:
      <KEY>: <VALUE>
    hooks:
      pre: <commands>
      post: <commands>
      always: <bool>
      allow_failure: <bool>

backends:                       # Custom execution backends (e.g. Slurm, SGE, LSF)
  <backend_name>:
    type: template
    submit_command: <string>
    poll_command: <string>
    cancel_command: <string>

hooks:                          # Global pipeline lifecycle hooks
  pre: <commands>
  post: <commands>
  always: <bool>
  allow_failure: <bool>

stages:                         # List of pipeline execution stages
  - name: <string>
    depends_on: [<stage_name>, ...]
    enabled_if: <expression>
    allow_failure: <bool>
    include: <path>             # Modular external job suite
    defaults:
      ...
    jobs:
      - name: <string>
        ...
```

---

## 1. Settings Section (settings:)

Configure pipeline-wide runtime behavior:

```yaml
settings:
  max_parallel_jobs: 8          # Max concurrent worker slots (default: CPU cores)
  base_output_dir: "./runs"     # Output root for timestamped folders (default: ./condor_runs)
  on_failure_action: "abort"    # "abort", "continue", or "stage_barrier"
  exit_zero_on_failure: false   # Return 0 even if failures occur (useful for CI)
  status_interval_sec: 15       # CI mode status dump period in seconds
  output_mode: "all"            # "all", "failures", or "summary"
  path_style: "relative"        # "relative" (portable) or "absolute" (host paths)
  backend: "local"              # Pipeline execution backend ("local", "slurm", "lsf", "sge", etc.)
```

---

## 2. Variables & Expressions

### Variable Interpolation
Variables are declared in the `variables:` block and referenced via standard shell syntax:
- `${VAR}` or `$VAR`
- `${VAR:-default_value}` (fallback default if unset or empty)

Variables are available for string interpolation within commands and hooks, and are automatically exported into child environments as `CONDOR_VAR_<KEY>` and literal `<KEY>`.

### Conditional Expressions (enabled_if:)
Stages and individual jobs can be conditionally skipped using `enabled_if`:
- **Equality**: `${coverage} == '1'`, `${env} != 'prod'`
- **Numeric Comparisons**: `${tier} >= 2`, `${count} < 10`
- **Logical Connectives**: `(${tier} >= 1) && (${mode} == 'fast')`, `!(${skip})`

When a stage is skipped via `enabled_if`, it satisfies dependency edges for downstream stages without failing the pipeline.

---

## 3. Directed Acyclic Graph (DAG) Dependencies (depends_on:)

Condor structures pipelines into dependency graphs to ensure correct execution ordering while maximizing parallel throughput across worker slots.

### Stage-Level Dependencies
Stages execute in topological order according to their declared `depends_on:` list:

```yaml
stages:
  - name: compile
    command: "make build"

  - name: unit_tests
    depends_on: [compile]
    jobs:
      - name: test_core
      - name: test_network

  - name: integration_tests
    depends_on: [compile]
    jobs:
      - name: test_end_to_end

  - name: package
    depends_on: [unit_tests, integration_tests]
    command: "make package"
```

* **Cycle Detection**: Condor validates the entire dependency graph before execution during parsing and `--dry-run`. Circular dependencies trigger an immediate configuration error.
* **Skipped Stage Satisfaction**: When an intermediate stage is skipped via `enabled_if`, it automatically satisfies dependency edges for downstream stages without failing the pipeline.

### Job-Level Dependencies
In addition to stage boundaries, jobs can declare fine-grained dependencies across stages or within the same stage:

```yaml
stages:
  - name: prepare
    jobs:
      - name: setup_db     # Auto-assigned job_id: 0001
      - name: seed_data    # Auto-assigned job_id: 0002
        depends_on: ["0001"]

  - name: execution
    jobs:
      - name: probe_test
        # Cross-stage dependency targeting specific job in earlier stage
        depends_on: ["prepare/0002"]
      - name: independent_job
        # Dispatches immediately without waiting for prepare/0002
```

* **Exact Job Key**: Reference a job in another stage using `<stage_name>/<job_id>` (e.g. `prepare/0002`).
* **Intra-Stage Job ID**: Reference another job in the same stage by its 4-digit ID (e.g. `"0001"`).
* **Job Name Reference**: Reference a job by its configured `name:` string.

---

## 4. Hierarchical Defaults (defaults:)

Condor eliminates boilerplate repetition across jobs by providing a hierarchical two-tier defaults system:

1. **Global `defaults:`**: Applied across every stage and job in the pipeline.
2. **Stage `defaults:`**: Overrides or extends global defaults for all jobs within that specific stage.
3. **Job-Level Settings**: Explicit job definitions take precedence over all inherited defaults.

```yaml
defaults:
  command: "pytest tests/ --test=${TEST_NAME} ${ARGS}"
  variables:
    ARGS: "-v"
  hooks:
    pre: "echo 'Starting ${CONDOR_JOB_NAME}'"

stages:
  - name: test_suite
    defaults:
      variables:
        ARGS: "-v --tb=short"   # Overrides global ARGS for this stage
    jobs:
      - name: auth_tests
      - name: api_tests
      - name: heavy_tests
        command: "pytest tests/heavy/ -v" # Overrides default command
```

---

## 5. Test Scaling & Parameter Sweeps

Jobs support three distinct execution patterns:

### Single Command Shorthand
For stages executing only a single task, specify `command:` directly on the stage:
```yaml
stages:
  - name: build
    command: "make build"
```

### Parallel Replication (count: N)
Replicate a test across `N` parallel worker slots with automatic index assignment (`${INDEX}` from `1` to `N`):
```yaml
jobs:
  - name: stress_test
    count: 16                   # Dispatches 16 parallel jobs: stress_test_1 ... stress_test_16
```

### Parameter Sweeps (seed and seeds)
Assign specific random seeds or sweep multiple seeds:
```yaml
jobs:
  - name: fixed_seed_test
    seed: 42                    # Injects ${SEED} = "42"

  - name: sweep_test
    seeds: [100, 200, 300, 400] # Dispatches 4 jobs: sweep_test_seed_100 ... sweep_test_seed_400
```

@attention **Mutual Exclusivity**: `count` and `seed`/`seeds` cannot be combined on the same job definition.

---

## 6. Modular Test Suites (include:)

Split large test matrices into reusable, version-controlled YAML files:

```yaml
# pipeline.yaml
stages:
  - name: regression
    depends_on: [build]
    include: "regression_tests.yaml"
```

```yaml
# regression_tests.yaml
defaults:
  command: "${BUILD_DIR}/bin/sim --test=${TEST_NAME}"

jobs:
  - name: test_dma
  - name: test_uart
  - name: test_timer
```

Paths in `include:` resolve relative to the directory containing the pipeline YAML file.

---

## 7. Failure Handling & Fault Tolerance

Condor provides deterministic, fine-grained control over execution errors:

### Failure Policies (on_failure_action:)
Configured in `settings:` or overridden via `--on-failure-action`:
* **`abort` (Default)**: Immediately terminates all active jobs via `SIGTERM`/`SIGKILL` across worker slots and halts the pipeline.
* **`continue`**: Permits active jobs to finish. Downstream branches that do not depend on the failed stage continue executing. Only branches directly dependent on the failed stage are blocked.
* **`stage_barrier`**: Permits all currently running jobs within the active stage to complete, but prevents subsequent stages in the DAG from dispatching.

### Non-Fatal Job Tolerances (allow_failure:)
Mark experimental, flaky, or optional tasks with `allow_failure: true`:

```yaml
stages:
  - name: experimental_benchmarks
    allow_failure: true       # Stage-level: all jobs in stage are allowed to fail
    jobs:
      - name: unstable_probe

  - name: core_tests
    jobs:
      - name: critical_test
      - name: flaky_network_test
        allow_failure: true   # Job-level: failure does not block stage or trigger abort
```

When an allowed-failure job exits with a non-zero code, its status is recorded as `FAILED_ALLOWED`. It does not trigger abort policies, and downstream dependent stages can proceed.

### Forcing Exit Status Zero (exit_zero_on_failure:)
In CI/CD environments where pipeline failure is reported through artifact inspection rather than process exit codes:

```yaml
settings:
  exit_zero_on_failure: true  # Or via CLI flag --exit-zero-on-failure
```

Forces Condor to exit with return code `0` even when jobs fail, allowing CI upload steps to proceed.

---

## 8. Lifecycle Hooks (hooks:)

Condor provides native lifecycle hooks to execute deterministic setup and teardown logic at three distinct hierarchy levels:
1. **Pipeline Hooks**: Executed at the pipeline boundary before any stage begins and after all stages conclude.
2. **Stage Hooks**: Executed at the stage boundary before the first job begins and after all jobs in the stage terminate.
3. **Job Hooks**: Executed per-job directly inside the job's working directory (`${CONDOR_JOB_DIR}`) wrapping the primary command.

### Hook Configuration Schema

Hooks use a nested `hooks:` block with support for both single string commands and sequential command lists:

| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `pre` | String or List | `""` | Command(s) executed before execution begins. Can be a single shell string or a list of commands executed sequentially with `set -e`. |
| `post` | String or List | `""` | Command(s) executed after execution concludes. Can be a single shell string or a list of commands executed sequentially. |
| `always` | Boolean | `false` | For post-hooks: if `true`, executes even if prior steps, jobs, or stages failed. |
| `allow_failure` | Boolean | `false` | If `true`, a non-zero exit code from the hook is logged as a warning and tolerated without failing the stage, job, or pipeline. |

---

### Unified Defaults Hierarchy: `defaults.stage` vs. `defaults.job`

To keep the pipeline configuration clean, cohesive, and intuitive, Condor unifies all stage and job defaults under a single top-level `defaults:` block, while reserving top-level `hooks:` strictly for the whole-pipeline lifecycle:

```yaml
# 1. Pipeline-level lifecycle: runs once before pipeline starts and after all stages conclude
hooks:
  pre: "echo '[PIPELINE START] Verifying license servers and disk space...'"
  post: "python3 scripts/upload_ci_summary.py"
  always: true

# 2. Unified defaults: cleanly separates stage-level defaults from job-level defaults
defaults:
  # Defaults for every STAGE boundary across the pipeline (does NOT run on jobs)
  stage:
    hooks:
      pre:
        - "echo '[STAGE START] Initializing ${CONDOR_STAGE_NAME}...'"
        - "scripts/prep_stage_disk.sh"
      post:
        - "echo '[STAGE DONE] ${CONDOR_STAGE_NAME} finished with exit code ${CONDOR_EXIT_CODE}'"
      always: true
      allow_failure: false

  # Defaults for every JOB execution across the pipeline (does NOT run on stages)
  job:
    command: "./run_sim -test ${TEST_NAME} -seed ${SEED}"
    hooks:
      pre: "echo '[JOB PRE] Preparing ${CONDOR_JOB_DIR}'"
      post: "rm -rf *.tmp"
      always: true
      allow_failure: true
```

#### 1. Hooks for All Stages (but NOT Jobs)
Configure `defaults.stage.hooks`:
```yaml
defaults:
  stage:
    hooks:
      pre: "echo '[STAGE START] ${CONDOR_STAGE_NAME}'"
      post: "echo '[STAGE DONE] ${CONDOR_STAGE_NAME}'"
      always: true

stages:
  - name: rtl_synthesis
    jobs:
      - name: synth_top
        command: "./synth --top top_module"  # Inherits NO job hooks!

  - name: sim_regression
    depends_on: [rtl_synthesis]
    jobs:
      - name: test_alu
        command: "./sim --test alu"          # Inherits NO job hooks!
```
*Note*: For convenience, `stage_defaults: hooks: ...` is also accepted as a backwards-compatible alias. Any individual stage can override this default by specifying its own `hooks:` block.

---

#### 2. Hooks for All Jobs (but NOT Stages)
Configure `defaults.job.hooks` (or flat `defaults.hooks`):
```yaml
defaults:
  job:
    hooks:
      pre: "echo '[JOB PRE] Starting job #${CONDOR_JOB_ID}'"
      post: "rm -rf *.tmp"
      always: true

stages:
  - name: unit_tests
    # The stage itself has NO stage pre-hook or post-hook!
    jobs:
      - name: test_1
        command: "./run_test_1"  # Executes defaults.job.hooks inside test_1's directory
      - name: test_2
        command: "./run_test_2"  # Executes defaults.job.hooks inside test_2's directory
```

---

### Stage-Level Hooks

Stage hooks run once per stage boundary. They are ideal for downloading input assets, spinning up mock services, aggregating stage outputs, or cleaning temporary files:

```yaml
stages:
  - name: sim_tests
    hooks:
      pre:
        - "echo '[STAGE] Initializing test workspace for stage ${CONDOR_STAGE_NAME}...'"
        - "scripts/setup_stage_env.sh"
      post:
        - "python3 scripts/check_stage_coverage.py"
        - "echo '[STAGE] Stage ${CONDOR_STAGE_NAME} completed with exit code ${CONDOR_EXIT_CODE}'"
      always: true                  # Run stage post-hook even if simulation jobs failed
      allow_failure: false          # If pre-hook fails, abort stage and skip jobs
    jobs:
      - name: test_alu_directed
        command: "./run_sim -directed"
```

- **Pre-Hook Failure Policy**: If a stage pre-hook returns a non-zero exit code and `allow_failure: false`, the stage is immediately marked as `FAILED`, all jobs in the stage are skipped, and the pipeline failure policy (`on_failure_action: abort / continue / stage_barrier`) is enforced.
- **Log Capture**: Output from stage hooks is automatically saved to dedicated log files:
  - `<run_dir>/<stage_name>/pre_hook.log`
  - `<run_dir>/<stage_name>/post_hook.log`
  These logs are rendered inside the interactive HTML report (`report.html`).

---

### Job-Level Hooks & Defaults

Job hooks wrap individual job execution inside `${CONDOR_JOB_DIR}`. They can be configured per individual job or inherited across an entire stage via `defaults.hooks`:

```yaml
defaults:
  hooks:
    pre: "echo '[JOB PRE] Preparing ${CONDOR_JOB_DIR}'"
    post: "rm -f *.tmp"
    always: true                    # Clean up temp files even if the test fails
    allow_failure: true             # Cleanup failures do not fail the job

stages:
  - name: integration_tests
    jobs:
      - name: test_with_custom_hooks
        hooks:
          pre:
            - "mkdir -p inputs"
            - "cp ../data/input.bin inputs/"
          post:
            - "python3 scripts/validate_output.py"
          always: false
          allow_failure: false
        command: "./integration_runner --input inputs/input.bin"
```

- **Execution Context**: Job hooks execute inside `job_runner.sh` in the exact directory assigned to the job (`${CONDOR_JOB_DIR}`).
- **Log Capture**: Job hook output is captured into dedicated log files:
  - `${CONDOR_JOB_DIR}/pre_hook.log`
  - `${CONDOR_JOB_DIR}/post_hook.log`
  and displayed in the job's detail card in `report.html`.

---

### Pipeline Global Hooks

Pipeline-level hooks run at the global run directory root (`${CONDOR_RUN_DIR}`):

```yaml
hooks:
  pre:
    - "echo '[PIPELINE START] Checking cluster licenses and disk space...'"
    - "scripts/verify_prerequisites.sh"
  post:
    - "python3 scripts/notify_ci_webhook.py"
  always: true                      # Run post-hook notification even if pipeline encountered failures
  allow_failure: true               # Webhook failures do not fail the build
```

---

## 9. Machine Metrics & Custom Data Protocol (*.yaml)

Condor enables jobs and aggregation scripts to publish structured metrics back to the scheduler and HTML report without requiring external plugins, runtime agents, or C++ recompilation.

### Protocol Overview
1. **Emit**: Any job or post-processing step creates one or more `.yaml` files (e.g. `test_metrics.yaml`, `coverage.yaml`, `benchmark.yaml`) inside its dedicated working directory (`${CONDOR_JOB_DIR}`).
2. **Ingest**: At the end of execution, Condor automatically scans each job's directory for any regular file ending in `.yaml` (excluding the internal runtime record `job_status.yaml`) and parses all top-level key-value metric pairs.
3. **Display**: The extracted metrics are surfaced as interactive badge cards in the interactive HTML report (`report.html`) and aggregated in downstream summary scripts.
4. **Use Cases**:
   - **EDA Simulation**: Toggle, Line, Branch, Condition, and FSM coverage percentages.
   - **Verification**: Cycles simulated, assertions checked, transactions verified, seed numbers.
   - **Performance**: Latency, queries per second (QPS), peak memory RSS, duration.

---

### YAML Format Specification & Schema

Condor's metric parser utilizes a streaming, zero-dependency line scanner designed for maximum ingestion speed. The file format follows a strict yet simple contract:

| Specification Rule | Requirement | Notes & Details |
| :--- | :--- | :--- |
| **Directory Location** | `${CONDOR_JOB_DIR}` | Files must reside directly in the job directory (`<run_dir>/<stage_name>/<job_id>/`). |
| **File Extension** | `.yaml` (exact) | Files **must** have the `.yaml` extension (case-sensitive). Note: `.yml` is ignored by the scanner. |
| **Reserved Filenames** | `job_status.yaml` | `job_status.yaml` is reserved for internal Condor runtime status and is automatically excluded from metric ingestion. |
| **Multi-File Aggregation** | Supported | Multiple `.yaml` files in the same directory (e.g., `coverage.yaml` and `perf.yaml`) are discovered and aggregated into a single unified badge grid. |
| **Mapping Structure** | Flat `key: value` pairs | One key-value pair per line separated by a colon (`:`). Nested dictionaries and sequence lists are not converted to metric badges. |
| **Key Formatting** | `<key>:` | Leading and trailing whitespace (` ` and `\t`) is stripped. Keys should be concise, human-readable or `snake_case` identifiers. |
| **Value Formatting** | `: <value>` | Leading and trailing whitespace (` ` and `\t`) is stripped. Surrounding double quotes (`"`) are automatically stripped. |
| **Value Types** | Numbers, Strings, Units | Accepts integers (`100000`), floats (`98.4`), percentages (`98.4%`), units (`14.2s`, `256MB`), status codes (`PASSED`), or file paths (`cov/index.html`). |
| **Ignored Lines** | Blank lines, pure comments | Empty lines, whitespace-only lines, or comment lines without a colon (`:`) are safely ignored. Lines where either key or value trims to empty are discarded. |

#### Valid YAML Metric File Example (`test_metrics.yaml`)

```yaml
# test_metrics.yaml - Machine-readable execution metrics
test_name: "test_alu_directed"
seed: 42
status: "PASSED"
cycles_simulated: 100000
assertions_verified: 200000
simulation_time_ns: 2000000
line_coverage: "98.4%"
toggle_coverage: "94.2%"
fsm_coverage: "100.0%"
branch_coverage: "91.5%"
peak_memory_mb: 184
report_url: "cov_report/index.html"
```

#### Formatting Guidelines: Valid vs. Invalid Patterns

```yaml
# VALID: Flat key-value pairs (rendered as individual metric badge cards)
line_coverage: 98.4%
toggle_coverage: "94.2%"
cycles: 1000000

# INVALID / UNSUPPORTED for badge display:
# Nested maps and list sequences are not parsed into badges:
coverage:
  line: 98.4%       # Nested keys will not be correctly extracted
  toggle: 94.2%
test_list:
  - test_alu_basic  # List items are skipped
```

#### Emitting Metrics from Shell, Python, or C++

##### Shell Script
```bash
cat <<EOF > "${CONDOR_JOB_DIR}/coverage_metrics.yaml"
line_coverage: "${LINE_COV}%"
toggle_coverage: "${TGL_COV}%"
cycles_simulated: ${TOTAL_CYCLES}
status: "PASSED"
EOF
```

##### Python Script
```python
import os

job_dir = os.environ.get("CONDOR_JOB_DIR", ".")
with open(os.path.join(job_dir, "test_metrics.yaml"), "w") as f:
    f.write(f"cycles_simulated: {cycles}\n")
    f.write(f"assertions_verified: {assertions}\n")
    f.write(f"line_coverage: {line_cov:.1f}%\n")
    f.write('status: "PASSED"\n')
```

##### C++ Testbench
```cpp
std::ofstream yfs("test_metrics.yaml", std::ios::trunc);
yfs << "cycles_simulated: " << cycles << "\n"
    << "assertions_verified: " << assertions << "\n"
    << "status: \"PASSED\"\n";
```

See [Interactive HTML Report](@ref html_report) for detailed integration examples with EDA tools and simulation coverage engines.

---

## 10. Execution Backends & Grid Engine Family (backends:)

Condor provides an extensible, dynamic backend architecture. Backends are **not hardcoded in the C++ binaries**; instead, only `local` is the internal POSIX process execution engine, while all distributed grid schedulers are dynamically loaded from modular YAML definition files or pipeline specifications.

### Supported Distributed Grid Engines (Pre-configured)

Condor includes production-grade configuration definitions for industry-standard grid schedulers:

| Backend Identifier | Grid Engine / Cluster System | Command Ecosystem | Key Capabilities |
| :--- | :--- | :--- | :--- |
| **`slurm`** | **Slurm Workload Manager** | `sbatch`, `squeue`, `scancel` | High-performance computing clusters, `--parsable` ID extraction, batch job states |
| **`sge`** / **`gridengine`** | **Grid Engine Family** (Sun Grid Engine / Altair Grid Engine / Open Grid Scheduler) | `qsub`, `qstat`, `qdel` | `-terse` submission ID parsing, queue status polling, job deletion |
| **`lsf`** | **IBM Spectrum LSF** (Load Sharing Facility) | `bsub`, `bjobs`, `bkill` | Semiconductor & EDA design clusters, `Job <ID>` regex parsing, `DONE`/`EXIT` status |
| **`pbs`** | **PBS Professional** (Portable Batch System) | `qsub`, `qstat`, `qdel` | Enterprise HPC workload scheduling, job state tracking (`job_state = F/R/Q`) |
| **`torque`** | **TORQUE Resource Manager** | `qsub`, `qstat`, `qdel` | PBS-derivative open-source resource manager |
| **`spark`** | **Apache Spark** (Distributed Data Grid Engine) | `spark-submit` | Cluster-mode distributed big-data driver execution, `driver-XXXX` status monitoring |

To list all available backends recognized on the system at any time, run:
```bash
condor --list-backends
```

---

### Pipeline Backend Configuration (`settings.backend:`)

The execution backend defines where and how commands are executed and monitored across the entire workflow. Condor treats the backend strictly as a **pipeline-level configuration** specified inside `settings:`.

To maintain architectural clarity and prevent mixed-environment configuration drift, `backend` cannot be set at the YAML top level, in stage definitions, or per individual job.

#### 1. Configuring the Pipeline Backend
Define `backend:` under `settings:`:

```yaml
name: chip_verification_pipeline

settings:
  backend: slurm                      # Pipeline backend: all jobs execute via Slurm
  max_parallel_jobs: 16

stages:
  - name: sim_smoke
    jobs:
      - name: test_boot
```
*(For backwards compatibility, `settings: default_backend: <name>` is also accepted).*

#### 2. Command-Line Override (`-b / --backend`)
You can override the pipeline backend globally at runtime without modifying the YAML file:

```bash
# Execute the entire workflow on the Slurm cluster
condor -p pipeline.yaml -b slurm

# Run the exact same pipeline locally for rapid testing and debug
condor -p pipeline.yaml -b local
```

Aliases accepted: `-b`, `--backend`, and `--backend-override`.

#### Resolution Precedence
When determining the execution backend for pipeline execution, Condor evaluates configuration in the following strict order of precedence:

1. **CLI Flag**: `-b` / `--backend` / `--backend-override` (highest priority)
2. **Settings Configuration**: `settings.backend:` (or `settings.default_backend:`)
3. **Built-in System Fallback**: `local` (POSIX direct execution engine)

---

### Backend Configuration File Search Paths

Condor locates backend configuration files by searching the following locations in order:

1. **Environment Variables**: `$CONDOR_BACKEND_DIR` or `$CONDOR_BACKEND_PATH` (colon-delimited paths)
2. **Pipeline Directory**: `<pipeline_dir>/backends/<name>.yaml` or `<pipeline_dir>/<name>.yaml`
3. **Current Working Directory**: `./backends/<name>.yaml` or `./res/backends/<name>.yaml`
4. **User Configuration**: `~/.config/condor/backends/<name>.yaml`
5. **Executable-Relative Share**: `<condor_bin_dir>/../share/condor/backends/<name>.yaml`
6. **System Standard Directories**: `/usr/local/share/condor/backends/`, `/usr/share/condor/backends/`, `/etc/condor/backends/`

---

### Custom Pipeline Backend Definition

You can define custom template backends or override existing configurations directly within pipeline YAML files:

```yaml
settings:
  backend: lsf_high_priority

backends:
  lsf_high_priority:
    type: template
    description: "LSF High-Priority EDA Queue"
    submit_command: "bsub -q priority -o {job_dir}/stdout.log -e {job_dir}/stderr.err < {script}"
    poll_command: "bjobs -noheader -o \"stat\" {grid_id}"
    cancel_command: "bkill {grid_id}"
    job_id_regex: "Job <([0-9]+)>|([0-9]+)"
    success_pattern: "DONE"
    failure_pattern: "EXIT|UNKWN|ZOMBI"
    running_pattern: "PEND|RUN|SSUSP|USUSP|WAIT"
    empty_poll_means_done: true

stages:
  - name: physical_synthesis
    jobs:
      - name: route_block_a
      - name: timing_closure
```

### Supported Configuration Attributes

| Attribute | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `type` | string | `template` | `local` for direct POSIX fork/exec, or `template` for external batch runners. |
| `description` | string | `""` | Human-readable description displayed in `--list-backends`. |
| `submit_command` | string | required | Command line executed to submit a job to the cluster. |
| `poll_command` | string | required | Command line executed to query job status. |
| `cancel_command` | string | required | Command line executed to terminate/kill a running job. |
| `job_id_regex` | string | `""` | Regular expression capturing the remote job ID from submit output. Defaults to first token. |
| `success_pattern`| string | auto | Regular expression matching job completion (`COMPLETED`, `DONE`, `FINISHED`). |
| `failure_pattern`| string | auto | Regular expression matching job failure (`FAILED`, `CANCELLED`, `TIMEOUT`, `EXIT`, `ERROR`). |
| `running_pattern`| string | auto | Regular expression matching active jobs (`RUNNING`, `PENDING`, `RUN`, `PEND`, `QUEUED`). |
| `empty_poll_means_done` | bool | `true` | If true, empty poll output signifies that the job finished and cleared the queue. |

### Supported Template Placeholders
- `{script}`: Full filesystem path to the generated `job_runner.sh` execution wrapper script
- `{grid_id}`: Remote cluster job ID returned by `submit_command`
- `{job_id}`: Sequential zero-padded 4-digit Condor job identifier (`0001`, `0002`, ...)
- `{job_name}`: Descriptive human-readable job name
- `{job_dir}`: Dedicated per-job working directory
- `{stage_name}`: Parent stage identifier
- `{stage_dir}`: Stage directory path
- `{run_dir}`: Active run root directory
- `${VAR}`: Any pipeline or host environment variable (e.g. `${SPARK_MASTER}`, `${QUEUE}`)
