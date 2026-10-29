/**
 * @file config.hpp
 * @brief Configuration data structures and pipeline models parsed from YAML specifications.
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
#include <optional>
#include "condor/common.hpp"
#include "condor/variable.hpp"

namespace condor {

/**
 * @enum BackendType
 * @brief Backend execution category: local POSIX process group or remote template-based grid batch.
 */
enum class BackendType {
    LOCAL,     ///< Direct local POSIX process execution using fork/execvp and dedicated process groups.
    TEMPLATE   ///< Remote cluster or grid execution via parameterized shell command templates (e.g. Slurm, SGE, LSF).
};

inline std::string to_string(BackendType type) {
    switch (type) {
        case BackendType::LOCAL: return "local";
        case BackendType::TEMPLATE: return "template";
    }
    return "unknown";
}

/**
 * @struct BackendConfig
 * @brief Definition of an execution backend, including command templates for submission, polling, and cancellation.
 */
struct BackendConfig {
    std::string name{"local"};             ///< Unique name of backend identifier.
    BackendType type{BackendType::LOCAL};   ///< Backend execution type.
    std::string description;                ///< Human-readable description of backend.
    std::string submit_command;             ///< Template string executed to submit a job to the cluster.
    std::string poll_command;               ///< Template string executed to poll a submitted job's status.
    std::string cancel_command;             ///< Template string executed to cancel or kill a running job.
    std::string job_id_regex;               ///< Optional regex to extract the grid_id from submit command output.
    std::string success_pattern;            ///< Regex pattern identifying finished/successful jobs.
    std::string failure_pattern;            ///< Regex pattern identifying failed/aborted jobs.
    std::string running_pattern;            ///< Regex pattern identifying active/queued jobs.
    bool empty_poll_means_done{true};       ///< If poll output is empty, treat as completed.
};

/**
 * @struct HookConfig
 * @brief Lifecycle pre- and post-execution hooks configured at the global, stage, or job level.
 */
struct HookConfig {
    std::string pre;                           ///< Command or script executed before the stage or job starts.
    std::string post;                          ///< Command or script executed after the stage or job finishes.
    std::vector<std::string> pre_commands;     ///< List of individual pre-hook commands.
    std::vector<std::string> post_commands;    ///< List of individual post-hook commands.
    bool always{false};                        ///< If true, post hook executes even if the stage or job failed.
    bool allow_failure{false};                 ///< If true, non-zero return code from hook does not fail stage or job.
};

/**
 * @struct ContainerConfig
 * @brief Container virtualization configuration for isolated job/stage execution.
 */
struct ContainerConfig {
    std::string image;                                  ///< Container image name or tag (e.g. "ubuntu:22.04").
    std::string backend{"docker"};                      ///< Container runtime (docker, podman, singularity, apptainer).
    std::vector<std::string> options;                   ///< Additional command-line options passed to container run.
    std::vector<std::string> mounts;                    ///< Volume mount specifications (e.g. "/host:/container:ro").
};

/**
 * @struct ServerConfig
 * @brief Real-time telemetry reporting and remote control server configuration.
 */
struct ServerConfig {
    bool enabled{false};                                ///< If false, client operates 100% offline.
    std::string url;                                    ///< Server URL (e.g. "ws://localhost:8080/ws" or "http://localhost:8080/api/v1/telemetry").
    std::string token;                                  ///< Optional authentication Bearer token.
    std::string project;                                ///< Logical project identifier.
    std::vector<std::string> tags;                      ///< Descriptive tags for run categorization.
    int poll_interval_ms{500};                          ///< Telemetry streaming interval in milliseconds (default: 500ms).
};

/**
 * @struct JobDefaults
 * @brief Baseline default properties inherited by stages and jobs to eliminate repetitive YAML configurations.
 */
struct JobDefaults {
    std::string command;                                     ///< Default command line to execute if unspecified in job.
    std::string args;                                        ///< Default extra arguments appended to command.
    std::optional<bool> append_args;                         ///< If true, append args to parent defaults rather than overriding.
    std::optional<bool> scalable;                            ///< If false, job count is pinned and unaffected by count multipliers.
    std::string backend;                                     ///< Default backend identifier.
    std::string working_dir;                                 ///< Default working directory for execution.
    std::string timeout;                                     ///< Default timeout duration string (e.g. "30m").
    int timeout_sec{0};                                      ///< Parsed timeout duration in seconds.
    HookConfig hooks;                                        ///< Default lifecycle hooks.
    std::unordered_map<std::string, std::string> variables;  ///< Default variables scoped to this hierarchy level.
    std::unordered_map<std::string, int> resources;          ///< Default resource tokens required by jobs.
    int retry{0};                                            ///< Default retry attempts on job failure.
    int retry_delay_sec{0};                                  ///< Default delay in seconds before retrying a failed job.
    std::vector<int> retry_on_exit_codes;                    ///< Optional list of exit codes eligible for retry (empty = all).
    std::optional<ContainerConfig> container;                ///< Default container execution environment.
};

/**
 * @struct StageDefaults
 * @brief Baseline default properties inherited by stages across the pipeline.
 */
struct StageDefaults {
    HookConfig hooks;                                        ///< Default lifecycle hooks applied across all stages.
};

/**
 * @struct JobConfig
 * @brief Complete static configuration for an individual executable job in the DAG.
 */
struct JobConfig {
    std::string job_id;                                     ///< Formatted 4-digit unique identifier (e.g. "0001").
    std::string name;                                       ///< Descriptive human-readable job name.
    std::string command;                                    ///< Shell command to execute.
    std::string stage_name;                                 ///< Parent stage containing this job.
    std::string backend;                                    ///< Optional backend identifier override for this job.
    std::string working_dir;                                ///< Optional custom working directory for execution.
    std::string timeout;                                    ///< Optional timeout duration string (e.g. "15m", "300s").
    int timeout_sec{0};                                     ///< Parsed timeout in seconds (0 = disabled).
    std::string enabled_if;                                 ///< Conditional evaluation expression (e.g. "${coverage} == 1").
    bool allow_failure{false};                              ///< If true, non-zero exit code does not trigger failure policies.
    std::vector<std::string> depends_on;                    ///< List of prerequisite job IDs or stage names.
    HookConfig hooks;                                       ///< Job-level lifecycle hooks.
    std::unordered_map<std::string, std::string> variables; ///< Job-level scoped variables.
    std::optional<bool> append_args;                        ///< If true, append args to inherited defaults rather than overriding.
    std::optional<int> count;                               ///< Multi-instance count for replicated test execution.
    bool scalable{true};                                    ///< If false, job count is pinned and unaffected by CLI count multiplier.
    std::unordered_map<std::string, std::vector<std::string>> matrix; ///< Matrix parameter sweep variables.
    std::unordered_map<std::string, int> resources;         ///< Named resource tokens required to execute this job.
    int retry{0};                                           ///< Number of retry attempts on job failure.
    int retry_delay_sec{0};                                 ///< Delay in seconds before retrying this job.
    std::vector<int> retry_on_exit_codes;                   ///< Optional list of exit codes eligible for retry.
    std::optional<ContainerConfig> container;               ///< Optional container execution environment.
};

/**
 * @struct StageConfig
 * @brief Logical grouping and sequence boundary containing a collection of parallel jobs.
 */
struct StageConfig {
    std::string name;                     ///< Unique alphanumeric stage identifier.
    std::string backend;                  ///< Optional backend identifier override for all jobs in this stage.
    StageExecutionMode mode{StageExecutionMode::FULL}; ///< Stage execution mode (full, failed_only, cached).
    std::string enabled_if;               ///< Stage-level conditional evaluation expression.
    bool allow_failure{false};             ///< If true, stage failure does not abort the entire pipeline.
    std::vector<std::string> depends_on;  ///< Prerequisite stage names that must pass before this stage starts.
    HookConfig hooks;                     ///< Stage-level lifecycle hooks.
    std::vector<JobConfig> jobs;          ///< List of jobs configured directly inside this stage.
    std::vector<std::string> includes;    ///< Nested YAML test suite files included into this stage.
    JobDefaults defaults;                 ///< Stage-scoped defaults for jobs in this stage.
    std::optional<ContainerConfig> container; ///< Optional stage-level container execution environment.
};

/**
 * @struct PipelineSettings
 * @brief Global runtime execution options and command-line flags.
 */
struct PipelineSettings {
    size_t max_parallel_jobs{0};                          ///< Max concurrent jobs (0 = automatic hardware concurrency).
    std::string base_output_dir{"./condor_runs"};          ///< Root directory where execution timestamped runs are stored.
    std::string run_dir;                                  ///< Explicit run folder override if specified via --run-dir.
    std::string rerun_base_dir;                           ///< Previous run folder to re-run failures from.
    std::unordered_map<std::string, StageExecutionMode> stage_modes; ///< Per-stage execution modes for reruns.
    FailureAction on_failure_action{FailureAction::ABORT}; ///< Failure policy: abort, continue, or stage_barrier.
    bool exit_zero_on_failure{false};                     ///< If true, condor process exits with 0 even if jobs failed.
    int status_interval_sec{30};                          ///< Periodic status dump interval for CI / non-interactive mode.
    int grid_poll_interval_sec{10};                       ///< Polling interval in seconds for template/grid backends (default: 10s).
    bool emit_junit_xml{true};                            ///< Emit standard junit_report.xml upon completion.
    std::string junit_xml_path;                           ///< Custom destination path for junit_report.xml if specified.
    std::string backend{"local"};                         ///< Pipeline execution backend configured under settings.
    bool dry_run{false};                                  ///< If true, validate pipeline and print plan without executing.
    bool no_interactive{false};                           ///< If true, disable raw terminal escape codes and interactive menu.
    bool verbose_logging{false};                          ///< If true, print verbose debug logs.
    OutputMode output_mode{OutputMode::ALL};              ///< Verbosity filter: all, failures, or summary.
    PathStyle path_style{PathStyle::ABSOLUTE};            ///< Path format for environment exports: absolute or relative.
    std::string stage_filter;                             ///< Run only the specified stage if passed via --stage.
    std::string backend_override;                         ///< Force all jobs to use this backend if specified via --backend-override.
    size_t count_multiplier{1};                           ///< Multiplier for scalable job counts (default: 1).
    int kill_grace_period_sec{3};                         ///< Grace period in seconds between SIGTERM and SIGKILL (default: 3s).
    size_t max_retained_runs{0};                          ///< Maximum historical runs retained in base_output_dir (0 = unlimited).
    bool purge_successful_logs{false};                    ///< If true, purge raw log files of passing jobs upon completion.
    ServerConfig server;                                  ///< Central reporting and remote control server settings.
};

/**
 * @struct PipelineConfig
 * @brief Complete parsed pipeline configuration model representing the entire DAG.
 */
struct PipelineConfig {
    std::string name{"pipeline"};                            ///< Pipeline project name.
    VariableManager variables;                               ///< Global variable manager.
    PipelineSettings settings;                               ///< Runtime execution settings.
    JobDefaults defaults;                                    ///< Global job defaults applied across all stages.
    StageDefaults stage_defaults;                            ///< Global stage defaults applied across all stages.
    std::unordered_map<std::string, BackendConfig> backends; ///< Registered backend configurations.
    HookConfig hooks;                                        ///< Pipeline-level global hooks.
    std::vector<StageConfig> stages;                         ///< List of stages in the DAG.
    std::vector<std::string> includes;                       ///< Global YAML configuration files included at root level.
    std::unordered_map<std::string, int> resources;          ///< Named resource capacity pools (e.g. license tokens).

    /**
     * @brief Finds a stage configuration by name.
     * @param stage_name Identifier of stage to look up.
     * @return Pointer to StageConfig if found, or nullptr if not found.
     */
    const StageConfig* find_stage(std::string_view stage_name) const {
        for (const auto& stage : stages) {
            if (stage.name == stage_name) return &stage;
        }
        return nullptr;
    }
};

} // namespace condor
