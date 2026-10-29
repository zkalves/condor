/**
 * @file scheduler.hpp
 * @brief Multi-threaded DAG scheduler, event loop, slot concurrency limiter, and failure coordinator.
 * @author Ezequiel Alves
 * @copyright Copyright (C) 2026 Ezequiel Alves. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include "condor/config.hpp"
#include "condor/workspace.hpp"
#include "condor/backend.hpp"
#include "condor/progress.hpp"

namespace condor {

/**
 * @struct JobRuntimeState
 * @brief Dynamic runtime execution state and monitoring data tracked by the scheduler for a single job.
 */
struct JobRuntimeState {
    JobConfig config;                                    ///< Static configuration copy of the job.
    JobStatus status{JobStatus::QUEUED};                 ///< Current dynamic lifecycle state.
    int exit_code{0};                                    ///< Process exit code recorded upon termination.
    double elapsed_sec{0.0};                             ///< Active runtime duration in fractional seconds.
    std::string pid_or_grid_id{"-"};                     ///< Process PID or cluster batch job identifier.
    std::unique_ptr<JobExecutionContext> exec_ctx;       ///< Active backend execution context.
    ExecutionBackend* backend{nullptr};                  ///< Pointer to assigned backend handler.
    int retry_count{0};                                  ///< Number of times this job has been retried.
    std::chrono::steady_clock::time_point next_retry_time; ///< Earliest time when next retry may be dispatched.
    double peak_rss_mb{0.0};                             ///< Peak memory (RSS) observed in MB.
};

/**
 * @struct StageRuntimeState
 * @brief Dynamic runtime execution state tracked by the scheduler for an entire stage boundary.
 */
struct StageRuntimeState {
    StageConfig config;                  ///< Static configuration copy of the stage.
    JobStatus status{JobStatus::QUEUED}; ///< Overall stage status (QUEUED, RUNNING, PASSED, FAILED, SKIPPED).
    std::vector<std::string> job_ids;    ///< List of job IDs contained within this stage.
    bool skipped{false};                 ///< True if stage was skipped due to enabled_if condition.
};

/**
 * @class Scheduler
 * @brief Central DAG orchestrator managing job dependencies, thread-safe dispatching, backend polling, and failure policies.
 *
 * The Scheduler drives the main event loop, evaluates readiness of stages and jobs according to topological
 * DAG ordering, submits ready jobs within the concurrency limit (max_slots), monitors running jobs via non-blocking
 * polling, and enforces failure recovery policies (abort, continue, or stage_barrier).
 */
class Scheduler {
public:
    /**
     * @brief Constructs a scheduler instance bound to a validated pipeline configuration and workspace manager.
     * @param config Validated PipelineConfig model.
     * @param workspace WorkspaceManager handling execution folder hierarchy.
     */
    Scheduler(const PipelineConfig& config, WorkspaceManager& workspace);

    /**
     * @brief Destructor ensuring all child processes and active jobs are cleaned up gracefully.
     */
    ~Scheduler();

    /**
     * @brief Executes the pipeline DAG until completion, abort, or failure barrier.
     * @return True if the pipeline succeeded according to failure policy rules, false otherwise.
     */
    bool run();

    /**
     * @brief Prints a formatted tabular view of all jobs, statuses, runtimes, and PIDs to stdout.
     */
    void print_status_table();

    /**
     * @brief Toggles dispatch pause state (stops launching new jobs while allowing running jobs to finish).
     */
    void toggle_pause();

    /**
     * @brief Checks if job dispatching is currently paused.
     * @return True if dispatching is paused, false otherwise.
     */
    bool is_paused() const { return dispatch_paused_; }

    /**
     * @brief Terminates a specific running job by sending signals to its backend process group.
     * @param job_id Unique 4-digit identifier of job to kill (e.g. "0002").
     */
    void kill_job(std::string_view job_id);

    /**
     * @brief Prints recent stdout/stderr output lines for an active or completed job.
     * @param target Job ID or job name to inspect (empty string selects first running job).
     * @param lines Number of trailing lines to display (default 25).
     */
    void tail_job(std::string_view target = "", size_t lines = 25);

    /**
     * @brief Dynamically adjusts maximum concurrent execution slots at runtime.
     * @param new_slots New maximum concurrent job limit.
     */
    void scale_slots(size_t new_slots);

    /**
     * @brief Gets current maximum concurrent execution slots.
     * @return Slot concurrency limit.
     */
    size_t current_slots() const { return max_slots_; }

    /**
     * @brief Immediately aborts all running jobs and halts scheduling of new jobs.
     */
    void abort_all();

    /**
     * @brief Checks if a stage should execute given the CLI `--stage` filter option.
     * @param stage_name Identifier of stage to check.
     * @return True if stage should execute, false if filtered out.
     */
    bool should_run_stage(std::string_view stage_name) const;

private:
    void init_runtime_states();
    void dispatch_ready_jobs();
    void poll_running_jobs();
    bool is_stage_ready(const StageRuntimeState& stage) const;
    bool is_job_ready(const JobRuntimeState& job) const;
    int execute_hook(const HookConfig& hook, bool is_pre,
                     std::string_view stage_name = "",
                     std::string_view job_id = "",
                     int last_exit_code = 0);

    ProgressStats compute_progress_stats() const;
    std::vector<StageSummaryInfo> compute_stage_summaries() const;
    std::vector<ActiveJobInfo> get_active_job_infos() const;
    void sync_stage_manifest(std::string_view stage_name);

    PipelineConfig config_;
    WorkspaceManager& workspace_;

    std::unordered_map<std::string, std::unique_ptr<ExecutionBackend>> backends_;
    std::unordered_map<std::string, JobRuntimeState> jobs_;
    std::vector<StageRuntimeState> stages_;

    std::atomic<size_t> max_slots_{4};
    std::atomic<bool> dispatch_paused_{false};
    std::atomic<bool> aborted_{false};
    std::atomic<bool> stage_barrier_triggered_{false};

    mutable std::mutex mutex_;
    std::unique_ptr<ProgressDisplay> progress_;
    std::chrono::steady_clock::time_point pipeline_start_time_;
    std::chrono::steady_clock::time_point last_grid_poll_time_;
};

} // namespace condor
