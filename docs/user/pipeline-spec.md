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

## 7. Custom Execution Backends (`backends:`)

Define template-based backends to offload compute-heavy jobs to cluster schedulers (Slurm, Sun Grid Engine, LSF) or remote containers:

```yaml
backends:
  slurm_grid:
    type: template
    submit_command: "sbatch --parsable --job-name={job_name} {script}"
    poll_command: "squeue -j {grid_id} -h -o %T"
    cancel_command: "scancel {grid_id}"

stages:
  - name: cluster_simulations
    backend: slurm_grid         # Stage-level backend assignment
    jobs:
      - name: monte_carlo_sim
      - name: local_sanity_probe
        backend: local          # Job-level backend override to run locally
```

Supported template placeholders:
- `{script}`: Full path to the generated `job_runner.sh` script
- `{grid_id}`: Job ID returned by `submit_command`
- `{job_id}`: Sequential 4-digit Condor job identifier (`0001`)
- `{job_name}`: Configured job name
- `{job_dir}`: Job working directory
- `{stage_dir}`: Stage directory
- `{run_dir}`: Run directory
- `${VAR}`: Any pipeline or context environment variable
