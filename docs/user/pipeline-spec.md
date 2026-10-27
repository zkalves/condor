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
  default_backend: <string>

defaults:                       # Global defaults inherited by all stages and jobs
  command: <string>
  backend: <string>
  args: <string>
  variables:
    <KEY>: <VALUE>
  hooks:
    pre: <string>
    post: <string>
    always: <bool>

backends:                       # Custom execution backends (e.g. Slurm, SGE)
  <backend_name>:
    type: template
    submit_command: <string>
    poll_command: <string>
    cancel_command: <string>

hooks:                          # Global pipeline lifecycle hooks
  pre: <string>
  post: <string>
  always: <bool>

stages:                         # List of pipeline execution stages
  - name: <string>
    depends_on: [<stage_name>, ...]
    enabled_if: <expression>
    allow_failure: <bool>
    backend: <string>
    include: <path>             # Modular external job suite
    defaults:
      ...
    jobs:
      - name: <string>
        ...
```

---

## 1. Settings Section (`settings:`)

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
  default_backend: "local"      # Default backend identifier ("local" or custom backend)
```

---

## 2. Variables & Expressions

### Variable Interpolation
Variables are declared in the `variables:` block and referenced via standard shell syntax:
- `${VAR}` or `$VAR`
- `${VAR:-default_value}` (fallback default if unset or empty)

Variables are available for string interpolation within commands and hooks, and are automatically exported into child environments as `CONDOR_VAR_<KEY>` and literal `<KEY>`.

### Conditional Expressions (`enabled_if:`)
Stages and individual jobs can be conditionally skipped using `enabled_if`:
- **Equality**: `${coverage} == '1'`, `${env} != 'prod'`
- **Numeric Comparisons**: `${tier} >= 2`, `${count} < 10`
- **Logical Connectives**: `(${tier} >= 1) && (${mode} == 'fast')`, `!(${skip})`

When a stage is skipped via `enabled_if`, it satisfies dependency edges for downstream stages without failing the pipeline.

---

## 3. Hierarchical Defaults (`defaults:`)

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

## 4. Test Scaling & Parameter Sweeps

Jobs support three distinct execution patterns:

### Single Command Shorthand
For stages executing only a single task, specify `command:` directly on the stage:
```yaml
stages:
  - name: build
    command: "make build"
```

### Parallel Replication (`count: N`)
Replicate a test across `N` parallel worker slots with automatic index assignment (`${INDEX}` from `1` to `N`):
```yaml
jobs:
  - name: stress_test
    count: 16                   # Dispatches 16 parallel jobs: stress_test_1 ... stress_test_16
```

### Parameter Sweeps (`seed:` and `seeds: [...]`)
Assign specific random seeds or sweep multiple seeds:
```yaml
jobs:
  - name: fixed_seed_test
    seed: 42                    # Injects ${SEED} = "42"

  - name: sweep_test
    seeds: [100, 200, 300, 400] # Dispatches 4 jobs: sweep_test_seed_100 ... sweep_test_seed_400
```

> [!IMPORTANT]
> **Mutual Exclusivity**: `count` and `seed`/`seeds` cannot be combined on the same job definition.

## 5. Modular Test Suites (`include:`)

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

## 6. Lifecycle Hooks (`hooks:`)

Execute setup and teardown logic at three distinct levels:
1. **Pipeline Hooks**: Run before the entire pipeline starts and after all stages finish.
2. **Stage Hooks**: Run before the first job in a stage begins and after the last job completes.
3. **Job Hooks**: Wrapped directly into `job_runner.sh` around the job's command.

```yaml
hooks:
  pre: "echo '[PIPELINE START] Checking disk space...'"
  post: "python3 scripts/upload_metrics.py"
  always: true                  # Run post hook even if pipeline encountered failures
```

---

## 7. Execution Backends & Grid Engine Family (`backends:`)

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
    backend: lsf_high_priority    # Stage-level backend assignment
    jobs:
      - name: route_block_a
      - name: local_sanity_probe
        backend: local            # Job-level backend override to run locally
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
