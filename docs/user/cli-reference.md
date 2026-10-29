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
| `pipeline_file`, `--pipeline-config` | `<path>` | *(Required)* | Path to the pipeline YAML configuration file. Can be specified positionally or via `--pipeline-config`. |
| `--stage` | `<name>` | All stages | Execute only the specified stage. Must match POSIX identifier rules (`^[a-zA-Z0-9_-]+$`). |
| `--max-parallel-jobs` | `<N>` | Hardware concurrency | Maximum concurrent local jobs executed simultaneously. |
| `--count-multiplier` | `<N>` | `1` | Multiplier for scalable job counts across the pipeline. |
| `--base-output-dir` | `<path>` | `./condor_runs` | Base directory where timestamped run directories are created. |
| `--run-dir` | `<path>` | Auto-generated | Run inside an explicit fixed directory rather than generating a timestamped folder. |
| `-b`, `--backend`, `--backend-override` | `<name>` | Pipeline default | Set or override execution backend for all jobs (e.g. `local`, `slurm`, `lsf`, `sge`). |
| `--on-failure-action`| `abort`, `continue`, `stage_barrier` | `abort` | Override failure handling policy when a job fails. |
| `--exit-zero-on-failure`| None (Flag) | `false` | Return exit status code `0` even if individual jobs or stages encounter failures. |
| `--status-interval-sec`| `<seconds>` | `30` | Interval in seconds to output periodic status summary dumps in non-interactive CI mode. |
| `--output-mode` | `all`, `failures`, `summary` | `all` | Terminal event reporting verbosity. |
| `--path-style` | `absolute`, `relative` | `absolute` | Path representation inside runs. |
| `--relative-paths` | None (Flag) | `false` | Convenience flag to enable relative paths inside runs (equivalent to `--path-style=relative`). |
| `--absolute-paths` | None (Flag) | `true` | Convenience flag to force absolute paths inside runs (equivalent to `--path-style=absolute`). |
| `--define-variable` | `<KEY=VAL>` | None | Define or override pipeline variables from the command line. Repeatable for multiple variables. |
| `--define-resource`, `--resource` | `<NAME=CAPACITY>` | None | Define or override named resource pool capacities (e.g. `vcs=8`, `fpga=2`). Repeatable. |
| `--rerun` | `<path>` | None | Base run directory to incrementally re-run failures from. |
| `--stage-mode` | `<stage>:<mode>` | Configured in YAML | Override stage execution mode (`full`, `failed_only`, `cached`). Repeatable. |
| `--grid-poll-interval-sec` | `<seconds>` | `10` | Polling interval in seconds for cluster/grid backends. |
| `--junit-xml` | `<path>` | `artifacts/junit_report.xml` | Emit standard JUnit/xUnit XML test report to file. |
| `--kill-grace-period-sec` | `<seconds>` | `3` | Grace period in seconds between SIGTERM and SIGKILL during cancellation. |
| `--max-retained-runs` | `<N>` | `0` (keep all) | Maximum past run directories to retain in base_output_dir (0 = unlimited). |
| `--purge-successful-logs` | None (Flag) | `false` | Delete stdout.log, stderr.err, and runner scripts of passed jobs to conserve disk space. |
| `--dry-run` | None (Flag) | `false` | Validate YAML syntax, verify stage names, check for DAG cycles, evaluate conditions, print plan, and exit. |
| `--no-interactive` | None (Flag) | `false` | Disable interactive stdin polling and ANSI progress bar redraws. Recommended for CI logs. |
| `--verbose-logging`| None (Flag) | `false` | Enable detailed scheduler debug logging to stderr. |
| `--server-url` | `<url>` | None (Offline) | Central telemetry and control server URL (e.g. `ws://server:8080/ws` or `http://server:8080`). |
| `--server-token` | `<token>` | None | Authentication Bearer token for reporting to central server. |
| `--server-project` | `<name>` | None | Logical project identifier for fleet categorization. |
| `--server-tags` | `<tag>` | None | Categorization tags for run categorization on the server (repeatable). |
| `--server-poll-ms` | `<ms>` | `500` | Real-time telemetry streaming and command polling interval in milliseconds. |
| `--list-backends` | None (Flag) | None | Discover and list all available execution backends on the system and exit. |
| `--warranty` | None (Flag) | None | Display GNU General Public License warranty disclaimer and exit. |
| `--license` | None (Flag) | None | Display GNU General Public License redistribution conditions and exit. |
| `-v`, `--version` | None (Flag) | None | Display program version information and exit. |
| `-h`, `--help` | None (Flag) | None | Display command-line usage information and exit. |

---

## Failure Handling Policies (--on-failure-action)

When a job fails without `allow_failure: true`, Condor enforces one of three deterministic failure actions:

1. **`abort` (Default)**:
   Immediately sends `SIGTERM` followed by `SIGKILL` to all currently running process groups across all worker slots. Queued and pending stages are aborted, and the pipeline halts immediately.
2. **`continue`**:
   Active jobs in the failed stage continue running. Unrelated, independent downstream stages that do not depend on the failed stage continue executing. Only branches directly dependent on the failed stage are blocked.
3. **`stage_barrier`**:
   Jobs currently running within the active stage are permitted to finish naturally. However, no subsequent stages in the DAG are allowed to dispatch.

---

## Terminal Output Modes (--output-mode)

Control terminal output verbosity according to your operational environment:

- **`all` (Default)**:
  Logs both start and finish events for every job, including exit codes, elapsed durations, and full interactive progress redraws.
- **`failures`**:
  Suppresses start notifications and successful completions. Only outputs errors and failing jobs.
- **`summary`**:
  Hides individual job events entirely. Displays only the overall multi-bar progress and the final execution summary block.

---

## Path Style Configuration (--path-style)

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

During live interactive execution on a TTY, Condor listens asynchronously on `stdin` for keyboard interactions without interrupting active jobs.

Press **`m`** or **`Ctrl+C`** at any time to open the interactive runtime control menu:

| Command | Action | Description |
| :---: | :--- | :--- |
| **`s`** | **Status Table** | Prints an immediate, detailed tabular dump of all stages, jobs, PIDs, grid IDs, Peak RSS, and statuses. |
| **`p`** | **Pause / Resume** | Toggles scheduler dispatching. Pauses new job dispatches or resumes queued dispatches. |
| **`t <id>`** | **Tail Job Log** | Views the last 25 lines of stdout and stderr for a running or completed job without leaving the menu. |
| **`k <id>`** | **Kill Job** | Terminates an active job by its 4-digit job ID (e.g. `k 0002`). |
| **`j <N>`** | **Scale Slots** | Dynamically adjusts the maximum concurrent worker slots at runtime (e.g. `j 8`). |
| **`w`** | **Warranty** | Displays the GNU General Public License warranty disclaimer (§15-17). |
| **`l`** | **License** | Displays the GNU General Public License redistribution conditions (§4-5). |
| **`c`** | **Continue** | Resumes the live ANSI progress display. |
| **`q`** | **Quit / Abort All** | Signals all running process groups (`SIGTERM`/`SIGKILL`) and exits immediately. |

@note **Emergency Double Ctrl+C**: Pressing `Ctrl+C` twice within 2 seconds bypasses the menu and immediately terminates all active process groups across all worker slots. In automated CI/CD pipelines (such as GitHub Actions, GitLab CI, or Jenkins), pass `--no-interactive` to disable terminal polling and avoid consuming pipeline stdin.

---

## Built-in Environment Variables

Every job runner script (`job_runner.sh`) and hook script receives the following environment variables automatically exported by Condor:

| Variable | Description | Example (Relative Mode) | Example (Absolute Mode) |
| :--- | :--- | :--- | :--- |
| `CONDOR_RUN_DIR` | Top-level execution directory for the current run | `../../..` | `/home/user/condor_runs/20261026_120000_123456` |
| `CONDOR_STAGE_NAME`| Name of the active stage | `sim_tests` | `sim_tests` |
| `CONDOR_STAGE_DIR` | Directory holding all jobs for the stage | `..` | `/home/user/condor_runs/.../stages/sim_tests` |
| `CONDOR_JOB_ID` | 4-digit sequential job identifier | `0001` | `0001` |
| `CONDOR_JOB_NAME` | Configured name of the job | `arithmetic` | `arithmetic` |
| `CONDOR_JOB_DIR` | Dedicated working directory for the job | `.` | `/home/user/condor_runs/.../stages/sim_tests/0001` |
| `BUILD_DIR` | Discovered build artifact directory | `../../build_rtl/0001` | `/home/user/condor_runs/.../stages/build_rtl/0001` |
| `TEST_DIR` | Working test directory | `.` | `/home/user/condor_runs/.../stages/sim_tests/0001` |
| `CONDOR_ARTIFACTS_DIR` | Pipeline artifacts and reports directory | `../../../artifacts` | `/home/user/condor_runs/.../artifacts` |
| `CONDOR_PATH_STYLE`| Configured path style | `relative` | `absolute` |
| `CONDOR_OUTPUT_MODE`| Configured output mode | `summary` | `summary` |
| `CONDOR_PIPELINE_DIR`| Directory containing the pipeline configuration | `../../../../examples/hardware_test/scripts` | `/home/user/project/examples/hardware_test/scripts` |
| `CONDOR_RESOURCE_<NAME>`| Allocated token count for a named resource pool (e.g. `CONDOR_RESOURCE_VCS`) | `1` | `1` |
| `CONDOR_RESOURCES` | Comma-separated list of allocated resources and counts | `vcs=1,fpga=1` | `vcs=1,fpga=1` |
| `CONDOR_RETRY_COUNT` | Current retry attempt number (0 for initial run, 1 for first retry) | `0` | `0` |
| `CONDOR_VAR_<KEY>` | Exported user and configuration variables | Varies | Varies |

---

## Condor Hub CLI Reference (condor-hub)

`condor-hub` is the central reporting server and remote control plane executable. It accepts incoming WebSocket telemetry connections from Condor runners and serves the real-time web dashboard.

### Command Line Syntax

```bash
condor-hub [OPTIONS]
```

### Options

| Option | Argument | Default | Description |
| :--- | :--- | :--- | :--- |
| `-p`, `--port` | `<port>` | `8080` | Port to listen on for HTTP web dashboard and WebSocket connections. |
| `-H`, `--host` | `<host>` | `0.0.0.0` | Host or network interface address to bind. |
| `-a`, `--artifacts-dir` | `<path>` | `./hub_artifacts` | Directory where uploaded client HTML reports and run artifacts are stored. |
| `-t`, `--token`| `<token>`| *(empty)* | Optional Bearer authorization token for client and dashboard authentication. |
| `-w`, `--workers`| `<N>` | `4` | Worker thread count for epoll connection handling. |
| `-h`, `--help` | None (Flag) | None | Display command-line usage information and exit. |

### Running Condor Hub

```bash
# Start server with default settings (http://0.0.0.0:8080, storing reports in ./hub_artifacts)
condor-hub

# Bind to localhost on custom port with authentication token and dedicated artifacts storage
condor-hub --host 127.0.0.1 --port 9090 --token secret123 --artifacts-dir /var/log/condor/artifacts
```

### Server HTTP & WebSocket Endpoints

| Endpoint | Method / Protocol | Description |
| :--- | :--- | :--- |
| `/` | `GET` (HTTP) | Single-page real-time fleet dashboard UI. |
| `/api/v1/runs` | `GET` (HTTP) | Returns JSON list of all active and finished runs, including status, progress, metrics, and report links. |
| `/reports/<run_id>` | `GET` (HTTP) | Serves the standalone interactive HTML execution report uploaded by the runner upon pipeline completion. |
| `/reports/<run_id>/report.html` | `GET` (HTTP) | Direct path alias to the run's HTML report. |
| `/api/v1/runs/<run_id>/report` | `POST` (HTTP) | Uploads an HTML report file for a run (used by runners or external integrations). |
| `/api/v1/control` | `POST` (HTTP) | Submits runner control actions (`pause`, `resume`, `abort`, `kill_job`, `scale`). |
| `/api/v1/ws` | WebSocket (RFC 6455) | Bidirectional runner telemetry stream and remote control channel. |
| `/ws/dashboard` | WebSocket (RFC 6455) | Real-time broadcast channel for live browser UI telemetry updates. |
