# CLI Reference & Automation {#cli_reference}

Condor provides an intuitive, high-performance command-line interface designed for both interactive workstation use and automated headless CI/CD execution.

---

## Command Line Syntax

```bash
condor [OPTIONS] <pipeline.yaml>
```

---

## Command Line Options

| Option | Argument | Default | Description |
| :--- | :--- | :--- | :--- |
| `pipeline_file` | `<path>` | *(Required)* | Path to the pipeline YAML configuration file. Can be specified positionally or via `--pipeline-config`. |
| `--stage` | `<name>` | All stages | Execute only the specified stage. Must match POSIX identifier rules (`^[a-zA-Z0-9_-]+$`). |
| `--max-parallel-jobs` | `<N>` | Hardware concurrency | Maximum concurrent local jobs executed simultaneously. |
| `--base-output-dir` | `<path>` | `./condor_runs` | Base directory where timestamped run directories are created. |
| `--run-dir` | `<path>` | Auto-generated | Run inside an explicit fixed directory rather than generating a timestamped folder. |
| `--backend-override` | `<name>` | Pipeline default | Force all jobs to execute using the specified backend (e.g. `local` or a defined grid backend). |
| `--on-failure-action`| `abort`, `continue`, `stage_barrier` | `abort` | Override failure handling policy when a job fails. |
| `--exit-zero-on-failure`| None (Flag) | `false` | Return exit status code `0` even if individual jobs or stages encounter failures. |
| `--status-interval-sec`| `<seconds>` | `30` | Interval in seconds to output periodic status summary dumps in non-interactive CI mode. |
| `--output-mode` | `all`, `failures`, `summary` | `all` | Terminal event reporting verbosity. Alias: `--terminal-output`. |
| `--path-style` | `absolute`, `relative` | `absolute` | Path representation inside runs. Alias: `--path-mode`. |
| `--relative-paths` | None (Flag) | `false` | Convenience flag to enable relative paths inside runs (equivalent to `--path-style=relative`). |
| `--absolute-paths` | None (Flag) | `true` | Convenience flag to force absolute paths inside runs (equivalent to `--path-style=absolute`). |
| `--define-variable` | `<KEY=VAL>` | None | Define or override pipeline variables from the command line. Repeatable for multiple variables. |
| `--dry-run` | None (Flag) | `false` | Validate YAML syntax, verify stage names, check for DAG cycles, evaluate conditions, print plan, and exit. |
| `--no-interactive` | None (Flag) | `false` | Disable interactive stdin polling and ANSI progress bar redraws. Recommended for CI logs. |
| `--verbose-logging`| None (Flag) | `false` | Enable detailed scheduler debug logging to stderr. |
| `-h`, `--help` | None (Flag) | None | Display command-line usage information and exit. |

---

## Failure Handling Policies (`--on-failure-action`)

When a job fails without `allow_failure: true`, Condor enforces one of three deterministic failure actions:

1. **`abort` (Default)**:
   Immediately sends `SIGTERM` followed by `SIGKILL` to all currently running process groups across all worker slots. Queued and pending stages are aborted, and the pipeline halts immediately.
2. **`continue`**:
   Active jobs in the failed stage continue running. Unrelated, independent downstream stages that do not depend on the failed stage continue executing. Only branches directly dependent on the failed stage are blocked.
3. **`stage_barrier`**:
   Jobs currently running within the active stage are permitted to finish naturally. However, no subsequent stages in the DAG are allowed to dispatch.

---

## Terminal Output Modes (`--output-mode`)

Control terminal output verbosity according to your operational environment:

- **`all` (Default)**:
  Logs both start and finish events for every job, including exit codes, elapsed durations, and full interactive progress redraws.
- **`failures`**:
  Suppresses start notifications and successful completions. Only outputs errors and failing jobs.
- **`summary`**:
  Hides individual job events entirely. Displays only the overall multi-bar progress and the final execution summary block.

---

## Path Style Configuration (`--path-style`)

Condor allows choosing between absolute host paths and portable relative run paths:

```bash
# Relative paths mode
condor pipeline.yaml --relative-paths
```

In relative mode:
- `CONDOR_JOB_DIR` is exported as `.`
- `CONDOR_STAGE_DIR` is exported as `..`
- `CONDOR_RUN_DIR` is exported as `../..`
- `TEST_DIR` is exported as `.`
- `BUILD_DIR` is computed relative to the current job (e.g. `../../build_rtl/0001`)
- `job_status.yaml` records `working_directory: "."`

---

## Interactive Runtime Menu Controls

During live interactive execution on a TTY, Condor listens asynchronously on `stdin` for single-keystroke control commands without interrupting the running jobs:

| Keystroke | Action | Description |
| :---: | :--- | :--- |
| **`p`** | **Pause Dispatch** | Pauses scheduling of newly ready jobs. Currently running jobs continue to completion. |
| **`r`** | **Resume Dispatch** | Resumes scheduling queued jobs across available worker slots. |
| **`k`** | **Kill Pipeline** | Sends cancellation signals (`SIGTERM`/`SIGKILL`) to all running jobs and immediately halts. |
| **`q`** | **Quit Gracefully** | Stops dispatching new jobs and waits for currently running jobs to complete before exiting. |
| **`?`** or **`h`** | **Show Menu** | Displays interactive help menu overlay. |

> [!TIP]
> In automated CI/CD pipelines (such as GitHub Actions, GitLab CI, or Jenkins), pass `--no-interactive` to disable terminal polling and avoid consuming pipeline stdin.

## Built-in Environment Variables

Every job runner script (`job_runner.sh`) and hook script receives the following environment variables automatically exported by Condor:

| Variable | Description | Example (Relative Mode) | Example (Absolute Mode) |
| :--- | :--- | :--- | :--- |
| `CONDOR_RUN_DIR` | Top-level execution directory for the current run | `../..` | `/home/user/condor_runs/20261026_120000_123456` |
| `CONDOR_STAGE_NAME`| Name of the active stage | `sim_tests` | `sim_tests` |
| `CONDOR_STAGE_DIR` | Directory holding all jobs for the stage | `..` | `/home/user/condor_runs/.../sim_tests` |
| `CONDOR_JOB_ID` | 4-digit sequential job identifier | `0001` | `0001` |
| `CONDOR_JOB_NAME` | Configured name of the job | `arithmetic` | `arithmetic` |
| `CONDOR_JOB_DIR` | Dedicated working directory for the job | `.` | `/home/user/condor_runs/.../sim_tests/0001` |
| `BUILD_DIR` | Discovered build artifact directory | `../../build_rtl/0001` | `/home/user/condor_runs/.../build_rtl/0001` |
| `TEST_DIR` | Working test directory | `.` | `/home/user/condor_runs/.../sim_tests/0001` |
| `CONDOR_PATH_STYLE`| Configured path style | `relative` | `absolute` |
| `CONDOR_OUTPUT_MODE`| Configured output mode | `summary` | `summary` |
| `CONDOR_PIPELINE_DIR`| Directory containing the pipeline configuration | `../../../../examples/verilator/scripts` | `/home/user/project/examples/verilator/scripts` |
| `CONDOR_VAR_<KEY>` | Exported user and configuration variables | Varies | Varies |
