# Interactive HTML Execution Report {#html_report}

For every pipeline run, Condor automatically compiles a zero-dependency, self-contained interactive HTML report located at:

```text
<run_dir>/report.html
```

When timestamped run directories are generated in `./condor_runs/`, Condor automatically maintains an atomic symlink `./condor_runs/latest` pointing to the most recent run, allowing you to access the latest report via:

```bash
xdg-open ./condor_runs/latest/report.html
# Or on macOS:
open ./condor_runs/latest/report.html
```

---

## Zero-Dependency Offline Philosophy

The HTML report is fully self-contained within a single `.html` file:
- **No External CDN Dependencies**: All CSS styling, SVG icons, fonts, and JavaScript interactions are completely embedded inline.
- **Air-Gapped & Secure**: Opens and functions perfectly in completely offline environments without internet access.
- **Easy Archiving & Sharing**: Can be emailed, uploaded as a CI/CD build artifact, or hosted on internal static web servers without requiring server-side rendering or database backends.

---

## Key Report Features

### 1. Executive Summary & Metadata Header
The report header provides an immediate high-level overview of execution parameters:
- **Pipeline Name & Verdict**: Clear, prominent `Passed` or `Failed` status badge.
- **Execution Metadata**: Active run directory path, UTC timestamp, worker concurrency slots, and configured path style (`relative` or `absolute`).
- **Stats Grid**: Metric cards summarizing Total Jobs, Passed Jobs, Failed Jobs, and formatted pipeline duration (`HH:MM:SS` or milliseconds).

### 2. Stage Progression Visualizer
A dedicated multi-track progress section displays the status of every stage:
- **Interactive Clickable Bars**: Clicking any stage bar instantly scrolls down to that stage and filters the view to show only that stage's jobs.
- **Visual Color Coding**: Green (`fill-pass`) for completed stages, Red (`fill-fail`) for stages with errors, and Gray (`fill-skip`) for skipped stages.
- **Skipped Stage Reporting**: Stages skipped due to `enabled_if` conditions are accurately displayed as `1/1 skipped` with 100% skipped bars rather than confusing `0/1 passed` counters.

### 3. Per-Stage Navigation Bar & Deep-Linking
A sticky stage toolbar allows filtering the entire report by individual stages:
- **Stage Pills**: Click any stage button (`build_rtl`, `sim_tests`, etc.) to isolate its jobs.
- **Count Badges**: Each button displays the exact number of jobs contained within that stage.
- **Active Stage Banner**: When filtered, displays `Viewing Stage: <name>` with a one-click `✕ Show All Stages` reset button.
- **URL Hash Deep-Linking**: The report updates the browser address bar with anchor hashes (e.g., `report.html#stage=sim_tests`). Sharing or bookmarking this URL immediately restores the filtered stage view.

### 4. Search & Multi-Criteria Filtering
Locate specific jobs and failure logs instantly:
- **Real-Time Search Box**: As-you-type fuzzy substring filtering matching across job IDs (`#0001`), job names, stage names, and shell commands.
- **Status Filter Pills**: Toggle between `All`, `Passed`, `Failed`, and `Skipped` jobs.
- **Global Collapse / Expand**: One-click button to expand or collapse all job cards simultaneously.

### 5. Detailed Job Cards
Each job is rendered as an expandable card featuring:
- **Header Badges**: Status pill (`PASSED`, `FAILED`, `SKIPPED`), job ID (`#0001`), stage name, job name, return exit code, and execution time in seconds.
- **Command Box**: The exact command string that was executed.
- **Job Directory**: Working folder path (rendered as a relative path when `path_style: relative` is active).
- **Custom Machine Metrics Grid**: Automatically parses any `*.yaml` files written to the job folder into structured, formatted key-value cards (e.g. `assertions_verified: 200000`, `cycles_simulated: 100000`).
- **Embedded Log Inspectors**: Collapsible syntax-highlighted viewers for standard output (`stdout.log`) and standard error (`stderr.err`).
