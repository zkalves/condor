# Pipeline YAML Specification {#pipeline_spec}

The Condor pipeline configuration file is a declarative YAML document defining execution settings, variables, global defaults, stages, job dependencies, test sweeps, and backend targets.

---

## Top-Level Master Schema

Below is the generic master YAML template illustrating every supported section, property, and hierarchy level in Condor. Placeholders formatted as `<...>` indicate values you customize; allowed values, defaults, and usage details are summarized in the inline comments and detailed in the reference tables below:

```yaml
# yaml-language-server: $schema=res/schema/condor-pipeline-schema.json
# ==============================================================================
# Condor Pipeline Configuration - Generic Master Template
# ==============================================================================

# Pipeline project name (optional, string, default: "pipeline")
name: "<pipeline_name>"

# Top-level external YAML file inclusion (optional: single path string OR sequence of paths)
# Single file:    include: "<path/to/included_file.yaml>"
# Multiple files: include: ["<path_1.yaml>", "<path_2.yaml>"]
include: "<path/to/included_file.yaml>"

# Global application-independent variables (optional, mapping of string -> string/number/bool)
# Interpolated via ${KEY} in commands, args, and hooks; exported as CONDOR_VAR_<KEY>
variables:
  <VAR_NAME_1>: "<string_value>"
  <VAR_NAME_2>: <number_or_boolean>

# Execution engine and runtime scheduler settings (optional, mapping)
settings:
  max_parallel_jobs: <int>           # Max concurrent slots (0 = auto CPU cores; default: 0)
  base_output_dir: "<path>"          # Base folder for timestamped run directories (default: "./condor_runs")
  run_dir: "<path>"                  # Fixed output directory override (disables timestamping)
  on_failure_action: "<action>"      # "abort" (default) | "continue" | "stage_barrier"
  exit_zero_on_failure: <bool>       # true | false (default: false)
  status_interval_sec: <int>         # Periodic status dump interval in seconds (default: 30)
  backend: "<backend_name>"          # "local" (default) | "slurm" | "lsf" | "sge" | "pbs" | etc.
  output_mode: "<mode>"              # "all" (default) | "failures" | "summary"
  path_style: "<style>"              # "absolute" (default) | "relative"
  dry_run: <bool>                    # true | false (default: false)
  no_interactive: <bool>             # true | false (default: false)
  verbose_logging: <bool>            # true | false (default: false)
  count_multiplier: <int>            # Multiplier for scalable job counts (default: 1)

# Global pipeline lifecycle hooks (optional, mapping)
# Runs once before the first stage starts and after all stages conclude
hooks:
  # Hook commands: supports single string command OR sequence of string commands
  # Single command:  pre: "<command>"
  # Multiple (list): pre: ["<cmd_1>", "<cmd_2>"]
  pre: "<command>"
  post: "<command>"
  always: <bool>                     # true (runs even on failure) | false (default: false)
  allow_failure: <bool>              # true (errors ignored) | false (default: false)

# Unified hierarchical defaults (optional, mapping)
defaults:
  # Defaults inherited by all stages across the pipeline
  stage:
    hooks:
      pre: "<command>"               # Command(s) executed before stage starts (string or sequence)
      post: "<command>"              # Command(s) executed after stage finishes (string or sequence)
      always: <bool>                 # true | false (default: false)
      allow_failure: <bool>          # true | false (default: false)

  # Defaults inherited by all jobs across the pipeline
  job:
    command: "<command>"             # Fallback command line for jobs
    args: "<args>"                   # Fallback arguments appended to command
    append_args: <bool>              # true (appends args) | false (default: false, overrides)
    scalable: <bool>                 # true (default, scales with count multiplier) | false (pinned count)
    variables:                       # Default job variables
      <VAR_NAME>: "<value>"
    hooks:                           # Default job lifecycle hooks (pre/post accept string or sequence)
      pre: "<command>"
      post: "<command>"
      always: <bool>
      allow_failure: <bool>

# Custom template execution backends (optional, mapping of backend_name -> backend_config)
backends:
  <custom_backend_name>:
    type: "<type>"                   # "template" (default) | "local"
    description: "<description>"     # Human-readable backend description
    submit_command: "<command>"      # Submission template (e.g. "sbatch --parsable {script}")
    poll_command: "<command>"        # Status query template (e.g. "squeue -h -j {grid_id}")
    cancel_command: "<command>"      # Cancellation template (e.g. "scancel {grid_id}")
    job_id_regex: "<regex>"          # Regex capturing remote cluster job ID from submit output
    success_pattern: "<regex>"       # Regex matching successful job state
    failure_pattern: "<regex>"       # Regex matching failed job state
    running_pattern: "<regex>"       # Regex matching active/pending job state
    empty_poll_means_done: <bool>    # true (default) | false

# List of stages in the DAG (REQUIRED, sequence of StageConfig, minItems: 1)
stages:
  - name: "<stage_name>"             # (REQUIRED) POSIX-safe identifier (^[a-zA-Z0-9_-]+$)
    enabled_if: "<expression>"       # (Optional) Condition expression (e.g. "${VAR} == 'value'")
    allow_failure: <bool>            # (Optional) true | false (default: false)
    # Stage dependencies (optional: single stage string OR sequence of stages)
    # Single dependency:   depends_on: "<prerequisite_stage>"
    # Sequence of stages:  depends_on: ["<stage_1>", "<stage_2>"]
    depends_on:
      - "<prerequisite_stage_1>"
      - "<prerequisite_stage_2>"
    # Stage-scoped job defaults (optional: configures command, args, hooks for jobs in this stage)
    defaults:
      command: "<command>"           # Default command for jobs in this stage (overrides defaults.job.command)
      args: "<args>"                 # Stage arguments (appended if append_args: true; overrides if false)
      append_args: <bool>            # true (appends to pipeline args) | false (default: false, overrides)
      scalable: <bool>               # true (default) | false (pinned count)
      variables:
        <VAR_NAME>: "<value>"
      hooks:
        pre: "<command>"
        post: "<command>"
        always: <bool>
        allow_failure: <bool>
    # Stage modular job inclusion (optional: single path string OR sequence of paths)
    # Single file:    include: "<path/to/jobs.yaml>"
    # Multiple files: include: ["<jobs_1.yaml>", "<jobs_2.yaml>"]
    include: "<path/to/stage_jobs.yaml>"
    hooks:                           # (Optional) Stage-level lifecycle hooks (pre/post accept string or sequence)
      pre: "<command>"
      post: "<command>"
      always: <bool>
      allow_failure: <bool>
    jobs:                            # Sequence of jobs executed in this stage (or provided via include)
      - name: "<job_name>"           # (Optional) Job name (default: "job_<job_id>")
        command: "<command>"         # (Optional) Shell command line to execute (inherits stage/pipeline defaults)
        args: "<args>"               # (Optional) Arguments (appends to defaults if append_args: true; overrides if false)
        append_args: <bool>          # (Optional) true (appends to inherited args) | false (overrides args)
        enabled_if: "<expression>"   # (Optional) Job-level condition expression
        allow_failure: <bool>        # (Optional) true | false (default: false)
        # Job dependencies (optional: single string ID OR sequence of IDs)
        # Single dependency:   depends_on: "<job_id>"
        # Sequence of IDs:     depends_on: ["<job_1>", "<stage/job_2>"]
        depends_on:
          - "<job_id>"               # Intra-stage ID, e.g. "0001"
          - "<stage_name/job_id>"    # Cross-stage reference, e.g. "stage_a/0002"
        hooks:                       # (Optional) Job-level lifecycle hooks (pre/post accept string or sequence)
          pre: "<command>"
          post: "<command>"
          always: <bool>
          allow_failure: <bool>
        variables:                   # (Optional) Job-scoped variables
          <VAR_NAME>: "<value>"
        count: <int>                 # (Optional) Replicates job N times with index ${INDEX} (>= 1)
        scalable: <bool>             # (Optional) true (default: scales with CLI multiplier) | false (pinned count)
        # Matrix parameter sweep (optional: mapping of variable names to lists of values; generates Cartesian product)
        matrix:
          <VAR_NAME_1>: ["<val_1>", "<val_2>"]
          <VAR_NAME_2>: [<number_1>, <number_2>]
        # Modular job inclusion (optional: single path string OR sequence of paths)
        # Single file:    include: "<path/to/suite.yaml>"
        # Multiple files: include: ["<suite_1.yaml>", "<suite_2.yaml>"]
        include: "<path/to/job_suite.yaml>"
```

---

## Option Value Formats: Lists, Maps & Single Arguments

Every property in the Condor pipeline specification falls into one of four distinct structural formats:

1. **Dual-Format Options** (accepts either a single value OR a list of values)
2. **Lists Only** (strictly YAML sequences)
3. **Maps Only** (strictly key-value mappings / dictionaries)
4. **Single Arguments Only** (strictly scalar values: strings, numbers, or booleans)

### Structural Cardinality Reference Table

| Configuration Property | YAML Path / Location | Structural Format | Allowed YAML Shapes |
| :--- | :--- | :--- | :--- |
| `include` | `include`, `stages[].include`, `jobs[].include` | **Single Value OR List** | Single file path string OR sequence of path strings |
| `depends_on` | `stages[].depends_on`, `jobs[].depends_on` | **Single Value OR List** | Single identifier string OR sequence of identifier strings |
| `pre` / `post` | `hooks.pre`, `hooks.post`, `*.hooks.pre`, `*.hooks.post` | **Single Value OR List** | Single command string OR sequence of command strings |
| `stages` | `stages` | **List Only** | Sequence of stage mapping objects (`minItems: 1`) |
| `jobs` | `stages[].jobs`, included job files | **List Only** | Sequence of job mapping objects |
| `variables` | `variables`, `*.variables` | **Map Only** | Key-value dictionary of string keys to scalar values |
| `matrix` | `jobs[].matrix` | **Map Only** | Dictionary mapping variable names to lists of values for parameter sweeps |
| `settings` | `settings` | **Map Only** | Dictionary of runtime execution settings |
| `defaults` | `defaults`, `stages[].defaults` | **Map Only** | Dictionary of stage/job defaults (`command`, `args`, `append_args`, `scalable`, `variables`, `hooks`) |
| `backends` | `backends` | **Map Only** | Dictionary of `<backend_name>:` to backend template objects |
| `hooks` | `hooks`, `*.hooks` | **Map Only** | Dictionary containing `pre`, `post`, `always`, `allow_failure` |
| `name` | `name`, `stages[].name`, `jobs[].name` | **Single Argument Only** | Single string identifier |
| `command` | `jobs[].command`, `stages[].defaults.command`, `defaults.job.command` | **Single Argument Only** | Single shell command line string |
| `args` | `jobs[].args`, `stages[].defaults.args`, `defaults.job.args` | **Single Argument Only** | Single argument line string |
| `append_args` | `jobs[].append_args`, `stages[].defaults.append_args`, `defaults.job.append_args` | **Single Argument Only** | Single boolean (`true` or `false`) |
| `scalable` | `jobs[].scalable`, `stages[].defaults.scalable`, `defaults.job.scalable` | **Single Argument Only** | Single boolean (`true` or `false`) |
| `enabled_if` | `stages[].enabled_if`, `jobs[].enabled_if` | **Single Argument Only** | Single expression string |
| `count` | `jobs[].count` | **Single Argument Only** | Single positive integer (`>= 1`) |
| `count_multiplier` | `settings.count_multiplier` | **Single Argument Only** | Single positive integer (`>= 1`) |
| `allow_failure` | `stages[].allow_failure`, `jobs[].allow_failure`, `hooks.allow_failure` | **Single Argument Only** | Single boolean (`true` or `false`) |
| `always` | `hooks.always` | **Single Argument Only** | Single boolean (`true` or `false`) |
| `backend` | `settings.backend` | **Single Argument Only** | Single string identifying a registered backend |
| `output_mode` | `settings.output_mode` | **Single Argument Only** | Single enum string: `"all"`, `"failures"`, `"summary"` |
| `path_style` | `settings.path_style` | **Single Argument Only** | Single enum string: `"absolute"`, `"relative"` |
| `max_parallel_jobs`| `settings.max_parallel_jobs` | **Single Argument Only** | Single non-negative integer (`>= 0`) |
| `status_interval_sec`| `settings.status_interval_sec` | **Single Argument Only** | Single positive integer (`>= 1`) |
| `base_output_dir` | `settings.base_output_dir` | **Single Argument Only** | Single filesystem path string |
| `run_dir` | `settings.run_dir` | **Single Argument Only** | Single filesystem path string |
| `on_failure_action`| `settings.on_failure_action` | **Single Argument Only** | Single enum string: `"abort"`, `"continue"`, `"stage_barrier"` |
| `exit_zero_on_failure`| `settings.exit_zero_on_failure` | **Single Argument Only** | Single boolean (`true` or `false`) |
| `dry_run` | `settings.dry_run` | **Single Argument Only** | Single boolean (`true` or `false`) |
| `no_interactive` | `settings.no_interactive` | **Single Argument Only** | Single boolean (`true` or `false`) |
| `verbose_logging` | `settings.verbose_logging` | **Single Argument Only** | Single boolean (`true` or `false`) |
| Backend properties | `backends.<name>.*` | **Single Argument Only** | Single template strings or booleans (`submit_command`, `poll_command`, `cancel_command`, etc.) |

---

### 1. Dual-Format Options: Single Value vs. List Syntaxes

The following 3 properties natively accept **either** a single argument or a list (in both block bullet `- ` style and inline flow `[...]` style):

#### A. include (Root, Stage, and Job levels)
Use a single string to include one file, or a list to include multiple files:

```yaml
# 1. Single file argument:
include: "backends.yaml"

# 2. List of files (bulleted block style):
include:
  - "variables.yaml"
  - "backends.yaml"
  - "common_stages.yaml"

# 3. List of files (inline flow style):
include: ["variables.yaml", "backends.yaml", "common_stages.yaml"]
```

#### B. depends_on (Stage and Job levels)
Use a single string to depend on one stage/job, or a list to declare multiple prerequisites:

```yaml
# Stage dependency: single prerequisite
stages:
  - name: test
    depends_on: compile   # or "compile"

# Stage dependency: multiple prerequisites
stages:
  - name: package
    depends_on:
      - lint
      - unit_tests
      - integration_tests
    # Or inline: depends_on: [lint, unit_tests, integration_tests]

# Job dependency: single intra-stage ID
jobs:
  - name: step_b
    depends_on: "0001"

# Job dependency: multiple prerequisites (combining intra-stage and cross-stage)
jobs:
  - name: probe_service
    depends_on:
      - "0001"
      - "setup_stage/0002"
    # Or inline: depends_on: ["0001", "setup_stage/0002"]
```

#### C. pre and post Lifecycle Hooks
Use a single string command, or a list of shell commands executed sequentially:

```yaml
hooks:
  # 1. Single command string:
  pre: "echo 'Starting pipeline run'"

  # 2. Sequence of shell commands (executed one after another in order):
  post:
    - "echo 'Collecting test artifacts...'"
    - "tar -czf artifacts.tar.gz ./condor_runs/latest"
    - "./scripts/notify_slack.sh --status=${CONDOR_PIPELINE_STATUS}"

  # 3. Sequence in inline flow style:
  # pre: ["mkdir -p build", "cd build && cmake .."]
```

---

### 2. Lists Only (Sequences)

The following properties **must always be written as YAML sequences** (`- item` or `[item1, item2]`):

- **`stages:`**: Sequence of stage objects (must contain at least one stage).
  ```yaml
  stages:
    - name: build
      jobs:
        - name: compile
          command: "make build"
    - name: test
      jobs:
        - name: unit_tests
          command: "make test"
  ```
- **`jobs:`**: Sequence of job objects under a stage or within an included job file.
  ```yaml
  jobs:
    - name: unit_tests
      command: "./run_unit"
    - name: e2e_tests
      command: "./run_e2e"
  ```

---

### 3. Maps Only (Key-Value Dictionaries)

The following properties **must always be written as YAML key-value mappings** (`key: value`):

- **`variables:`**: Key-value pairs of variable names to string, number, or boolean values.
- **`settings:`**: Key-value pairs of pipeline execution options.
- **`defaults:`**: Key-value mapping containing `stage:` and/or `job:` default sections.
- **`backends:`**: Key-value mapping of custom backend names to their definition blocks.
- **`hooks:`**: Key-value mapping containing hook configuration (`pre`, `post`, `always`, `allow_failure`).

---

### 4. Single Arguments Only (Scalars)

All other properties accept **strictly a single scalar value** (string, integer, or boolean):

- **Single Shell Commands**: `command` and `args` accept a single string, passed directly to the POSIX shell (`/bin/sh -c`). They do not accept lists of tokens; separate arguments with spaces inside the string (e.g. `args: "--verbose --threads=4"`).
- **Conditionals**: `enabled_if` accepts a single expression string evaluated by the expression engine (e.g. `enabled_if: "${coverage} == '1'"`).
- **Job Scaling**: `count` accepts a single integer `>= 1` (e.g. `count: 8`); `scalable` accepts a boolean (`true` or `false`).
- **Flags**: `allow_failure`, `always`, `exit_zero_on_failure`, `dry_run`, `no_interactive`, `verbose_logging` accept a single boolean literal (`true` or `false`).

---

## Complete Configuration Reference Tables

### 1. Root-Level Options

| Key | Type | Required | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `name` | string | Optional | `"pipeline"` | Unique project/workflow identifier displayed in summaries and reports. |
| `stages` | sequence of stages | **Required** | &mdash; | Ordered list of execution stage definitions in the DAG (minimum: 1). |
| `variables` | map | Optional | `{}` | Global key-value dictionary interpolated as `${KEY}` and exported to child environments. |
| `settings` | map | Optional | `{}` | Pipeline-wide execution options, scheduler controls, and backend configuration. |
| `defaults` | map | Optional | `{}` | Unified hierarchical defaults inherited by all stages (`defaults.stage`) and jobs (`defaults.job`). |
| `backends` | map | Optional | `{}` | Custom execution backend templates (e.g. Slurm, SGE, LSF, PBS, Spark). |
| `hooks` | map | Optional | `{}` | Global pipeline lifecycle hooks executed at the pipeline boundary. |
| `include` | string or sequence of strings | Optional | `[]` | External YAML files included into the root configuration scope. |

---

### 2. Execution Settings (settings:)

| Key | Type | Default | Allowed Values | Description | CLI Equivalent |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `max_parallel_jobs` | integer | `0` (CPU cores) | `0..N` | Maximum concurrent job execution slots. `0` auto-detects hardware concurrency. | `--max-parallel-jobs <N>` |
| `base_output_dir` | string | `"./condor_runs"` | Any path | Directory where timestamped run directories (`YYYYMMDD_HHMMSS_XXXXXX`) are created. | `--base-output-dir <path>` |
| `run_dir` | string | `""` | Any path | Overrides timestamping to execute directly inside an explicit fixed folder. | `--run-dir <path>` |
| `on_failure_action`| string | `"abort"` | `abort`, `continue`, `stage_barrier` | Failure policy when a job fails: abort immediately, continue independent branches, or barrier at stage. | `--on-failure-action <mode>` |
| `exit_zero_on_failure`| boolean | `false` | `true`, `false` | If `true`, Condor exits with return code `0` even if jobs or stages failed (ideal for CI pipelines). | `--exit-zero-on-failure` |
| `status_interval_sec` | integer | `30` | `1..N` | Seconds between periodic timestamped progress dumps in non-interactive / CI mode. | `--status-interval-sec <N>` |
| `backend` | string | `"local"` | Any registered backend | Execution backend identifier (`local`, `slurm`, `lsf`, `sge`, `gridengine`, `pbs`, `torque`, `spark`). | `-b`, `--backend <name>` |
| `output_mode` | string | `"all"` | `all`, `failures`, `summary` | Terminal output verbosity: `all` (full events), `failures` (only failing jobs and errors), `summary` (clean tracker). | `--output-mode <mode>` |
| `path_style` | string | `"absolute"` | `absolute`, `relative` | Path representation exported to child environments and log metadata. | `--path-style <mode>` |
| `dry_run` | boolean | `false` | `true`, `false` | If `true`, validates syntax, DAG dependencies, prints the execution plan, and exits without running jobs. | `--dry-run` |
| `no_interactive` | boolean | `false` | `true`, `false` | Disables raw ANSI terminal redraws and interactive menu polling for headless CI/CD runners. | `--no-interactive` |
| `verbose_logging`| boolean | `false` | `true`, `false` | Enables detailed internal scheduler state machine transitions and debugging logs. | `--verbose-logging` |
| `count_multiplier`| integer | `1` | `1..N` | Global multiplier for scalable job counts. | `--count-multiplier <N>` |

---

### 3. Stage Definition (stages:[])

| Key | Type | Required | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `name` | string | **Required** | &mdash; | Stage identifier. Must match POSIX-safe regex `^[a-zA-Z0-9_-]+$`. |
| `enabled_if` | string | Optional | `""` | Conditional evaluation expression (e.g. `"${coverage} == '1'"`). If false, stage is skipped. |
| `allow_failure`| boolean | Optional | `false` | If `true`, failures in this stage do not fail the overall pipeline or trigger abort policies. |
| `depends_on` | string or sequence of strings | Optional | `[]` | List of prerequisite stage names that must pass or be skipped before this stage starts. |
| `defaults` | map | Optional | `{}` | Stage-level job defaults (`command`, `args`, `append_args`, `scalable`, `variables`, `hooks`). |
| `hooks` | map | Optional | `{}` | Stage-level lifecycle hooks (`pre`, `post`, `always`, `allow_failure`). |
| `include` | string or sequence of strings | Optional | `[]` | External YAML file(s) containing job definitions to execute in this stage. |
| `jobs` | sequence of jobs | Optional | `[]` | Sequence of job definitions configured directly in this stage. |

---

### 4. Job Definition (jobs:[])

| Key | Type | Required | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `name` | string | Optional | `"job_<id>"` | Descriptive human-readable job name. Used as `${CONDOR_JOB_NAME}` and `${TEST_NAME}`. |
| `command` | string | Optional | Inherited | Shell command line executed by worker slots. Inherited from global or stage defaults. |
| `args` | string | Optional | Inherited | Arguments appended to the command line or exposed as `${ARGS}`. |
| `append_args` | boolean | Optional | Inherited | If `true`, appends `args` to parent arguments rather than overriding. |
| `scalable` | boolean | Optional | `true` | If `true`, count scales with CLI `--count-multiplier`. If `false`, count is pinned. |
| `enabled_if` | string | Optional | `""` | Job-level conditional expression. If false, job is marked `SKIPPED`. |
| `allow_failure`| boolean | Optional | `false` | If `true`, non-zero return code is marked `FAILED_ALLOWED` and does not trigger abort policies. |
| `depends_on` | string or sequence of strings | Optional | `[]` | Dependency references: Intra-stage job ID (`"0001"`), cross-stage key (`"stage/0002"`), or stage name. |
| `hooks` | map | Optional | Inherited | Job-level lifecycle hooks wrapping the primary command (`pre`, `post`, `always`, `allow_failure`). |
| `variables` | map | Optional | `{}` | Scoped variables exposed as `${KEY}` in command/hooks and exported to environment. |
| `count` | integer | Optional | `1` | Test scaling replication count (`>= 1`). Generates `N` replicated jobs (`<name>_0`, `<name>_1`, ...). |
| `matrix` | map | Optional | `{}` | Parameter sweep matrix across arbitrary variables (Cartesian product combinations). |
| `include` | string or sequence of strings | Optional | `[]` | Path or list of paths to external YAML files containing jobs to expand into this position. |

---

### 5. Lifecycle Hooks (HookConfig)

Hooks can be configured at:
- **Pipeline Level**: `hooks:`
- **Global Stage Defaults**: `defaults.stage.hooks:`
- **Global Job Defaults**: `defaults.job.hooks:`
- **Stage Level**: `stages[].hooks:`
- **Job Level**: `jobs[].hooks:`

| Key | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `pre` | string or sequence of shell commands | `""` | Command or list of shell commands executed sequentially before the entity starts. |
| `post` | string or sequence of shell commands | `""` | Command or list of shell commands executed sequentially after the entity concludes. |
| `always` | boolean | `false` | For post-hooks: If `true`, executes even if prior steps, commands, or jobs failed. |
| `allow_failure` | boolean | `false` | If `true`, a non-zero exit code from the hook logs a warning without failing the job, stage, or pipeline. |

---

### 6. Execution Backends (backends:)

| Key | Type | Required | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `type` | string | Optional | `"template"` | Backend execution type: `"local"` (direct POSIX fork/exec) or `"template"` (grid runners). |
| `description` | string | Optional | `""` | Human-readable description displayed when querying `condor --list-backends`. |
| `submit_command`| string | **Required** | &mdash; | Command template executed to submit a job to the cluster (placeholders: `{script}`, `{job_id}`, etc.). |
| `poll_command` | string | **Required** | &mdash; | Command template executed to poll a submitted job's status (placeholder: `{grid_id}`). |
| `cancel_command`| string | **Required** | &mdash; | Command template executed to cancel or kill a running job (placeholder: `{grid_id}`). |
| `job_id_regex` | string | Optional | `""` | Regex extracting the remote cluster job ID from submit output. Defaults to first token. |
| `success_pattern`| string | Optional | Auto-detect | Regex matching job completion (e.g. `COMPLETED|DONE|FINISHED`). |
| `failure_pattern`| string | Optional | Auto-detect | Regex matching job failure (e.g. `FAILED|CANCELLED|TIMEOUT|EXIT`). |
| `running_pattern`| string | Optional | Auto-detect | Regex matching active jobs (e.g. `RUNNING|PENDING|RUN|PEND|QUEUED`). |
| `empty_poll_means_done` | boolean | Optional | `true` | If `true`, empty poll output signifies that the job finished and cleared the queue. |

#### Backend Template Placeholders
- `{script}`: Full path to generated `job_runner.sh` script
- `{grid_id}`: Remote cluster job ID returned by submit command
- `{job_id}`: Sequential 4-digit job identifier (`0001`, `0002`, ...)
- `{job_name}`: Human-readable job name
- `{job_dir}`: Per-job dedicated output directory
- `{stage_name}`: Parent stage name
- `{stage_dir}`: Stage directory path
- `{run_dir}`: Active run root directory
- `${VAR}`: Any pipeline or host environment variable (e.g. `${QUEUE}`, `${SLURM_PARTITION}`)

---

### 7. Declarative JSON Schema & IDE Integration

Condor embeds a compiled [JSON Schema](https://json-schema.org/) Draft 7 specification into every build and installs it to:
```text
<PREFIX>/share/condor/schema/condor-pipeline-schema.json
```
The repository source is located at [`res/schema/condor-pipeline-schema.json`](file:///home/eze/projects/github/condor/res/schema/condor-pipeline-schema.json).

#### IDE Autocomplete, Tooltips & Real-Time Linting
Add the `$schema` directive modeline to the very top of your pipeline YAML file:

```yaml
# yaml-language-server: $schema=res/schema/condor-pipeline-schema.json
name: sample_pipeline
settings:
  max_parallel_jobs: 4
stages:
  - name: build
    jobs:
      - name: compile
        command: "make build"
```

* **VS Code**: Supported natively via the **Red Hat YAML** extension (`redhat.vscode-yaml`). Provides hover documentation, type checking, autocomplete for all properties and enums, and red-squiggle error highlighting.
* **Neovim**: Supported natively with `yamlls` (YAML Language Server).
* **JetBrains (CLion / PyCharm / IntelliJ)**: Automatically recognizes the `$schema` modeline or can be associated under **Settings &rarr; Languages & Frameworks &rarr; Schemas and DTDs &rarr; JSON Schema Mappings**.

#### In-Process Validation
During pipeline load (`condor run` or `condor --dry-run`), Condor evaluates the YAML document against the embedded schema before building the DAG. Any syntax error, misspelled property (e.g. `commnd: ...`), invalid enum value, or forbidden key generates an immediate `ConfigurationError` with exact JSON pointer paths and reasons.

---

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
    jobs:
      - name: make_build
        command: "make build"

  - name: unit_tests
    depends_on: [compile]
    jobs:
      - name: test_core
        command: "./run_core"
      - name: test_network
        command: "./run_net"

  - name: integration_tests
    depends_on: [compile]
    jobs:
      - name: test_end_to_end
        command: "./run_e2e"

  - name: package
    depends_on: [unit_tests, integration_tests]
    jobs:
      - name: make_package
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

## 4. Defaults & Argument Inheritance (defaults: and append_args:)

Condor eliminates boilerplate repetition across jobs by providing a clean, 3-tier hierarchical defaults system:

1. **Pipeline Global Defaults (`defaults.job`)**: Baseline `command`, `args`, `append_args`, `variables`, and `hooks` inherited across the entire pipeline.
2. **Stage-Level Defaults (`stages[].defaults`)**: Scoped defaults for jobs within a specific stage. Different stages typically execute different commands (e.g. `make` for build, `./sim` for simulation, `gcov` for reporting) without duplicating `command:` across individual jobs.
3. **Individual Job Configurations (`jobs[]`)**: Direct job-level options that override or append to inherited properties.

```
+-------------------------------------------------------------+
| TIER 1: Pipeline Global (defaults.job)                      |
| command: "<global_command>"                                 |
| args: "<global_args>"                                       |
| append_args: <bool>                                         |
+------------------------------+------------------------------+
                               |
                               v
+-------------------------------------------------------------+
| TIER 2: Stage Scoped (stages[].defaults)                    |
| command: "<stage_command>"   (overrides Tier 1 command)     |
| args: "<stage_args>"         (appends if Tier 2 append_args)|
| append_args: <bool>                                         |
+------------------------------+------------------------------+
                               |
                               v
+-------------------------------------------------------------+
| TIER 3: Individual Job (stages[].jobs[])                    |
| command: "<job_command>"     (overrides Tier 2 command)     |
| args: "<job_args>"           (appends if Tier 3 append_args)|
| append_args: <bool>                                         |
+-------------------------------------------------------------+
```

### Argument Resolution & append_args

By default (`append_args: false`), specifying `args:` at a more specific level completely **overrides** the arguments inherited from higher levels.

When `append_args: true` is configured (either globally, per-stage, or per-job), arguments are **concatenated with a space**:
- `final_args = "<inherited_args> <child_args>"`
- A stage with `append_args: true` appends its `args` to `defaults.job.args`.
- A job with `append_args: true` appends its `args` to the stage's resolved arguments.
- Any child level can opt out of appending by specifying `append_args: false`.

### Automatic Command Argument Appending

Condor handles the injection of resolved arguments into the shell command automatically:
1. **Explicit `${ARGS}` Placeholder**: If `command` explicitly contains `${ARGS}` (e.g., `"pytest ${ARGS} --report"`), Condor replaces `${ARGS}` in place.
2. **Auto-Append**: If `command` does **not** contain `${ARGS}`, Condor automatically appends the resolved arguments to the end of the command:
   ```text
   "<command> <final_args>"
   ```
   Users do not need to add `${ARGS}` boilerplate to their command strings.

### 3-Tier Hierarchy Example

```yaml
defaults:
  job:
    command: "<base_runner>"
    args: "--verbose --log-level=info"
    append_args: true                # Global policy: append arguments down the hierarchy
    hooks:
      pre: "echo 'Starting job ${CONDOR_JOB_NAME}'"

stages:
  # Stage 1: Build stage using make
  - name: build_stage
    defaults:
      command: "make"                # Overrides global command for all jobs in this stage
      args: "-j4"                    # Appends to global args -> "--verbose --log-level=info -j4"
      append_args: true
    jobs:
      - name: compile_core
        args: "TARGET=core"          # Result: "make --verbose --log-level=info -j4 TARGET=core"
      - name: compile_plugins
        args: "TARGET=plugins"       # Result: "make --verbose --log-level=info -j4 TARGET=plugins"

  # Stage 2: Simulation stage using a simulation binary
  - name: sim_stage
    depends_on: [build_stage]
    defaults:
      command: "./run_sim"           # Overrides command for simulation jobs
      args: "--gui=off"              # Opts out of global args; replaces with stage args
      append_args: false
    jobs:
      - name: sim_smoke
        args: "--test=smoke"
        append_args: true            # Appends to stage args -> "./run_sim --gui=off --test=smoke"
      - name: sim_regression
        args: "--test=regression --fast"
        append_args: true            # Appends to stage args -> "./run_sim --gui=off --test=regression --fast"
      - name: sim_custom
        command: "./run_custom_sim"  # Overrides command directly
        args: "--standalone"
        append_args: false           # Replaces args completely -> "./run_custom_sim --standalone"
```

---

## 5. Test Scaling, Multipliers & Matrix Sweeps (count, scalable, and matrix)

Condor provides domain-agnostic primitives for parallel scaling, parameter sweeps, and execution multipliers:

### Single Job Execution
Each job in a stage explicitly declares its name and command (or inherits defaults):
```yaml
stages:
  - name: build
    jobs:
      - name: compile
        command: "make build"
```

### Parallel Replication (count: N)
Replicate a test across `N` parallel worker slots with automatic index assignment (`${INDEX}` from `0` to `N - 1`):
```yaml
jobs:
  - name: stress_test
    count: 16                   # Dispatches 16 parallel jobs: stress_test_0 ... stress_test_15
```

### Scalability Control (scalable: <bool>) & Count Multipliers
In regression and CI/CD workflows, users often run quick smoke tests on pull requests (1x count) and soak tests on weekends (e.g., 5x or 10x count).

Condor supports scaling counts dynamically via CLI flag `--count-multiplier <int>` or YAML setting `settings.count_multiplier`:
* **`scalable: true` (default)**: The job count scales with the multiplier: `count = count * multiplier`.
* **`scalable: false`**: The job count is **pinned**. The multiplier is ignored, and the job runs exactly `count` times.

```yaml
jobs:
  # Fuzz test: scales when passing e.g. --count-multiplier 5 (10 * 5 = 50 runs)
  - name: fuzz_packet_parser
    count: 10
    scalable: true

  # Fixed topology test: always runs exactly 3 instances, never multiplied
  - name: three_node_consensus
    count: 3
    scalable: false
```

### Matrix Parameter Sweeps (matrix:)
Sweep across arbitrary lists of variables using `matrix:`. Condor computes the **Cartesian product** across all dimensions and dispatches 1 job instance per combination:

```yaml
jobs:
  # 1-Dimensional Sweep (e.g. reproducing specific targeted seeds)
  - name: repro_failing_seeds
    matrix:
      SEED: [42, 1337, 2024]
    command: "./sim --seed=${SEED}"

  # Multi-Dimensional Sweep (cross-compilation matrix: 2 x 2 = 4 jobs)
  - name: cross_compile
    matrix:
      ARCH: ["x86_64", "aarch64"]
      OPT: ["O0", "O3"]
    command: "gcc -${OPT} -march=${ARCH} -o bin/app_${ARCH}_${OPT} src/main.c"
```

Each matrix instance receives:
* The matrix variable values injected into `job.variables` (accessible via `${VAR}` and environment variables).
* `${INDEX}` representing the combination index (`0`, `1`, ...).
* A self-describing, POSIX-safe job name (e.g., `cross_compile_ARCH_x86_64_OPT_O0`).
* **Multiplier Immunity**: Discrete matrix combinations represent specific test targets and are **not** multiplied by `--count-multiplier`.

### Combining matrix: and count:
When both `matrix:` and `count: N` are specified on the same job, Condor replicates **each matrix combination** `N` times:

```yaml
jobs:
  - name: benchmark
    matrix:
      BACKEND: ["vulkan", "opengl"]
    count: 5                    # Runs 5 replicates of Vulkan and 5 replicates of OpenGL (10 total)
    scalable: true              # If --count-multiplier 2 is passed, runs 10 of each (20 total)
```

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

### Unified Defaults Hierarchy: defaults.stage vs. defaults.job

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
Any individual stage can override this default by specifying its own stage-level `hooks:` block.

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

Job hooks wrap individual job execution inside `${CONDOR_JOB_DIR}`. They can be configured per individual job or inherited across all jobs via `defaults.job.hooks`:

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

#### Valid YAML Metric File Example (test_metrics.yaml)

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

### Pipeline Backend Configuration (settings.backend:)

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
#### 2. Command-Line Override (-b / --backend)
You can override the pipeline backend globally at runtime without modifying the YAML file:

```bash
# Execute the entire workflow on the Slurm cluster
condor -p pipeline.yaml -b slurm

# Run the exact same pipeline locally for rapid testing and debug
condor -p pipeline.yaml -b local
```

#### Resolution Precedence
When determining the execution backend for pipeline execution, Condor evaluates configuration in the following strict order of precedence:

1. **CLI Flag**: `-b` / `--backend` / `--backend-override` (highest priority)
2. **Settings Configuration**: `settings.backend:`
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
